#pragma once

#include <QImage>
#include <QPoint>
#include <QSize>
#include <QString>

#include <vector>

namespace chatterino {

/// One function of a CSS `filter` chain. Only the functions that are plain
/// colour transforms are implemented (the ones of the Filter Effects spec
/// that don't need neighbouring pixels): `drop-shadow()` and `blur()` have no
/// equivalent here and are left out by whoever builds the chain.
struct SpriteColorFilter {
    enum class Kind {
        Brightness,
        Contrast,
        Saturate,
        HueRotate,
        Grayscale,
        Sepia,
        Invert,
        Opacity,
    };

    Kind kind = Kind::Brightness;

    /// The CSS argument: a factor (1 = unchanged for brightness, contrast,
    /// saturate and opacity; 0 = unchanged for grayscale, sepia and invert)
    /// or, for hue-rotate, an angle in degrees.
    double amount = 1.0;

    bool operator==(const SpriteColorFilter &) const = default;
};

/// Describes which parts of a (sprite) sheet are shown, in which order and
/// for how long. This is what a CSS rule like
///
///     width: 56px; background-position: 0; animation: x 3.5s steps(99)
///
/// boils down to once the stylesheet has been evaluated.
struct SpriteSheetSpec {
    /// The size of one frame in image pixels.
    QSize frameSize;

    /// The top-left corner of each frame in the sheet, in play order. A frame
    /// may reach outside of the sheet; the part that does is transparent
    /// (that is what a CSS background does, too).
    std::vector<QPoint> offsets;

    /// How long each frame is shown, in milliseconds. One entry per entry of
    /// `offsets`. Unused if there is only a single frame.
    std::vector<int> durationsMs;

    /// Applied to every frame, in order.
    std::vector<SpriteColorFilter> filters;

    bool isValid() const;
    bool animated() const
    {
        return this->offsets.size() > 1;
    }

    /// A short string that is equal for equal specs, and only for those. Used
    /// to share one `Image` between everything that wants the same frames.
    QString cacheKey() const;

    bool operator==(const SpriteSheetSpec &) const = default;
};

struct SpriteFrame {
    QImage image;
    int durationMs = 0;
};

/// Cuts the frames of `spec` out of `sheet` and applies its filters.
///
/// Returns nothing if the spec is invalid or the sheet is null. If all the
/// frames together would need more than `maxBytes` of memory, only the first
/// frame is returned (a static image is better than none).
///
/// Frames are returned as `QImage::Format_ARGB32`. Every frame takes at least
/// 20 ms: that is the resolution of the animation clock, and anything faster
/// would just be dropped frames.
std::vector<SpriteFrame> sliceSpriteSheet(const QImage &sheet,
                                          const SpriteSheetSpec &spec,
                                          qint64 maxBytes);

/// Applies a filter chain to an image (any format; the result is ARGB32).
QImage applySpriteFilters(const QImage &image,
                          const std::vector<SpriteColorFilter> &filters);

}  // namespace chatterino
