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

/// Contrast applied after turning the image to grayscale (`c!`, `ffzCursed`).
/// 1 is plain grayscale, higher values push towards pure black and white.
constexpr float CURSED_CONTRAST = 2.0F;

/// Length of the pre-rendered hue cycle of a still image (`p!`)
constexpr int PARTY_STILL_FRAME_COUNT = 20;
constexpr int PARTY_STILL_FRAME_DURATION_MS = 100;

/// An animated image is repeated until it's at least this long, so the hue
/// doesn't cycle faster than it's pleasant for short animations.
constexpr int PARTY_MIN_CYCLE_MS = 2000;
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
    // CSS `grayscale(1) contrast(CURSED_CONTRAST)`
    return mapPixels(source, [](float r, float g, float b) {
        const float luma = (0.2126F * r) + (0.7152F * g) + (0.0722F * b);
        const float v = ((luma - 127.5F) * CURSED_CONTRAST) + 127.5F;
        return std::array<float, 3>{v, v, v};
    });
}

QPixmap renderHueRotated(const QImage &source, double degrees)
{
    const auto m = hueRotateMatrix(degrees);
    return mapPixels(source, [&m](float r, float g, float b) {
        return std::array<float, 3>{
            (m[0] * r) + (m[1] * g) + (m[2] * b),
            (m[3] * r) + (m[4] * g) + (m[5] * b),
            (m[6] * r) + (m[7] * g) + (m[8] * b),
        };
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
                .image = renderHueRotated(images.first(),
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
                .image = renderHueRotated(images[i], hue),
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
