#include "messages/SpriteSheet.hpp"

#include <QByteArray>
#include <QCryptographicHash>
#include <QRect>

#include <algorithm>
#include <array>
#include <cmath>

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

}  // namespace

namespace chatterino {

bool SpriteSheetSpec::isValid() const
{
    return this->frameSize.width() > 0 && this->frameSize.height() > 0 &&
           !this->offsets.empty() &&
           this->durationsMs.size() == this->offsets.size();
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
    for (const auto &filter : this->filters)
    {
        blob.append(QByteArray::number(static_cast<int>(filter.kind)));
        blob.append(':');
        blob.append(QByteArray::number(filter.amount, 'g', 10));
        blob.append(';');
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
            }
            line[x] = qRgba(toByte(px[0]), toByte(px[1]), toByte(px[2]),
                            toByte(px[3]));
        }
    }
    return out;
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
    const qint64 frameBytes = static_cast<qint64>(spec.frameSize.width()) *
                              spec.frameSize.height() * 4;
    bool addRest = spec.playsOnce();
    if (frameBytes * static_cast<qint64>(count + (addRest ? 1 : 0)) > maxBytes)
    {
        count = 1;
        addRest = false;
    }

    out.reserve(count);
    for (size_t i = 0; i < count; i++)
    {
        QImage frame = source.copy(QRect(spec.offsets[i], spec.frameSize));
        if (!spec.filters.empty())
        {
            frame = applySpriteFilters(frame, spec.filters);
        }

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
        QImage rest = source.copy(QRect(spec.restOffset, spec.frameSize));
        if (!spec.filters.empty())
        {
            rest = applySpriteFilters(rest, spec.filters);
        }
        out.push_back(SpriteFrame{.image = std::move(rest), .durationMs = 0});
    }

    return out;
}

}  // namespace chatterino
