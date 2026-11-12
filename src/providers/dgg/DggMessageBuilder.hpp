#pragma once

#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"

#include <QDateTime>
#include <QString>

namespace chatterino {

class DggAssets;
class DggChannel;
struct DggChatMessage;
struct DggDeath;
struct DggUser;

/// Builds the messages shown in the destiny.gg channel.
///
/// Every `make*` function takes `backlog`: true for events that came out of
/// the `HISTORY` replay. Those are marked as recent messages (so they're
/// faded like Twitch's recent-messages history, if the user has that turned
/// on) and are not written to the chat log again.
class DggMessageBuilder : public MessageBuilder
{
public:
    DggMessageBuilder(SystemMessageTag, DggChannel *channel,
                      const QDateTime &time);
    DggMessageBuilder(DggChannel *channel, const QDateTime &time);

    /// A chat line: timestamp, flair badges, nick (in its flair colour) and
    /// the text with emotes and links. Greentext is coloured, `/me` lines
    /// have no colon after the nick.
    static MessagePtrMut makeChatMessage(DggChannel *channel,
                                         const DggChatMessage &msg,
                                         bool backlog,
                                         const DggAssets &assets);

    /// A system line with the given time. `moderation` marks it as a
    /// moderation action (mutes, bans, sub-only mode).
    static MessagePtrMut makeSystemLine(DggChannel *channel,
                                        const QDateTime &time,
                                        const QString &text, bool backlog,
                                        bool moderation);

    /// A `DEATH` line, like the site shows it: a warning icon, the nick in
    /// its flair colour and the cause in italics (with emotes). Flagged as a
    /// system line, so greying out the dead user's messages skips it.
    static MessagePtrMut makeDeathMessage(DggChannel *channel,
                                          const DggDeath &death, bool backlog,
                                          const DggAssets &assets);

    /// A broadcast from a moderator or the system, shown in bold.
    static MessagePtrMut makeBroadcastMessage(DggChannel *channel,
                                              const QDateTime &time,
                                              const DggUser &user,
                                              const QString &text,
                                              bool backlog);

    DggChannel *channel() const
    {
        return this->channel_;
    }

private:
    void markBacklog();

    DggChannel *channel_ = nullptr;
};

}  // namespace chatterino
