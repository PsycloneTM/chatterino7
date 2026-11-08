#include "providers/dgg/DggMessageBuilder.hpp"

#include "common/LinkParser.hpp"
#include "messages/MessageElement.hpp"
#include "providers/dgg/DggAssets.hpp"
#include "providers/dgg/DggChannel.hpp"
#include "providers/dgg/DggMessage.hpp"
#include "singletons/Settings.hpp"

namespace {

using namespace chatterino;

/// chat-gui's `$color-green-text`.
const QColor GREENTEXT_COLOR{0x6c, 0xa5, 0x28};

/// Adds one word of user text: an emote if the word is one the author may
/// use, otherwise a link or plain text.
void appendWord(DggMessageBuilder &builder, QStringView word,
                const DggUser &user, bool greentext, const DggAssets &assets)
{
    const MessageColor textColor =
        greentext ? MessageColor(GREENTEXT_COLOR) : MessageColor::Text;

    if (auto emote = assets.emoteFor(word, user))
    {
        // textColor is only used if the image can't be shown
        builder.emplace<EmoteElement>(emote, MessageElementFlag::Emote,
                                      textColor);
        return;
    }

    if (!greentext)
    {
        builder.addWordFromUserMessage(word);
        return;
    }

    // addWordFromUserMessage() always uses the default text colour, so
    // greentext words are emitted here. Links keep their link colour.
    if (auto link = linkparser::parse(word))
    {
        builder.addLink(*link, word);
        return;
    }
    builder.emplace<TextElement>(word.toString(), MessageElementFlag::Text,
                                 textColor);
}

QString displayedNick(const QString &nick)
{
    if (auto nickname = getSettings()->matchNickname(nick))
    {
        return *nickname;
    }
    return nick;
}

}  // namespace

namespace chatterino {

DggMessageBuilder::DggMessageBuilder(SystemMessageTag /*tag*/,
                                     DggChannel *channel, const QDateTime &time)
    : channel_(channel)
{
    this->emplace<TimestampElement>(time.time());
    this->message().flags.set(MessageFlag::System);
    this->message().platform = MessagePlatform::Dgg;
    this->message().timestamp = time;
}

DggMessageBuilder::DggMessageBuilder(DggChannel *channel, const QDateTime &time)
    : channel_(channel)
{
    this->emplace<TimestampElement>(time.time());
    this->message().platform = MessagePlatform::Dgg;
    this->message().timestamp = time;
}

void DggMessageBuilder::markBacklog()
{
    this->message().flags.set(MessageFlag::RecentMessage,
                              MessageFlag::DoNotLog);
}

MessagePtrMut DggMessageBuilder::makeChatMessage(DggChannel *channel,
                                                 const DggChatMessage &msg,
                                                 bool backlog,
                                                 const DggAssets &assets)
{
    DggMessageBuilder builder(channel, msg.time());
    builder->channelName = channel->getName();
    builder->id = msg.hash;
    builder->displayName = msg.user.nick;
    builder->loginName = msg.user.nick.toLower();
    builder->userID = QString::number(msg.user.id);

    if (backlog)
    {
        builder.markBacklog();
    }
    if (msg.isAction)
    {
        builder->flags.set(MessageFlag::Action);
    }

    // Flairs first, like chat-gui. Linking the nick to the user popup waits
    // for phase 7: the popup only knows Twitch and Kick.
    for (const auto &badge : assets.badgesFor(msg.user))
    {
        builder.emplace<BadgeElement>(badge, MessageElementFlag::BadgeVanity);
    }

    const auto nickColor = assets.nickColorFor(msg.user);
    if (nickColor)
    {
        builder->usernameColor = *nickColor;
    }

    const QString nick = displayedNick(msg.user.nick);
    QString usernameText = nick;
    if (!msg.isAction)
    {
        usernameText.append(u':');
    }
    builder.emplace<TextElement>(
        usernameText, MessageElementFlag::Username,
        nickColor ? MessageColor(*nickColor) : MessageColor::Text,
        FontStyle::ChatMediumBold);

    const QString simplified = msg.text.simplified();
    QString messageText;
    for (auto word : QStringView(simplified).tokenize(u' ', Qt::SkipEmptyParts))
    {
        if (!messageText.isEmpty())
        {
            messageText.append(u' ');
        }
        messageText.append(word);
        appendWord(builder, word, msg.user, msg.greentext, assets);
    }

    builder->messageText = messageText;
    builder->searchText = builder->loginName + QChar(u' ') + nick +
                          QStringLiteral(": ") + messageText;

    return builder.release();
}

MessagePtrMut DggMessageBuilder::makeSystemLine(DggChannel *channel,
                                                const QDateTime &time,
                                                const QString &text,
                                                bool backlog, bool moderation)
{
    DggMessageBuilder builder(systemMessage, channel, time);
    if (backlog)
    {
        builder.markBacklog();
    }
    if (moderation)
    {
        builder->flags.set(MessageFlag::ModerationAction);
    }

    builder.emplace<TextElement>(text, MessageElementFlag::Text,
                                 MessageColor::System);
    builder->messageText = text;
    builder->searchText = text;

    return builder.release();
}

MessagePtrMut DggMessageBuilder::makeBroadcastMessage(DggChannel *channel,
                                                      const QDateTime &time,
                                                      const DggUser &user,
                                                      const QString &text,
                                                      bool backlog)
{
    DggMessageBuilder builder(systemMessage, channel, time);
    if (backlog)
    {
        builder.markBacklog();
    }

    QString full = text;
    if (!user.isSystem() && !user.nick.isEmpty())
    {
        full = user.nick + QStringLiteral(": ") + text;
        builder->loginName = user.nick.toLower();
        builder->displayName = user.nick;
    }

    builder.emplace<TextElement>(full, MessageElementFlag::Text,
                                 MessageColor::Text, FontStyle::ChatMediumBold);
    builder->messageText = full;
    builder->searchText = full;

    return builder.release();
}

}  // namespace chatterino
