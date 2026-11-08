#pragma once

#include "messages/SpriteSheet.hpp"

#include <QSize>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace chatterino {

/// What chat-gui's page looks like around an emote, as far as the stylesheet
/// can tell. chat-gui renders every message as
///
///     <div class="msg-chat msg-user {features} [msg-me] [msg-historical]"
///          data-username="{lower-case nick}">
///       ... <a class="user {nick colour flair}">nick</a> ...
///       <span class="text"> ... <div class="emote NAME"> ... </span>
///     </div>
///
/// and `emotes.css` selects on those classes and on `data-username`.
struct DggEmoteContext {
    /// `data-username`: the author's nick in lower case.
    QString username;

    /// The classes of the message element besides `msg-chat` and `msg-user`
    /// (which every chat line has): the author's `features` (`flair42`,
    /// `subscriber`, ...), plus `msg-me` for a `/me` line and
    /// `msg-historical` for one that came out of the backlog.
    QStringList messageClasses;

    /// The only flair class chat-gui puts on the nick: the one that colours
    /// it (`usernameColorFlair`). Empty if there is none. Note that this is
    /// *not* every flair the user has, which is what `messageClasses` holds.
    QString nickColorFlair;
};

/// The result of evaluating `emotes.css` for one emote in one context.
struct DggEmoteStyle {
    /// False if the stylesheet doesn't know the emote or doesn't give it a
    /// size. Nothing else in here means anything then, and the image should
    /// be shown as it is.
    bool valid = false;

    /// The size the emote is drawn at (CSS `width` and `height`). This is
    /// *not* the size of the image: for a sprite sheet it's one frame.
    QSize frameSize;

    /// Which parts of the image are shown, in which order, and how they are
    /// coloured. Valid (with at least one frame) if `valid`.
    SpriteSheetSpec sprite;

    /// Things in the stylesheet that apply to this emote but that are not
    /// reproduced (a `transform`, an animation that isn't `steps()`, ...).
    /// For logs and tests.
    QStringList notes;

    bool animated() const
    {
        return this->sprite.animated();
    }

    /// True if drawing the unmodified image gives the same picture: a single
    /// frame at the origin that is as big as the whole image, and no filters.
    bool isWholeImage(QSize sheetSize) const;

    /// Equal for styles that draw the same thing.
    QString cacheKey() const;
};

namespace dggcss {

/// A `name: value` pair. `name` is lower case unless it's a custom property.
struct Decl {
    std::string name;
    std::string value;
    bool important = false;
};

struct Attr {
    std::string name;
    /// Empty for `[name]`, otherwise `=`, `~=`, `^=`, ...
    std::string op;
    std::string value;
};

/// Like `div.a.b[c=d]:hover`, without the element name.
struct Compound {
    std::vector<std::string> classes;
    std::vector<Attr> attrs;
    std::vector<std::string> pseudos;
};

struct SelectorPart {
    /// The combinator between the previous part and this one: ` `, `>`, `~`
    /// or `+`. Meaningless (0) for the first part.
    char combinator = 0;
    Compound compound;
};

/// A style rule for one selector of a rule's selector list. Only selectors
/// of the supported shape get here (see DggEmoteCss::parse()).
struct Rule {
    std::vector<SelectorPart> parts;
    int specificity = 0;
    int order = 0;

    /// Whether the rule can match differently for different users.
    bool dependsOnContext = false;
    std::vector<Decl> decls;
};

struct KeyframeStop {
    double percent = 0;
    std::vector<Decl> decls;
};

}  // namespace dggcss

/// The part of DGG's `emotes.css` that decides what an emote looks like:
/// which part of its image is shown, how big, in which animation, for whom.
///
/// chat-gui draws every emote as a `<div class="emote NAME">` whose
/// `background-image` is the emote's PNG/GIF/AVIF. Many of those images are
/// *sprite sheets* (all animation frames, or several per-user variants,
/// side by side); the stylesheet cuts the right part out with `width`,
/// `height`, `background-position` and a `steps()` animation. The manifest's
/// image size is the size of the whole sheet and says nothing about the size
/// the emote is drawn at.
///
/// This class understands the subset of CSS that DGG uses for that:
///
///  - rules whose last selector part is `.emote.NAME`, optionally below
///    `.msg-chat`, `.msg-chat.msg-user[data-username="x"]`,
///    `[data-username="x"]`, `.msg-chat.msg-user.<class>` and
///    `.user.<flair> ~ .text` (the shapes DGG uses for per-user and per-tier
///    variants);
///  - `width`, `height`, `background-position` (lengths, percentages and
///    keywords), `animation` with `@keyframes` that move
///    `background-position` and a `steps()` timing function, `filter`
///    (colour functions only) and custom properties with `var()`;
///  - the cascade: specificity, source order and `!important`.
///
/// Everything else is skipped, namely `:hover` rules, pseudo-elements,
/// neighbour selectors (`+`, `:has()`, `:nth-child()`, ...), combo messages,
/// transforms, and every animation that isn't a sprite-sheet one.
///
/// Immutable after parse(), so it can be shared between threads.
class DggEmoteCss
{
public:
    struct Stats {
        /// Rules (selectors, really) that can be evaluated.
        size_t rules = 0;
        /// `:hover` rules. Understood, but nothing is drawn on hover.
        size_t hoverRules = 0;
        /// Rules with a selector that isn't supported.
        size_t unsupportedRules = 0;
        size_t keyframes = 0;
        /// Emotes with at least one rule.
        size_t emotes = 0;
    };

    /// An empty stylesheet: every emote resolves to an invalid style.
    DggEmoteCss() = default;

    /// Never fails: what isn't understood is skipped (and counted).
    static DggEmoteCss parse(const QString &css);

    bool isEmpty() const
    {
        return this->rules_.empty();
    }

    const Stats &stats() const
    {
        return this->stats_;
    }

    /// "reason: count" for every kind of selector that was skipped, most
    /// common first. For debug logs.
    QStringList describeUnsupported() const;

    bool hasEmote(const QString &emote) const;

    /// Whether some rule of the emote can give a different result for
    /// different users or messages. If not, resolve() gives the same style
    /// for every context, and it only needs to be evaluated once.
    bool dependsOnContext(const QString &emote) const;

    /// Evaluates the stylesheet for one emote. `sheetSize` is the size of
    /// the emote's image (from the manifest), which `background-position`
    /// keywords and percentages are relative to; pass an empty size if it's
    /// not known.
    DggEmoteStyle resolve(const QString &emote, const DggEmoteContext &context,
                          QSize sheetSize) const;

private:
    std::unordered_map<std::string, std::vector<dggcss::Rule>> rules_;
    std::unordered_map<std::string, std::vector<dggcss::KeyframeStop>>
        keyframes_;
    std::map<std::string, size_t> unsupported_;
    Stats stats_;
};

}  // namespace chatterino
