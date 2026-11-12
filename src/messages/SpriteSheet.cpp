#include "messages/SpriteSheet.hpp"

#include <QByteArray>
#include <QColor>
#include <QCryptographicHash>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPolygonF>
#include <QRect>
#include <QStringList>
#include <QTransform>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numeric>
#include <optional>
#include <vector>

namespace {

using namespace chatterino;

/// The animation clock ticks every 20 ms, so shorter frames can't be shown.
constexpr int MIN_FRAME_DURATION_MS = 20;

constexpr double PI = 3.14159265358979323846;

using Rgba = std::array<double, 4>;

double clamp01(double v)
{
    return std::min(1.0, std::max(0.0, v));
}

uchar toByte(double v)
{
    return static_cast<uchar>(std::lround(clamp01(v) * 255.0));
}

/// Applies a 3x3 colour matrix (row-major) to `px`.
Rgba applyMatrix(const Rgba &px, const std::array<double, 9> &m)
{
    return {
        m[0] * px[0] + m[1] * px[1] + m[2] * px[2],
        m[3] * px[0] + m[4] * px[1] + m[5] * px[2],
        m[6] * px[0] + m[7] * px[1] + m[8] * px[2],
        px[3],
    };
}

/// One pixel through one filter. Colours are 0..1, not premultiplied. The
/// formulas are the ones of the CSS Filter Effects spec.
Rgba applyFilter(Rgba px, const SpriteColorFilter &filter)
{
    using Kind = SpriteColorFilter::Kind;

    switch (filter.kind)
    {
        case Kind::Brightness: {
            const double a = std::max(0.0, filter.amount);
            px[0] = clamp01(px[0] * a);
            px[1] = clamp01(px[1] * a);
            px[2] = clamp01(px[2] * a);
            return px;
        }
        case Kind::Contrast: {
            const double a = std::max(0.0, filter.amount);
            for (int i = 0; i < 3; i++)
            {
                px[i] = clamp01(a * px[i] + (0.5 - 0.5 * a));
            }
            return px;
        }
        case Kind::Saturate: {
            const double s = std::max(0.0, filter.amount);
            return applyMatrix(px, {
                                       0.213 + 0.787 * s,
                                       0.715 - 0.715 * s,
                                       0.072 - 0.072 * s,
                                       0.213 - 0.213 * s,
                                       0.715 + 0.285 * s,
                                       0.072 - 0.072 * s,
                                       0.213 - 0.213 * s,
                                       0.715 - 0.715 * s,
                                       0.072 + 0.928 * s,
                                   });
        }
        case Kind::HueRotate: {
            const double rad = filter.amount * PI / 180.0;
            const double c = std::cos(rad);
            const double s = std::sin(rad);
            return applyMatrix(px, {
                                       0.213 + c * 0.787 - s * 0.213,
                                       0.715 - c * 0.715 - s * 0.715,
                                       0.072 - c * 0.072 + s * 0.928,
                                       0.213 - c * 0.213 + s * 0.143,
                                       0.715 + c * 0.285 + s * 0.140,
                                       0.072 - c * 0.072 - s * 0.283,
                                       0.213 - c * 0.213 - s * 0.787,
                                       0.715 - c * 0.715 + s * 0.715,
                                       0.072 + c * 0.928 + s * 0.072,
                                   });
        }
        case Kind::Grayscale: {
            const double k = 1.0 - clamp01(filter.amount);
            return applyMatrix(px, {
                                       0.2126 + 0.7874 * k,
                                       0.7152 - 0.7152 * k,
                                       0.0722 - 0.0722 * k,
                                       0.2126 - 0.2126 * k,
                                       0.7152 + 0.2848 * k,
                                       0.0722 - 0.0722 * k,
                                       0.2126 - 0.2126 * k,
                                       0.7152 - 0.7152 * k,
                                       0.0722 + 0.9278 * k,
                                   });
        }
        case Kind::Sepia: {
            const double k = 1.0 - clamp01(filter.amount);
            return applyMatrix(px, {
                                       0.393 + 0.607 * k,
                                       0.769 - 0.769 * k,
                                       0.189 - 0.189 * k,
                                       0.349 - 0.349 * k,
                                       0.686 + 0.314 * k,
                                       0.168 - 0.168 * k,
                                       0.272 - 0.272 * k,
                                       0.534 - 0.534 * k,
                                       0.131 + 0.869 * k,
                                   });
        }
        case Kind::Invert: {
            const double a = clamp01(filter.amount);
            for (int i = 0; i < 3; i++)
            {
                px[i] = a * (1.0 - px[i]) + (1.0 - a) * px[i];
            }
            return px;
        }
        case Kind::Opacity: {
            px[3] = px[3] * clamp01(filter.amount);
            return px;
        }
    }
    return px;
}

/// Floor division, also for negative numbers.
int floorDiv(int a, int b)
{
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
    {
        q--;
    }
    return q;
}

/// The `size` area of `source` at `offset`. What lies outside of the sheet is
/// explicitly transparent, unless the sheet repeats in that direction: then
/// it's the other end of the sheet, like a CSS background that is `repeat`ed.
/// (QImage::copy() fills the outside with zeroes, which is only transparent
/// for some formats.)
QImage copyFrame(const QImage &source, QPoint offset, QSize size, bool tileX,
                 bool tileY)
{
    QImage out(size, QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    if (source.isNull())
    {
        return out;
    }

    const int w = source.width();
    const int h = source.height();
    const QRect wanted(offset, size);

    const int firstX = tileX ? floorDiv(wanted.left(), w) : 0;
    const int lastX = tileX ? floorDiv(wanted.right(), w) : 0;
    const int firstY = tileY ? floorDiv(wanted.top(), h) : 0;
    const int lastY = tileY ? floorDiv(wanted.bottom(), h) : 0;

    QPainter painter(&out);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    for (int ty = firstY; ty <= lastY; ty++)
    {
        for (int tx = firstX; tx <= lastX; tx++)
        {
            // where this copy of the sheet lies
            const QRect tile(tx * w, ty * h, w, h);
            const QRect inside = wanted.intersected(tile);
            if (inside.isEmpty())
            {
                continue;
            }
            // `inside` in the coordinates of the sheet
            const QRect from = inside.translated(-tile.topLeft());
            painter.drawImage(inside.topLeft() - offset, source, from);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Overlays (the `::before` / `::after` of an emote)
// ---------------------------------------------------------------------------

int toByte255(double v)
{
    return std::clamp(static_cast<int>(std::lround(v)), 0, 255);
}

QColor toQColor(const std::array<double, 4> &rgba)
{
    return QColor(toByte255(rgba[0]), toByte255(rgba[1]), toByte255(rgba[2]),
                  toByte255(rgba[3] * 255.0));
}

/// The names of a CSS `font-family` list that are real families. `monospace`
/// is reported separately, the other generic names are left out (Qt picks
/// its default for them).
QStringList cssFamilies(const QString &list, bool &monospace)
{
    QStringList out;
    for (QString part : list.split(QChar(u',')))
    {
        part = part.trimmed();
        if (part.size() >= 2 &&
            (part.startsWith(QChar(u'"')) || part.startsWith(QChar(u'\''))) &&
            part.endsWith(part.at(0)))
        {
            part = part.mid(1, part.size() - 2);
        }
        if (part.isEmpty())
        {
            continue;
        }
        const QString lower = part.toLower();
        if (lower == u"monospace")
        {
            monospace = true;
        }
        else if (lower != u"serif" && lower != u"sans-serif" &&
                 lower != u"cursive" && lower != u"fantasy")
        {
            out.append(part);
        }
    }
    return out;
}

/// How much bigger than its CSS size the text is measured and drawn. Qt takes
/// whole pixel sizes only, and 8.5px rounded to 9px is wide enough to wrap a
/// line that fits in the browser (HACKERMAN's "ID: 420"). The text is laid out
/// at `kTextScale` times the size and the painter scales it back down.
constexpr double kTextScale = 8.0;

QFont overlayFont(const SpriteOverlay &o)
{
    QFont font;
    bool monospace = false;
    QStringList families = cssFamilies(o.fontFamily, monospace);
    const bool nonAscii =
        std::any_of(o.text.begin(), o.text.end(),
                    [](QChar c) { return c.unicode() > 0x7F; });
    if (nonAscii)
    {
        // an emoji: the families that have colour glyphs
        families << QStringLiteral("Segoe UI Emoji")
                 << QStringLiteral("Apple Color Emoji")
                 << QStringLiteral("Noto Color Emoji");
    }
    if (!families.isEmpty())
    {
        font.setFamilies(families);
    }
    if (monospace)
    {
        font.setStyleHint(QFont::Monospace);
        font.setFixedPitch(true);
    }
    font.setPixelSize(std::max(
        1, static_cast<int>(std::lround(o.fontPx * kTextScale))));
    font.setHintingPreference(QFont::PreferNoHinting);
    if (o.letterSpacingPx != 0.0)
    {
        font.setLetterSpacing(QFont::AbsoluteSpacing,
                              o.letterSpacingPx * kTextScale);
    }
    return font;
}

/// Breaks `text` at spaces so that no line is wider than `maxWidth` (a word
/// that is wider stays on a line of its own, as in CSS).
QStringList wrapText(const QString &text, const QFontMetricsF &metrics,
                     double maxWidth)
{
    QStringList lines;
    QString current;
    for (const QString &word : text.split(QChar(u' '), Qt::SkipEmptyParts))
    {
        const QString candidate =
            current.isEmpty() ? word : current + QChar(u' ') + word;
        if (!current.isEmpty() && metrics.horizontalAdvance(candidate) > maxWidth)
        {
            lines.append(current);
            current = word;
        }
        else
        {
            current = candidate;
        }
    }
    if (!current.isEmpty())
    {
        lines.append(current);
    }
    return lines;
}

/// One pass of a box blur over every pixel channel (premultiplied ARGB32,
/// outside of the image is transparent).
void blurPass(QImage &image, int radius, bool horizontal)
{
    const int length = horizontal ? image.width() : image.height();
    const int lines = horizontal ? image.height() : image.width();
    const int window = 2 * radius + 1;

    std::vector<std::array<int, 4>> in(static_cast<size_t>(length));
    for (int l = 0; l < lines; l++)
    {
        for (int i = 0; i < length; i++)
        {
            const int x = horizontal ? i : l;
            const int y = horizontal ? l : i;
            const uchar *px = image.constScanLine(y) + 4 * x;
            for (int c = 0; c < 4; c++)
            {
                in[static_cast<size_t>(i)][static_cast<size_t>(c)] = px[c];
            }
        }

        std::array<int, 4> sum{0, 0, 0, 0};
        for (int i = -radius; i <= radius; i++)
        {
            if (i >= 0 && i < length)
            {
                for (size_t c = 0; c < 4; c++)
                {
                    sum[c] += in[static_cast<size_t>(i)][c];
                }
            }
        }
        for (int i = 0; i < length; i++)
        {
            const int x = horizontal ? i : l;
            const int y = horizontal ? l : i;
            uchar *px = image.scanLine(y) + 4 * x;
            for (size_t c = 0; c < 4; c++)
            {
                px[c] = static_cast<uchar>(sum[c] / window);
            }
            const int leaving = i - radius;
            const int entering = i + radius + 1;
            for (size_t c = 0; c < 4; c++)
            {
                if (leaving >= 0)
                {
                    sum[c] -= in[static_cast<size_t>(leaving)][c];
                }
                if (entering < length)
                {
                    sum[c] += in[static_cast<size_t>(entering)][c];
                }
            }
        }
    }
}

/// A gaussian blur of standard deviation `sigma`, as three box blurs.
void blurImage(QImage &image, double sigma)
{
    if (sigma < 0.3)
    {
        return;
    }
    const int radius = std::max(1, static_cast<int>(std::lround(sigma - 0.5)));
    for (int pass = 0; pass < 3; pass++)
    {
        blurPass(image, radius, true);
        blurPass(image, radius, false);
    }
}

QImage transparentImage(QSize size)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    return image;
}

/// A shadow that falls outside of `box` (CSS never draws it below the box).
void drawOuterShadow(QImage &target, const QRectF &box, const SpriteShadow &s)
{
    const QRectF shape = box.translated(s.x, s.y)
                             .adjusted(-s.spread, -s.spread, s.spread, s.spread);
    if (shape.width() <= 0 || shape.height() <= 0)
    {
        return;
    }

    QImage mask = transparentImage(target.size());
    {
        QPainter painter(&mask);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(toQColor(s.rgba));
        painter.drawRect(shape);
    }
    blurImage(mask, s.blur / 2.0);
    {
        QPainter painter(&mask);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);
        painter.drawRect(box);
    }
    QPainter painter(&target);
    painter.drawImage(0, 0, mask);
}

/// A shadow inside of `box`: what is left of the box when its shifted and
/// shrunk copy is taken away.
void drawInsetShadow(QImage &target, const QRectF &box, const SpriteShadow &s)
{
    QImage mask = transparentImage(target.size());
    {
        QPainter painter(&mask);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(toQColor(s.rgba));
        painter.drawRect(box);

        const QRectF hole = box.translated(s.x, s.y)
                                .adjusted(s.spread, s.spread, -s.spread,
                                          -s.spread);
        if (hole.width() > 0 && hole.height() > 0)
        {
            painter.setCompositionMode(
                QPainter::CompositionMode_DestinationOut);
            painter.setBrush(Qt::black);
            painter.drawRect(hole);
        }
    }
    blurImage(mask, s.blur / 2.0);

    // the blur spreads outwards, too: only what is inside of the box stays
    QImage inside = transparentImage(target.size());
    {
        QPainter painter(&inside);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);
        painter.drawRect(box);
    }
    {
        QPainter painter(&mask);
        painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        painter.drawImage(0, 0, inside);
    }
    QPainter painter(&target);
    painter.drawImage(0, 0, mask);
}

/// Draws one overlay on `target` (which is the emote's box, or the canvas
/// that the box is part of). Whatever sticks out of `target` is lost.
void drawOverlay(QImage &target, const SpriteOverlay &o)
{
    const QSize box = target.size();
    const QFont font = overlayFont(o);
    const QFontMetricsF metrics(font);
    // `metrics` is for the font at `kTextScale` times its size
    const double lineHeight =
        o.lineHeightPx > 0 ? o.lineHeightPx : metrics.height() / kTextScale;

    QStringList lines;
    double textWidth = 0;
    if (!o.text.isEmpty())
    {
        lines = o.width ? wrapText(o.text, metrics, *o.width * kTextScale)
                        : QStringList{o.text};
        for (const auto &line : lines)
        {
            textWidth = std::max(textWidth,
                                 metrics.horizontalAdvance(line) / kTextScale);
        }
    }
    const double elementW = o.width.value_or(textWidth);
    const double elementH =
        o.height.value_or(static_cast<double>(lines.size()) * lineHeight);

    // absolute positioning against the box; `auto` on both sides is the
    // top-left corner
    double x = 0;
    double y = 0;
    if (o.left)
    {
        x = *o.left;
    }
    else if (o.right)
    {
        x = box.width() - *o.right - elementW;
    }
    if (o.top)
    {
        y = *o.top;
    }
    else if (o.bottom)
    {
        y = box.height() - *o.bottom - elementH;
    }

    // The element is drawn into a layer of its own (so that the transform
    // moves its shadows too), which has to be big enough for it wherever it
    // lies and for the blur of its shadows
    double reach = 2;
    for (const auto &shadow : o.shadows)
    {
        reach = std::max(reach, shadow.blur * 1.5 + std::abs(shadow.spread) +
                                    std::max(std::abs(shadow.x),
                                             std::abs(shadow.y)) +
                                    2);
    }
    reach = std::min(reach, 64.0);
    QRectF area = QRectF(0, 0, box.width(), box.height())
                      .united(QRectF(x - reach, y - reach, elementW + 2 * reach,
                                     elementH + 2 * reach));
    area = area.intersected(
        QRectF(-256, -256, box.width() + 512, box.height() + 512));
    const QPoint layerOrigin(static_cast<int>(std::floor(area.left())),
                             static_cast<int>(std::floor(area.top())));
    const QSize layerSize(
        static_cast<int>(std::ceil(area.right())) - layerOrigin.x(),
        static_cast<int>(std::ceil(area.bottom())) - layerOrigin.y());
    if (layerSize.isEmpty())
    {
        return;
    }

    QImage layer = transparentImage(layerSize);
    const QRectF rect(x - layerOrigin.x(), y - layerOrigin.y(), elementW,
                      elementH);
    for (auto it = o.shadows.rbegin(); it != o.shadows.rend(); ++it)
    {
        if (!it->inset)
        {
            drawOuterShadow(layer, rect, *it);
        }
    }
    for (auto it = o.shadows.rbegin(); it != o.shadows.rend(); ++it)
    {
        if (it->inset)
        {
            drawInsetShadow(layer, rect, *it);
        }
    }
    if (!lines.isEmpty())
    {
        QPainter painter(&layer);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setFont(font);
        painter.setPen(toQColor(o.color));
        // the font is `kTextScale` times too big: draw in a scaled-up space
        painter.scale(1.0 / kTextScale, 1.0 / kTextScale);
        for (qsizetype i = 0; i < lines.size(); i++)
        {
            const double baseline =
                rect.top() + static_cast<double>(i) * lineHeight +
                (lineHeight - metrics.height() / kTextScale) / 2.0 +
                metrics.ascent() / kTextScale;
            painter.drawText(QPointF(rect.left() * kTextScale,
                                     baseline * kTextScale),
                             lines[static_cast<qsizetype>(i)]);
        }
    }

    if (!o.filters.empty())
    {
        layer = applySpriteFilters(layer, o.filters);
    }

    const double originX =
        x + (o.originXPercent ? o.originX * elementW / 100.0 : o.originX);
    const double originY =
        y + (o.originYPercent ? o.originY * elementH / 100.0 : o.originY);
    const auto &m = o.matrix;

    QPainter painter(&target);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setOpacity(std::clamp(o.opacity, 0.0, 1.0));
    // the calls act in reverse order on a point: move the origin to (0, 0),
    // transform, move it back
    painter.translate(originX, originY);
    painter.setTransform(QTransform(m[0], m[1], m[2], m[3], m[4], m[5]), true);
    painter.translate(-originX, -originY);
    painter.drawImage(QPointF(layerOrigin), layer);
}

/// A frame with its overlays on it, on its canvas.
QImage composeFrame(const QImage &frame, const SpriteSheetSpec &spec,
                    const std::vector<SpriteOverlay> *overlays)
{
    const bool hasOverlays = overlays != nullptr && !overlays->empty();
    if (spec.canvasSize.isEmpty() && !hasOverlays)
    {
        return frame;
    }

    const QSize canvasSize =
        spec.canvasSize.isEmpty() ? frame.size() : spec.canvasSize;
    QImage canvas = transparentImage(canvasSize);
    {
        QPainter painter(&canvas);
        painter.drawImage(spec.canvasImagePos, frame);
    }
    if (hasOverlays)
    {
        QImage layer = transparentImage(canvasSize);
        for (const auto &overlay : *overlays)
        {
            drawOverlay(layer, overlay);
        }
        if (spec.canvasMirror)
        {
            layer = layer.mirrored(true, false);
        }
        QPainter painter(&canvas);
        painter.drawImage(0, 0, layer);
    }
    return canvas.convertToFormat(QImage::Format_ARGB32);
}

}  // namespace

namespace chatterino {

bool SpritePose::isIdentity() const
{
    return this->matrix == std::array<double, 6>{1, 0, 0, 1, 0, 0} &&
           this->opacity == 1.0 && this->clip.empty();
}

bool SpriteSheetSpec::isValid() const
{
    return this->frameSize.width() > 0 && this->frameSize.height() > 0 &&
           !this->offsets.empty() &&
           this->durationsMs.size() == this->offsets.size() &&
           (this->poses.empty() || this->poses.size() == this->offsets.size()) &&
           (this->overlays.empty() ||
            this->overlays.size() == this->offsets.size());
}

QMargins SpriteSheetSpec::canvasMargins() const
{
    if (this->canvasSize.isEmpty())
    {
        return {};
    }
    const int left = std::max(0, this->canvasImagePos.x());
    const int top = std::max(0, this->canvasImagePos.y());
    const int right = std::max(0, this->canvasSize.width() -
                                      this->canvasImagePos.x() -
                                      this->frameSize.width());
    const int bottom = std::max(0, this->canvasSize.height() -
                                       this->canvasImagePos.y() -
                                       this->frameSize.height());
    return QMargins(left, top, right, bottom);
}

QString SpriteSheetSpec::cacheKey() const
{
    QByteArray blob;
    blob.append(QByteArray::number(this->frameSize.width()));
    blob.append('x');
    blob.append(QByteArray::number(this->frameSize.height()));
    blob.append('|');
    for (const auto &offset : this->offsets)
    {
        blob.append(QByteArray::number(offset.x()));
        blob.append(',');
        blob.append(QByteArray::number(offset.y()));
        blob.append(';');
    }
    blob.append('|');
    for (const auto duration : this->durationsMs)
    {
        blob.append(QByteArray::number(duration));
        blob.append(';');
    }
    blob.append('|');
    if (this->tileX || this->tileY)
    {
        // only added when set, so the key of a sheet that doesn't repeat is
        // what it always was
        blob.append(this->tileX ? "tx" : "");
        blob.append(this->tileY ? "ty" : "");
        blob.append('|');
    }
    for (const auto &filter : this->filters)
    {
        blob.append(QByteArray::number(static_cast<int>(filter.kind)));
        blob.append(':');
        blob.append(QByteArray::number(filter.amount, 'g', 10));
        blob.append(';');
    }

    if (!this->poses.empty())
    {
        const auto addPose = [&](const SpritePose &pose) {
            for (const auto v : pose.matrix)
            {
                blob.append(QByteArray::number(v, 'g', 8));
                blob.append(',');
            }
            blob.append(QByteArray::number(pose.opacity, 'g', 8));
            blob.append('f');
            for (const auto &filter : pose.filters)
            {
                blob.append(QByteArray::number(static_cast<int>(filter.kind)));
                blob.append(':');
                blob.append(QByteArray::number(filter.amount, 'g', 8));
                blob.append(',');
            }
            blob.append('c');
            for (const auto &[x, y] : pose.clip)
            {
                blob.append(QByteArray::number(x, 'g', 8));
                blob.append(',');
                blob.append(QByteArray::number(y, 'g', 8));
                blob.append(',');
            }
            blob.append(';');
        };
        blob.append("|P");
        for (const auto &pose : this->poses)
        {
            addPose(pose);
        }
        blob.append('r');
        addPose(this->restPose);
    }

    if (this->hasOverlays() || !this->canvasSize.isEmpty())
    {
        const auto num = [&](double v) {
            blob.append(QByteArray::number(v, 'g', 8));
            blob.append(',');
        };
        const auto numOrNone = [&](const std::optional<double> &v) {
            if (v)
            {
                num(*v);
            }
            else
            {
                blob.append("n,");
            }
        };
        const auto addOverlay = [&](const SpriteOverlay &o) {
            blob.append(o.text.toUtf8());
            blob.append('\x1f');
            blob.append(o.fontFamily.toUtf8());
            blob.append('\x1f');
            num(o.fontPx);
            for (const auto v : o.color)
            {
                num(v);
            }
            num(o.letterSpacingPx);
            num(o.lineHeightPx);
            numOrNone(o.left);
            numOrNone(o.top);
            numOrNone(o.right);
            numOrNone(o.bottom);
            numOrNone(o.width);
            numOrNone(o.height);
            for (const auto v : o.matrix)
            {
                num(v);
            }
            num(o.originX);
            num(o.originXPercent ? 1 : 0);
            num(o.originY);
            num(o.originYPercent ? 1 : 0);
            num(o.opacity);
            for (const auto &shadow : o.shadows)
            {
                blob.append(shadow.inset ? "i" : "o");
                num(shadow.x);
                num(shadow.y);
                num(shadow.blur);
                num(shadow.spread);
                for (const auto v : shadow.rgba)
                {
                    num(v);
                }
            }
            for (const auto &filter : o.filters)
            {
                blob.append('f');
                num(static_cast<double>(filter.kind));
                num(filter.amount);
            }
            blob.append(';');
        };
        blob.append("|O");
        for (const auto &list : this->overlays)
        {
            for (const auto &overlay : list)
            {
                addOverlay(overlay);
            }
            blob.append('/');
        }
        blob.append('r');
        for (const auto &overlay : this->restOverlays)
        {
            addOverlay(overlay);
        }
        blob.append('c');
        blob.append(QByteArray::number(this->canvasSize.width()));
        blob.append(',');
        blob.append(QByteArray::number(this->canvasSize.height()));
        blob.append(',');
        blob.append(QByteArray::number(this->canvasImagePos.x()));
        blob.append(',');
        blob.append(QByteArray::number(this->canvasImagePos.y()));
        blob.append(this->canvasMirror ? 'm' : 'u');
    }

    // Only added when set, so the key of an ordinary (looping) animation
    // is what it always was
    if (this->loopCount > 0 || this->instanceId != 0)
    {
        blob.append('|');
        blob.append(QByteArray::number(this->loopCount));
        blob.append(',');
        blob.append(QByteArray::number(this->restOffset.x()));
        blob.append(',');
        blob.append(QByteArray::number(this->restOffset.y()));
        blob.append(',');
        blob.append(QByteArray::number(this->instanceId));
    }

    return QString::fromLatin1(
        QCryptographicHash::hash(blob, QCryptographicHash::Sha1).toHex());
}

QImage applySpriteFilters(const QImage &image,
                          const std::vector<SpriteColorFilter> &filters)
{
    QImage out = image.convertToFormat(QImage::Format_ARGB32);
    if (filters.empty())
    {
        return out;
    }

    for (int y = 0; y < out.height(); y++)
    {
        auto *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < out.width(); x++)
        {
            const QRgb in = line[x];
            Rgba px{
                qRed(in) / 255.0,
                qGreen(in) / 255.0,
                qBlue(in) / 255.0,
                qAlpha(in) / 255.0,
            };
            for (const auto &filter : filters)
            {
                px = applyFilter(px, filter);
                // every filter function of a chain clamps its result, as
                // browsers do: without it saturate(60) overshoots by far and
                // the next filter turns the emote plain red (COOMER)
                px[0] = clamp01(px[0]);
                px[1] = clamp01(px[1]);
                px[2] = clamp01(px[2]);
            }
            line[x] = qRgba(toByte(px[0]), toByte(px[1]), toByte(px[2]),
                            toByte(px[3]));
        }
    }
    return out;
}

QImage applySpritePose(const QImage &frame, const SpritePose &pose)
{
    QImage filtered = applySpriteFilters(frame, pose.filters);
    if (pose.isIdentity())
    {
        return filtered;
    }

    QImage out(filtered.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);

    QPainter painter(&out);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (pose.clip.size() >= 3)
    {
        // the polygon is in the element's own coordinates, so it is set
        // before the transform moves the element
        QPolygonF polygon;
        for (const auto &[x, y] : pose.clip)
        {
            polygon << QPointF(x, y);
        }
        QPainterPath clipPath;
        clipPath.addPolygon(polygon);
        clipPath.closeSubpath();

        QImage clipped(filtered.size(), QImage::Format_ARGB32_Premultiplied);
        clipped.fill(Qt::transparent);
        {
            QPainter clipper(&clipped);
            clipper.setRenderHint(QPainter::Antialiasing, true);
            clipper.setClipPath(clipPath);
            clipper.drawImage(0, 0, filtered);
        }
        filtered = std::move(clipped);
    }

    painter.setOpacity(std::clamp(pose.opacity, 0.0, 1.0));
    const auto &m = pose.matrix;
    painter.setTransform(QTransform(m[0], m[1], m[2], m[3], m[4], m[5]));
    painter.drawImage(0, 0, filtered);
    painter.end();

    return out.convertToFormat(QImage::Format_ARGB32);
}

std::vector<SpriteFrame> sliceSpriteSheet(const QImage &sheet,
                                          const SpriteSheetSpec &spec,
                                          qint64 maxBytes)
{
    std::vector<SpriteFrame> out;
    if (sheet.isNull() || !spec.isValid())
    {
        return out;
    }

    // QImage::copy() fills whatever lies outside of the sheet with zeroes,
    // which is only transparent for formats that have an alpha channel.
    const QImage source = sheet.convertToFormat(QImage::Format_ARGB32);

    size_t count = spec.offsets.size();
    const QSize outSize =
        spec.canvasSize.isEmpty() ? spec.frameSize : spec.canvasSize;
    const qint64 frameBytes =
        static_cast<qint64>(outSize.width()) * outSize.height() * 4;
    bool addRest = spec.playsOnce();
    if (frameBytes * static_cast<qint64>(count + (addRest ? 1 : 0)) > maxBytes)
    {
        count = 1;
        addRest = false;
    }

    out.reserve(count);
    for (size_t i = 0; i < count; i++)
    {
        QImage frame = copyFrame(source, spec.offsets[i], spec.frameSize,
                                    spec.tileX, spec.tileY);
        if (spec.hasPoses())
        {
            frame = applySpritePose(frame, spec.poses[i]);
        }
        else if (!spec.filters.empty())
        {
            frame = applySpriteFilters(frame, spec.filters);
        }
        frame = composeFrame(
            frame, spec, spec.overlays.empty() ? nullptr : &spec.overlays[i]);

        const int duration =
            count > 1 ? std::max(MIN_FRAME_DURATION_MS, spec.durationsMs[i])
                      : 0;
        out.push_back(SpriteFrame{
            .image = std::move(frame),
            .durationMs = duration,
        });
    }

    if (addRest)
    {
        QImage rest = copyFrame(source, spec.restOffset, spec.frameSize,
                                spec.tileX, spec.tileY);
        if (spec.hasPoses())
        {
            rest = applySpritePose(rest, spec.restPose);
        }
        else if (!spec.filters.empty())
        {
            rest = applySpriteFilters(rest, spec.filters);
        }
        rest = composeFrame(rest, spec,
                            spec.overlays.empty() ? nullptr
                                                  : &spec.restOverlays);
        out.push_back(SpriteFrame{.image = std::move(rest), .durationMs = 0});
    }

    return out;
}

PosedNativeFrames composePosedNativeFrames(
    const std::vector<SpriteFrame> &native, const SpriteSheetSpec &spec,
    qint64 maxBytes)
{
    PosedNativeFrames out;
    if (native.empty() || !spec.hasPoses() ||
        spec.poses.size() != spec.durationsMs.size())
    {
        return out;
    }

    constexpr qint64 MAX_PHASE_MS = 60000;
    constexpr int MAX_POSE_PLAYS = 50;
    constexpr size_t MAX_COMPOSED_FRAMES = 1500;

    const size_t poseCount = spec.poses.size();
    const size_t nativeCount = native.size();

    std::vector<QImage> sources;
    sources.reserve(nativeCount);
    for (const auto &frame : native)
    {
        sources.push_back(frame.image.convertToFormat(QImage::Format_ARGB32));
    }

    const auto nativeDuration = [&](size_t i) -> qint64 {
        const int d = native[i].durationMs > 0 ? native[i].durationMs : 100;
        return std::max(MIN_FRAME_DURATION_MS, d);
    };
    const auto poseDuration = [&](size_t i) -> qint64 {
        return std::max(MIN_FRAME_DURATION_MS, spec.durationsMs[i]);
    };

    qint64 poseTotal = 0;
    for (size_t i = 0; i < poseCount; i++)
    {
        poseTotal += poseDuration(i);
    }
    qint64 nativeTotal = 0;
    for (size_t i = 0; i < nativeCount; i++)
    {
        nativeTotal += nativeDuration(i);
    }

    if (poseCount == 1 && spec.loopCount <= 0)
    {
        // one pose that never changes (a flip): every native frame, posed
        out.loopFrom = 0;
        for (size_t i = 0; i < nativeCount; i++)
        {
            out.frames.push_back(SpriteFrame{
                .image = applySpritePose(sources[i], spec.poses.front()),
                .durationMs = static_cast<int>(nativeDuration(i)),
            });
        }
        return out;
    }

    const qint64 frameBytes =
        std::max<qint64>(1, static_cast<qint64>(sources.front().width()) *
                                sources.front().height() * 4);
    const size_t maxFrames = static_cast<size_t>(std::clamp<qint64>(
        maxBytes / frameBytes / 2, static_cast<qint64>(nativeCount) + 2,
        static_cast<qint64>(MAX_COMPOSED_FRAMES)));

    // (native frame, pose) -> image; the pose index `poseCount` is the rest
    // pose
    std::map<std::pair<size_t, size_t>, QImage> cache;
    const auto render = [&](size_t ni, size_t pi) -> QImage {
        const auto key = std::make_pair(ni, pi);
        if (const auto it = cache.find(key); it != cache.end())
        {
            return it->second;
        }
        const auto &pose = pi == poseCount ? spec.restPose : spec.poses[pi];
        auto image = applySpritePose(sources[ni], pose);
        cache.emplace(key, image);
        return image;
    };

    std::vector<std::pair<size_t, size_t>> keys;
    const auto push = [&](size_t ni, size_t pi, qint64 duration) {
        const auto key = std::make_pair(ni, pi);
        const bool tooShort = duration < MIN_FRAME_DURATION_MS;
        if (!out.frames.empty() && (keys.back() == key || tooShort))
        {
            // the same moment again, or too short to be seen: the frame
            // before it takes the time
            out.frames.back().durationMs += static_cast<int>(duration);
            return;
        }
        keys.push_back(key);
        out.frames.push_back(SpriteFrame{
            .image = render(ni, pi),
            .durationMs = static_cast<int>(duration),
        });
    };

    const bool endless = spec.loopCount <= 0;
    qint64 phaseLength = 0;
    if (endless)
    {
        phaseLength = std::min<qint64>(std::lcm(poseTotal, nativeTotal),
                                       MAX_PHASE_MS);
    }
    else
    {
        phaseLength = std::min<qint64>(
            poseTotal * std::min(spec.loopCount, MAX_POSE_PLAYS),
            MAX_PHASE_MS * 2);
    }

    size_t pi = 0;
    size_t ni = 0;
    qint64 poseLeft = poseDuration(0);
    qint64 nativeLeft = nativeDuration(0);
    qint64 t = 0;
    while (t < phaseLength && out.frames.size() < maxFrames)
    {
        const qint64 step =
            std::min({poseLeft, nativeLeft, phaseLength - t});
        push(ni, pi, step);
        t += step;
        poseLeft -= step;
        nativeLeft -= step;
        if (poseLeft == 0)
        {
            pi = (pi + 1) % poseCount;
            poseLeft = poseDuration(pi);
        }
        if (nativeLeft == 0)
        {
            ni = (ni + 1) % nativeCount;
            nativeLeft = nativeDuration(ni);
        }
    }

    if (endless)
    {
        out.loopFrom = 0;
        return out;
    }

    // the poses are done: the rest pose, and the image keeps looping
    out.loopFrom = static_cast<int>(out.frames.size());
    for (size_t i = 0; i < nativeCount; i++)
    {
        keys.emplace_back(i, poseCount);
        out.frames.push_back(SpriteFrame{
            .image = render(i, poseCount),
            .durationMs = static_cast<int>(nativeDuration(i)),
        });
    }
    return out;
}

}  // namespace chatterino
