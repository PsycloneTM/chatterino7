#pragma once

#include <QJsonArray>
#include <QString>
#include <QStringList>

#include <vector>

namespace chatterino {

struct DggUser;

/// The first element of a manifest entry's `image` array. DGG only has one
/// resolution per image, so there are no 2x/3x variants.
struct DggImageRef {
    QString url;

    /// 0 if the manifest doesn't say.
    int width = 0;
    int height = 0;

    bool isValid() const
    {
        return !this->url.isEmpty();
    }
};

/// One entry of `https://cdn.destiny.gg/emotes/emotes.json`.
struct DggEmoteDef {
    /// The word that turns into the emote. Case-sensitive.
    QString prefix;

    /// Missing for some emotes.
    QString creator;

    /// 0 means everyone may use it.
    int minimumSubTier = 0;

    /// Only Patreon members (or privileged users) may use it. Not present in
    /// every entry; defaults to false.
    bool patreonOnly = false;

    DggImageRef image;
};

/// One entry of `https://cdn.destiny.gg/flairs/flairs.json`.
struct DggFlairDef {
    /// What appears in a user's `features` (`flair17`, `admin`, ...).
    QString name;
    QString label;

    /// Null in the manifest for most flairs.
    QString description;

    /// chat-gui doesn't read this when drawing messages, so neither do we.
    bool hidden = false;

    /// Lower number = more important.
    int priority = 0;

    /// `#RRGGBB`, empty if the flair has no colour.
    QString color;
    bool rainbowColor = false;

    DggImageRef image;

    bool colorsNick() const
    {
        return this->rainbowColor || !this->color.isEmpty();
    }
};

/// Parses the emote manifest. Entries without a `prefix` are skipped, as are
/// entries that aren't objects. The order of the manifest is kept.
std::vector<DggEmoteDef> parseDggEmotes(const QJsonArray &json);

/// Parses the flair manifest. Entries without a `name` are skipped. The order
/// of the manifest is kept.
std::vector<DggFlairDef> parseDggFlairs(const QJsonArray &json);

/// chat-gui's `EmoteService.emotesForUser`: privileged users may use every
/// emote, everyone else needs Patreon (for Patreon-only emotes) or a high
/// enough sub tier.
bool dggCanUseEmote(const DggEmoteDef &emote, const DggUser &user);

/// The flair whose colour the nick takes (chat-gui's `usernameColorFlair`):
/// of the manifest flairs the user has, the one with the lowest priority
/// number that has a colour. Priority ties keep the manifest order. Returns
/// null if there is none.
const DggFlairDef *dggNickColorFlair(const std::vector<DggFlairDef> &flairs,
                                     const QStringList &features);

}  // namespace chatterino
