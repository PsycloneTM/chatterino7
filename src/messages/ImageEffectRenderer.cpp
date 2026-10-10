// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "messages/ImageEffectRenderer.hpp"

#include <QImage>
#include <QPixmap>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace {

using namespace chatterino::detail;

/// `c!` and `ffzCursed` are the same effect in BTTV and FFZ:
/// CSS `grayscale(1) brightness(0.7) contrast(2.5)`
/// (BetterTTV: src/modules/chat/style.css, FrankerFaceZ: modules/chat/emotes.js)
constexpr float CURSED_BRIGHTNESS = 0.7F;
constexpr float CURSED_CONTRAST = 2.5F;

/// `p!` is BTTV's CSS animation `sepia(0.5) hue-rotate(0..360deg)
/// saturate(2.5)` running linearly over 1.5s.
constexpr float PARTY_SEPIA = 0.5F;
constexpr float PARTY_SATURATE = 2.5F;

/// Length of the pre-rendered hue cycle of a still image (`p!`).
/// 25 * 60ms = 1.5s, the cycle length of BTTV. 60ms is a multiple of the GIF
/// timer's 20ms step.
constexpr int PARTY_STILL_FRAME_COUNT = 25;
constexpr int PARTY_STILL_FRAME_DURATION_MS = 60;

/// An animated image is repeated until it's at least this long (BTTV's cycle
/// length), so the hue doesn't cycle faster than it's pleasant for short
/// animations.
constexpr int PARTY_MIN_CYCLE_MS = 1500;
constexpr int PARTY_MAX_REPEATS = 8;
/// Upper bound for the memory the frames of a party image may take
constexpr qint64 PARTY_MAX_BYTES = chatterino::Image::maxBytesRam / 2;

/// A row-major 3x3 matrix that's applied to (r, g, b)
using ColorMatrix = std::array<float, 9>;

/// The matrix of CSS's `hue-rotate(<degrees>)` filter
/// (https://drafts.fxtf.org/filter-effects/#feColorMatrixElement)
ColorMatrix hueRotateMatrix(double degrees)
{
    const double rad = degrees * std::numbers::pi / 180.0;
    const auto cs = static_cast<float>(std::cos(rad));
    const auto sn = static_cast<float>(std::sin(rad));

    return {
        0.213F + (cs * 0.787F) - (sn * 0.213F),
        0.715F - (cs * 0.715F) - (sn * 0.715F),
        0.072F - (cs * 0.072F) + (sn * 0.928F),
        0.213F - (cs * 0.213F) + (sn * 0.143F),
        0.715F + (cs * 0.285F) + (sn * 0.140F),
        0.072F - (cs * 0.072F) - (sn * 0.283F),
        0.213F - (cs * 0.213F) - (sn * 0.787F),
        0.715F - (cs * 0.715F) + (sn * 0.715F),
        0.072F + (cs * 0.928F) + (sn * 0.072F),
    };
}

/// The matrix of CSS's `sepia(<amount>)` filter
ColorMatrix sepiaMatrix(float amount)
{
    const float k = 1.0F - amount;
    return {
        0.393F + (0.607F * k), 0.769F - (0.769F * k), 0.189F - (0.189F * k),
        0.349F - (0.349F * k), 0.686F + (0.314F * k), 0.168F - (0.168F * k),
        0.272F - (0.272F * k), 0.534F - (0.534F * k), 0.131F + (0.869F * k),
    };
}

/// The matrix of CSS's `saturate(<amount>)` filter
ColorMatrix saturateMatrix(float s)
{
    return {
        0.213F + (0.787F * s), 0.715F - (0.715F * s), 0.072F - (0.072F * s),
        0.213F - (0.213F * s), 0.715F + (0.285F * s), 0.072F - (0.072F * s),
        0.213F - (0.213F * s), 0.715F - (0.715F * s), 0.072F + (0.928F * s),
    };
}

/// Every CSS filter function clamps its result to [0, 255] (here), so
/// chained matrices can't be multiplied together beforehand.
std::array<float, 3> applyMatrix(const ColorMatrix &m,
                                 const std::array<float, 3> &c)
{
    const auto clamp = [](float v) {
        return std::clamp(v, 0.0F, 255.0F);
    };
    return {
        clamp((m[0] * c[0]) + (m[1] * c[1]) + (m[2] * c[2])),
        clamp((m[3] * c[0]) + (m[4] * c[1]) + (m[5] * c[2])),
        clamp((m[6] * c[0]) + (m[7] * c[1]) + (m[8] * c[2])),
    };
}

int clampChannel(float value)
{
    return static_cast<int>(std::clamp(value + 0.5F, 0.0F, 255.0F));
}

/// The frame as a non-premultiplied ARGB image. Color math is only correct on
/// non-premultiplied pixels.
QImage toStraightImage(const QPixmap &pixmap)
{
    return pixmap.toImage().convertToFormat(QImage::Format_ARGB32);
}

/// Runs `transform(r, g, b)` (returning `std::array<float, 3>`) on every pixel
/// of `source`. The alpha channel is kept as is.
template <typename Transform>
QPixmap mapPixels(const QImage &source, Transform &&transform)
{
    QImage out(source.size(), QImage::Format_ARGB32);

    for (int y = 0; y < source.height(); y++)
    {
        const auto *in =
            reinterpret_cast<const QRgb *>(source.constScanLine(y));
        auto *dst = reinterpret_cast<QRgb *>(out.scanLine(y));

        for (int x = 0; x < source.width(); x++)
        {
            const QRgb px = in[x];
            const auto rgb = transform(static_cast<float>(qRed(px)),
                                       static_cast<float>(qGreen(px)),
                                       static_cast<float>(qBlue(px)));
            dst[x] = qRgba(clampChannel(rgb[0]), clampChannel(rgb[1]),
                           clampChannel(rgb[2]), qAlpha(px));
        }
    }

    return QPixmap::fromImage(std::move(out));
}

QPixmap renderCursed(const QImage &source)
{
    // CSS `grayscale(1) brightness(0.7) contrast(2.5)`
    return mapPixels(source, [](float r, float g, float b) {
        const float luma = (0.2126F * r) + (0.7152F * g) + (0.0722F * b);
        const float bright = luma * CURSED_BRIGHTNESS;
        const float v = ((bright - 127.5F) * CURSED_CONTRAST) + 127.5F;
        return std::array<float, 3>{v, v, v};
    });
}

QPixmap renderPartyFrame(const QImage &source, double degrees)
{
    // CSS `sepia(0.5) hue-rotate(<degrees>) saturate(2.5)`
    const auto sepia = sepiaMatrix(PARTY_SEPIA);
    const auto hue = hueRotateMatrix(degrees);
    const auto saturate = saturateMatrix(PARTY_SATURATE);
    return mapPixels(source, [&](float r, float g, float b) {
        auto c = applyMatrix(sepia, {r, g, b});
        c = applyMatrix(hue, c);
        return applyMatrix(saturate, c);
    });
}

QList<Frame> renderCursedFrames(const QList<Frame> &source)
{
    QList<Frame> out;
    out.reserve(source.size());

    for (const auto &frame : source)
    {
        auto image = toStraightImage(frame.image);
        if (image.isNull())
        {
            out.append(frame);
            continue;
        }
        out.append(
            Frame{.image = renderCursed(image), .duration = frame.duration});
    }

    return out;
}

QList<Frame> renderPartyFrames(const QList<Frame> &source)
{
    QList<QImage> images;
    images.reserve(source.size());
    for (const auto &frame : source)
    {
        images.append(toStraightImage(frame.image));
        if (images.last().isNull())
        {
            return source;
        }
    }

    QList<Frame> out;

    if (source.size() == 1)
    {
        // Still image: pre-render one full hue cycle
        out.reserve(PARTY_STILL_FRAME_COUNT);
        for (int i = 0; i < PARTY_STILL_FRAME_COUNT; i++)
        {
            out.append(Frame{
                .image = renderPartyFrame(images.first(),
                                          360.0 * i / PARTY_STILL_FRAME_COUNT),
                .duration = PARTY_STILL_FRAME_DURATION_MS,
            });
        }
        return out;
    }

    qint64 loopMs = 0;
    for (const auto &frame : source)
    {
        loopMs += frame.duration;
    }
    loopMs = std::max<qint64>(loopMs, 1);

    const qint64 bytesPerLoop =
        static_cast<qint64>(images.first().sizeInBytes()) * source.size();

    qint64 repeats = std::clamp<qint64>(
        (PARTY_MIN_CYCLE_MS + loopMs - 1) / loopMs, 1, PARTY_MAX_REPEATS);
    while (repeats > 1 && bytesPerLoop * repeats > PARTY_MAX_BYTES)
    {
        repeats--;
    }

    // One full hue cycle over all repeats of the loop, so the animation
    // loops without a jump.
    const double cycleMs = static_cast<double>(loopMs * repeats);
    double elapsedMs = 0;

    out.reserve(static_cast<qsizetype>(source.size() * repeats));
    for (qint64 repeat = 0; repeat < repeats; repeat++)
    {
        for (qsizetype i = 0; i < source.size(); i++)
        {
            const double hue = 360.0 * elapsedMs / cycleMs;
            out.append(Frame{
                .image = renderPartyFrame(images[i], hue),
                .duration = source[i].duration,
            });
            elapsedMs += source[i].duration;
        }
    }

    return out;
}

}  // namespace

namespace chatterino::detail {

QList<Frame> renderImageEffect(const QList<Frame> &source, ImageEffect effect)
{
    if (source.isEmpty())
    {
        return source;
    }

    switch (effect)
    {
        case ImageEffect::Cursed:
            return renderCursedFrames(source);
        case ImageEffect::Party:
            return renderPartyFrames(source);
        case ImageEffect::None:
            break;
    }

    return source;
}

}  // namespace chatterino::detail
