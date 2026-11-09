#pragma once

#include <QImage>
#include <QPoint>
#include <QtGlobal>
#include <QSize>
#include <QString>

#include <array>
#include <optional>
#include <utility>
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

/// How one frame is drawn on top of what the sheet shows: what a CSS
/// animation of `transform`, `opacity`, `filter` and `clip-path` makes of the
/// emote at one moment (see providers/dgg/DggCssAnimation.hpp, which "bakes"
/// such animations into a list of these).
///
/// Applied in the order of the CSS rendering model: filters, clip, opacity,
/// transform. The frame is not made bigger, so what a transform moves out of
/// the frame's box is cut off (chat-gui lets it overflow into its
/// neighbours).
struct SpritePose {
    /// CSS `matrix(a, b, c, d, e, f)` in frame pixels, with the transform
    /// origin already folded in: x' = a x + c y + e, y' = b x + d y + f.
    std::array<double, 6> matrix{1, 0, 0, 1, 0, 0};

    double opacity = 1.0;

    /// The colour filters of this moment (the element's own plus whatever
    /// the animation does to them).
    std::vector<SpriteColorFilter> filters;

    /// `clip-path: polygon()` in frame pixels. Empty if there is none.
    std::vector<std::pair<double, double>> clip;

    /// Nothing but (maybe) filters: no transform, opacity or clip.
    bool isIdentity() const;

    bool operator==(const SpritePose &) const = default;
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

    /// How often the frames are played. 0 means forever. For a number above
    /// 0 the animation plays that many times and then stays on one frame
    /// (`restOffset`): the first frame, or the last one if the CSS has
    /// `animation-fill-mode: forwards`.
    int loopCount = 0;

    /// Where the frame the animation ends on is. Only used if `loopCount`
    /// is above 0.
    QPoint restOffset;

    /// Optional, for emotes whose CSS animates `transform`, `opacity`, `filter`
    /// or `clip-path`: one pose per entry of `offsets` (so, as many as the
    /// frames), drawn on top of that frame. Empty for the ordinary case. If
    /// set, it replaces `filters` (the poses carry the element's own filters
    /// as well).
    std::vector<SpritePose> poses;

    /// What the animation rests on after `loopCount` plays, like
    /// `restOffset` for the sheet. Only used if `poses` is set.
    SpritePose restPose;

    /// Only used to keep specs apart that are otherwise equal. An animation
    /// that plays once starts when its image is loaded, so every message
    /// that should start it again needs an image of its own.
    quint64 instanceId = 0;

    /// Plays a limited number of times and then stops.
    bool playsOnce() const
    {
        return this->loopCount > 0 && this->offsets.size() > 1;
    }

    bool hasPoses() const
    {
        return !this->poses.empty();
    }

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
/// For a spec that `playsOnce()`, one more frame is returned after the frames
/// of `offsets`: the one the animation rests on (duration 0). It is missing if
/// only the first frame was returned because of `maxBytes`.
///
/// Frames are returned as `QImage::Format_ARGB32`. Every frame takes at least
/// 20 ms: that is the resolution of the animation clock, and anything faster
/// would just be dropped frames.
std::vector<SpriteFrame> sliceSpriteSheet(const QImage &sheet,
                                          const SpriteSheetSpec &spec,
                                          qint64 maxBytes);

/// Draws `pose` on `frame`: filters, clip, opacity, transform. The result
/// has the size of `frame` and is ARGB32.
QImage applySpritePose(const QImage &frame, const SpritePose &pose);

/// Applies a filter chain to an image (any format; the result is ARGB32).
QImage applySpriteFilters(const QImage &image,
                          const std::vector<SpriteColorFilter> &filters);

}  // namespace chatterino
