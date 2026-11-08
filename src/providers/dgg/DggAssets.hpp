#pragma once

#include "messages/Emote.hpp"
#include "providers/dgg/DggEmoteCss.hpp"
#include "providers/dgg/DggManifest.hpp"

#include <boost/unordered/unordered_flat_map.hpp>
#include <pajlada/signals/signal.hpp>
#include <QColor>
#include <QStringView>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace chatterino {

struct DggUser;

/// The destiny.gg emotes and flairs, loaded from the CDN manifests and the
/// emote stylesheet.
///
/// The manifest says which image an emote has, but not how it is shown: many
/// of the images are sprite sheets, and the stylesheet (`emotes.css`) is what
/// cuts the right part out for the right user. Without the stylesheet (it
/// failed to load) emotes are shown as their whole image.
///
/// Everything here must be used from the GUI thread.
class DggAssets
{
public:
    DggAssets();
    ~DggAssets();

    DggAssets(const DggAssets &) = delete;
    DggAssets &operator=(const DggAssets &) = delete;
    DggAssets(DggAssets &&) = delete;
    DggAssets &operator=(DggAssets &&) = delete;

    /// Fired once, when the first load has finished (whether or not the
    /// downloads worked). Messages that arrive before this would be built
    /// without emotes, so the server holds them back until then.
    pajlada::Signals::NoArgSignal ready;

    /// Downloads both manifests and the stylesheet. Does nothing if a load is
    /// already running.
    /// A failed download keeps what was loaded before.
    void load();

    bool isReady() const
    {
        return this->ready_;
    }

    /// The emote for `word`, if there is one and `user` may use it.
    /// Matching is case-sensitive and on whole words only, like chat-gui.
    EmotePtr emoteFor(QStringView word, const DggUser &user) const;

    /// Like above, but the emote looks the way the stylesheet makes it look
    /// in a message in `context` (which part of its image, what size, which
    /// colours: it depends on who wrote the message).
    EmotePtr emoteFor(QStringView word, const DggUser &user,
                      const DggEmoteContext &context) const;

    /// What chat-gui's page around the emotes of a message looks like, as far
    /// as the stylesheet can tell. `isAction` is a `/me` line, `backlog` one
    /// from the history replay.
    DggEmoteContext emoteContextFor(const DggUser &user, bool isAction,
                                    bool backlog) const;

    /// Badge images for the user's flairs, in the order of the user's
    /// `features` (that's the order chat-gui draws them in). Flairs without
    /// an image are skipped.
    std::vector<EmotePtr> badgesFor(const DggUser &user) const;

    /// How the nick is drawn, from the user's flairs.
    struct NickStyle {
        /// Null if no flair has a colour (and none is rainbow). For a rainbow
        /// flair this is its plain colour, if it has one, used when paints
        /// are turned off.
        std::optional<QColor> color;

        /// The winning flair is rainbow-coloured.
        bool rainbow = false;
    };

    NickStyle nickStyleFor(const DggUser &user) const;

private:
    struct StringViewHash : std::hash<QStringView> {
        using is_transparent = std::true_type;
    };

    template <typename T>
    using StringMap =
        boost::unordered_flat_map<QString, T, StringViewHash, std::equal_to<>>;

    struct EmoteEntry {
        DggEmoteDef def;

        /// The emote in a context without a user (the same for everybody if
        /// the stylesheet doesn't have per-user rules for it).
        EmotePtr emote;

        /// The stylesheet has rules that depend on the user or the message.
        bool contextual = false;
    };

    void fetch(const QString &url,
               std::function<void(const QJsonArray &)> onData);
    void fetchText(const QString &url,
                   std::function<void(const QString &)> onData);
    void onFetchFinished();
    void applyEmotes(const QJsonArray &json);
    void applyFlairs(const QJsonArray &json);
    void applyCss(const QString &css);

    /// Rebuilds every emote from the manifest and the stylesheet.
    void rebuildEmotes();

    StringMap<EmoteEntry> emotes_;
    DggEmoteCss css_;

    /// Emotes in a context that gives them a different look from
    /// `EmoteEntry::emote`, by emote name and style.
    mutable StringMap<EmotePtr> styled_;
    std::vector<DggFlairDef> flairs_;
    StringMap<EmotePtr> badges_;

    bool loading_ = false;
    bool ready_ = false;
    int pending_ = 0;

    /// Lets in-flight requests notice that this object is gone.
    std::shared_ptr<bool> alive_;
};

}  // namespace chatterino
