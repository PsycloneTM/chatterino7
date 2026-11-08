#pragma once

#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"

#include <QDateTime>
#include <QString>

namespace chatterino {

class DggChannel;
struct DggChatMessage;
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

    /// A chat line: timestamp, nick, and the text with links. Greentext is
    /// coloured, `/me` lines have no colon after the nick.
    static MessagePtrMut makeChatMessage(DggChannel *channel,
                                         const DggChatMessage &msg,
                                         bool backlog);

    /// A system line with the given time. `moderation` marks it as a
    /// moderation action (mutes, bans, sub-only mode).
    static MessagePtrMut makeSystemLine(DggChannel *channel,
                                        const QDateTime &time,
                                        const QString &text, bool backlog,
                                        bool moderation);

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
