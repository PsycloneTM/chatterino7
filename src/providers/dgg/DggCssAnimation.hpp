#pragma once

#include <array>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

/// A small CSS animation engine for DGG's `emotes.css`.
///
/// Qt can't run CSS animations, and chat-gui gets them for free from the
/// browser. What Chatterino can do is *bake* them: evaluate the keyframes at
/// a series of points in time and turn every point into one frame of an
/// ordinary multi-frame image. That is all this file does. It has no Qt
/// dependency (only `std`), so it can be tested on its own; the code that
/// draws the frames lives in `messages/SpriteSheet.cpp`.
///
/// Supported, as in the CSS specs:
///  - properties: `transform` (+ the individual `translate`, `rotate`,
///    `scale`), `transform-origin`, `opacity`, `filter` (colour functions),
///    `clip-path: polygon()`;
///  - `@keyframes` with any number of stops, per-stop
///    `animation-timing-function`, implicit 0% / 100% stops;
///  - timing functions: `linear`, `ease*`, `cubic-bezier()`, `steps()`;
///  - several animations on one element (a later one wins), `delay`,
///    iteration counts, `direction`, `fill-mode`.
///
/// Not supported (the emote is then left alone, and the reason is reported):
/// anything that moves other layout (`left`, `top`, `margin`, `width`, ...),
/// `mask-image`, `box-shadow`, `drop-shadow()`/`blur()` filters, 3D
/// transforms, `background-position` (that's what the sprite code is for).
namespace chatterino::dggcss::anim {

// ---------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------

/// A length in px or % (of the emote's box).
struct Length {
    double value = 0;
    bool percent = false;

    bool operator==(const Length &) const = default;
};

/// A 2D affine transform, as CSS `matrix(a, b, c, d, e, f)`:
///
///     x' = a x + c y + e
///     y' = b x + d y + f
struct Matrix {
    double a = 1;
    double b = 0;
    double c = 0;
    double d = 1;
    double e = 0;
    double f = 0;

    static Matrix translate(double x, double y);
    static Matrix rotate(double degrees);
    static Matrix scale(double x, double y);
    static Matrix skew(double xDegrees, double yDegrees);

    bool operator==(const Matrix &) const = default;
};

/// `l * r` applies `r` first, then `l` (the product of the CSS functions in
/// `transform: l r`).
Matrix operator*(const Matrix &l, const Matrix &r);

struct TransformFn {
    enum class Kind {
        Translate,  // x, y
        Rotate,     // n[0] degrees
        Scale,      // n[0], n[1]
        Skew,       // n[0], n[1] degrees
        Matrix,     // n[0..5]
    };

    Kind kind = Kind::Translate;
    Length x;
    Length y;
    std::array<double, 6> n{};

    bool operator==(const TransformFn &) const = default;
};

/// An empty list is `none`.
using TransformList = std::vector<TransformFn>;

/// Same numbering as `SpriteColorFilter::Kind` (checked where it's used).
enum class FilterKind {
    Brightness = 0,
    Contrast,
    Saturate,
    HueRotate,
    Grayscale,
    Sepia,
    Invert,
    Opacity,
};

struct FilterFn {
    FilterKind kind = FilterKind::Brightness;
    /// A factor, or degrees for `hue-rotate`.
    double amount = 1;

    bool operator==(const FilterFn &) const = default;
};

/// An empty list is `none`.
using FilterList = std::vector<FilterFn>;

struct Origin {
    Length x{50, true};
    Length y{50, true};

    bool operator==(const Origin &) const = default;
};

/// `clip-path`: `none`, or a polygon.
struct Clip {
    bool none = true;
    std::vector<std::pair<Length, Length>> points;

    bool operator==(const Clip &) const = default;
};

/// A length that can also be `auto`: box offsets (`left`, `top`, ...), sizes
/// and `line-height` (where `auto` stands for `normal`). A unitless
/// `line-height` is kept as a percentage of the font size.
struct LenOrAuto {
    bool isAuto = true;
    Length len;

    bool operator==(const LenOrAuto &) const = default;
};

/// `content`. `none` and `normal` make no box at all.
struct Content {
    bool none = true;
    std::string text;

    bool operator==(const Content &) const = default;
};

/// One `box-shadow`.
struct Shadow {
    bool inset = false;
    double x = 0;
    double y = 0;
    double blur = 0;
    double spread = 0;
    /// r, g, b in 0..255, alpha in 0..1
    std::array<double, 4> rgba{0, 0, 0, 1};

    bool operator==(const Shadow &) const = default;
};

using ShadowList = std::vector<Shadow>;

enum class Prop {
    Transform,
    Translate,
    Rotate,
    Scale,
    Opacity,
    Filter,
    Origin,
    Clip,
    // Properties of a `::before` / `::after` box (see bakeLayers()). They
    // are not used for the emote's own animations.
    Left,
    Right,
    Top,
    Bottom,
    Width,
    Height,
    FontSize,
    LineHeight,
    LetterSpacing,
    ContentText,
    BoxShadow,
};

using Value = std::variant<TransformList, double, FilterList, Origin, Clip,
                           LenOrAuto, Content, ShadowList>;

/// What the emote looks like at one moment, apart from its image.
struct Computed {
    TransformList transform;
    TransformList translate;
    TransformList rotate;
    TransformList scale;
    double opacity = 1;
    FilterList filter;
    Origin origin;
    Clip clip;

    // `::before` / `::after` boxes only
    LenOrAuto left;
    LenOrAuto right;
    LenOrAuto top;
    LenOrAuto bottom;
    LenOrAuto width;
    LenOrAuto height;
    double fontSize = 16;
    /// `auto` is `normal`
    LenOrAuto lineHeight;
    double letterSpacing = 0;
    Content content;
    ShadowList boxShadow;

    bool operator==(const Computed &) const = default;
};

/// The size of the element the percentages refer to.
struct Box {
    double width = 0;
    double height = 0;
};

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

struct TimingFunction {
    enum class Kind {
        Bezier,
        Steps,
    };
    enum class Jump {
        End,
        Start,
        None,
        Both,
    };

    Kind kind = Kind::Bezier;
    /// `ease`
    double x1 = 0.25;
    double y1 = 0.1;
    double x2 = 0.25;
    double y2 = 1.0;
    int steps = 1;
    Jump jump = Jump::End;

    bool operator==(const TimingFunction &) const = default;
};

/// `linear`, `ease`, `ease-in`, `ease-out`, `ease-in-out`, `step-start`,
/// `step-end`, `cubic-bezier(...)`, `steps(...)`.
std::optional<TimingFunction> parseTimingFunction(std::string_view text);

/// The output progress for an input progress in 0..1.
double applyTiming(const TimingFunction &timing, double progress);

// ---------------------------------------------------------------------------
// Animations
// ---------------------------------------------------------------------------

struct AnimationSpec {
    enum class Direction {
        Normal,
        Reverse,
        Alternate,
        AlternateReverse,
    };
    enum class Fill {
        None,
        Forwards,
        Backwards,
        Both,
    };

    std::string name;
    double durationMs = 0;
    double delayMs = 0;
    /// `infinity` for `infinite`.
    double iterations = 1;
    Direction direction = Direction::Normal;
    Fill fill = Fill::None;
    TimingFunction timing;  // `ease`

    bool infinite() const
    {
        return this->iterations == std::numeric_limits<double>::infinity();
    }
};

/// The value of an `animation` shorthand: a comma separated list. Returns
/// nothing if a part of it can't be understood.
std::optional<std::vector<AnimationSpec>> parseAnimationList(
    std::string_view value);

/// One `name: value` pair of a keyframe.
struct KeyframeDecl {
    std::string name;
    std::string value;
};

/// One stop of an `@keyframes` rule. `offset` is 0..1.
struct Keyframe {
    double offset = 0;
    std::vector<KeyframeDecl> decls;
};

/// Reads / writes one property of a state.
Value getProp(const Computed &state, Prop prop);
void setProp(Computed &state, Prop prop, Value value);

/// A CSS colour (`#rgb`, `#rrggbb`, `rgb()`, `rgba()`, `black`, `white`,
/// `transparent`) as r, g, b in 0..255 and alpha in 0..1.
std::optional<std::array<double, 4>> parseColorValue(std::string_view text);

/// Parses one declaration. Returns nothing if the property isn't one of the
/// supported ones or its value isn't understood.
std::optional<std::pair<Prop, Value>> parseDeclaration(
    std::string_view name, std::string_view value);

/// An animation with its keyframes digested.
class Animation
{
public:
    /// Why the animation can't be used, if it can't: a property that isn't
    /// supported, ...
    static std::optional<Animation> build(const AnimationSpec &spec,
                                          const std::vector<Keyframe> &keyframes,
                                          std::string &problem);

    const AnimationSpec &spec() const
    {
        return this->spec_;
    }

    /// Whether the animation has a say about `prop`.
    bool animates(Prop prop) const;

    /// Whether the animation controls what the element looks like at `t`
    /// ms after the start of the whole timeline (its delay included), and if
    /// so, with what progress (0..1, direction applied).
    std::optional<double> progressAt(double timeMs) const;

    /// The value of `prop` for a progress from progressAt(). `base` is where
    /// the animation has no stop of its own.
    Value valueAt(Prop prop, double progress, const Computed &base,
                  const Box &box) const;

private:
    struct Stop {
        double offset = 0;
        Value value;
        std::optional<TimingFunction> timing;
    };

    AnimationSpec spec_;
    std::vector<std::pair<Prop, std::vector<Stop>>> tracks_;
};

/// The state at `timeMs`: `base` with every animation in `animations` laid
/// on top, in order (a later one wins).
Computed computeAt(const std::vector<Animation> &animations,
                   const Computed &base, double timeMs, const Box &box);

// ---------------------------------------------------------------------------
// Baking
// ---------------------------------------------------------------------------

struct BakedFrame {
    Computed state;
    int durationMs = 0;
};

struct BakeOptions {
    /// How finely time is cut up. 40 ms (25 fps) is plenty for CSS motion
    /// and keeps the memory of a message's frames small.
    double stepMs = 40;
    /// The longest stretch of time that is baked. What would come after it
    /// is dropped (and `Baked::truncated` set).
    double maxMs = 10000;
};

struct Baked {
    std::vector<BakedFrame> frames;
    /// What the emote shows after the last play. Unused if `plays` is 0.
    Computed rest;
    /// How often `frames` are played: 0 forever, otherwise a count. Then
    /// `rest` is shown for good.
    int plays = 1;
    bool truncated = false;
};

/// Bakes the animations of an element. Returns nothing (with `problem` set)
/// if they can't be baked: nothing in them moves, an infinite animation
/// that doesn't repeat on its own, ...
std::optional<Baked> bake(const std::vector<Animation> &animations,
                          const Computed &base, const Box &box,
                          const BakeOptions &options, std::string &problem);

/// One element (a `::before` or `::after` box) with its animations.
struct LayerInput {
    std::vector<Animation> animations;
    Computed base;
};

struct BakedLayerFrame {
    /// One state per layer, in the order of the input.
    std::vector<Computed> layers;
    int durationMs = 0;
};

struct BakedLayers {
    std::vector<BakedLayerFrame> frames;
    /// What every layer looks like once all the animations are over.
    std::vector<Computed> rest;
    bool truncated = false;
};

/// Bakes several elements that run on one clock (the `::before` and the
/// `::after` of an emote). Every animation must have a finite end; the
/// baked part ends when the last of them does (a negative delay counts).
/// `box` is the containing block that percentages refer to. Returns nothing
/// (with `problem` set) if nothing moves or an animation never ends.
std::optional<BakedLayers> bakeLayers(const std::vector<LayerInput> &layers,
                                      const Box &box,
                                      const BakeOptions &options,
                                      std::string &problem);

/// The same without the transform origin: what `matrix(a, b, c, d, e, f)`
/// would be if the origin were the top-left corner.
Matrix matrixWithoutOrigin(const Computed &state, const Box &box);

/// The matrix of a state for an element of size `box`, with the transform
/// origin folded in: it maps a point of the element (0,0 is its top-left
/// corner) to where it ends up.
Matrix matrixOf(const Computed &state, const Box &box);

/// The origin in px.
std::pair<double, double> originPx(const Origin &origin, const Box &box);

/// A clip polygon in px; empty if there is none.
std::vector<std::pair<double, double>> clipPx(const Clip &clip,
                                              const Box &box);

}  // namespace chatterino::dggcss::anim
