#pragma once

#include "messages/Emote.hpp"
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

/// The destiny.gg emotes and flairs, loaded from the CDN manifests.
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

    /// Downloads both manifests. Does nothing if a load is already running.
    /// A failed download keeps what was loaded before.
    void load();

    bool isReady() const
    {
        return this->ready_;
    }

    /// The emote for `word`, if there is one and `user` may use it.
    /// Matching is case-sensitive and on whole words only, like chat-gui.
    EmotePtr emoteFor(QStringView word, const DggUser &user) const;

    /// Badge images for the user's flairs, in the order of the user's
    /// `features` (that's the order chat-gui draws them in). Flairs without
    /// an image are skipped.
    std::vector<EmotePtr> badgesFor(const DggUser &user) const;

    /// The nick colour from the user's flairs. Null if no flair has a colour,
    /// or if the winning flair is rainbow-only (chat-gui animates that one,
    /// which can't be drawn here, so the default colour is used).
    std::optional<QColor> nickColorFor(const DggUser &user) const;

private:
    struct StringViewHash : std::hash<QStringView> {
        using is_transparent = std::true_type;
    };

    template <typename T>
    using StringMap =
        boost::unordered_flat_map<QString, T, StringViewHash, std::equal_to<>>;

    struct EmoteEntry {
        DggEmoteDef def;
        EmotePtr emote;
    };

    void fetch(const QString &url,
               std::function<void(const QJsonArray &)> onData);
    void onFetchFinished();
    void applyEmotes(const QJsonArray &json);
    void applyFlairs(const QJsonArray &json);

    StringMap<EmoteEntry> emotes_;
    std::vector<DggFlairDef> flairs_;
    StringMap<EmotePtr> badges_;

    bool loading_ = false;
    bool ready_ = false;
    int pending_ = 0;

    /// Lets in-flight requests notice that this object is gone.
    std::shared_ptr<bool> alive_;
};

}  // namespace chatterino
