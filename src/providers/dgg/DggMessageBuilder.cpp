#include "providers/dgg/DggMessageBuilder.hpp"

#include "common/LinkParser.hpp"
#include "messages/Image.hpp"
#include "messages/MessageElement.hpp"
#include "messages/SpriteSheet.hpp"
#include "providers/dgg/DggAssets.hpp"
#include "providers/dgg/DggChannel.hpp"
#include "providers/dgg/DggMessage.hpp"
#include "singletons/Settings.hpp"

#include <QStringList>

#include <algorithm>
#include <vector>

namespace {

using namespace chatterino;

/// chat-gui's `$color-green-text`.
const QColor GREENTEXT_COLOR{0x6c, 0xa5, 0x28};

/// The sheet chat-gui takes its icons from (`img/icons.<hash>.png`). The hash
/// changes when the site is rebuilt with a new sheet, so if the death icon
/// goes blank, this is the URL to update.
const QString ICONS_SHEET_URL = QStringLiteral(
    "https://cdn.destiny.gg/img/icons.b81b90059d7691309000.png");

/// The warning sign in front of a death line: `.msg-death .user::before` is a
/// 16x16 box showing the sheet at `background-position: -32px 0`.
ImagePtr deathIcon()
{
    SpriteSheetSpec spec;
    spec.frameSize = QSize(16, 16);
    spec.offsets = {QPoint(32, 0)};
    spec.durationsMs = {100};
    return Image::fromSpriteSheet(Url{ICONS_SHEET_URL}, spec);
}

/// Adds one word of user text: an emote if the word is one the author may
/// use, otherwise a link or plain text.
void appendWord(DggMessageBuilder &builder, QStringView word,
                const DggUser &user, const DggEmoteContext &emoteContext,
                bool greentext, const DggAssets &assets)
{
    const MessageColor textColor =
        greentext ? MessageColor(GREENTEXT_COLOR) : MessageColor::Text;

    if (auto emote = assets.emoteFor(word, user, emoteContext))
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

/// The elements chat-gui would put directly inside the message's text, in
/// order: the emote's name for an emote, an empty string for anything else
/// that is an element (a link, an `@mention`). Plain words are text nodes and
/// don't count. The emote CSS looks at these to tell which emotes sit next to
/// each other.
struct TextElements {
    QStringList names;
    /// For every word, the index into `names` of the element it became, or -1
    /// for a plain word.
    std::vector<int> indexOfWord;
};

bool isMention(QStringView word)
{
    // chat-gui wraps `@nick` (3 to 20 word characters, optionally followed by
    // punctuation) in a span
    if (!word.startsWith(u'@'))
    {
        return false;
    }
    auto name = word.sliced(1);
    while (!name.isEmpty() && QStringView(u".?!,'").contains(name.back()))
    {
        name.chop(1);
    }
    if (name.size() < 3 || name.size() > 20)
    {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](QChar c) {
        return c.isLetterOrNumber() || c == u'_';
    });
}

TextElements collectElements(const QStringList &words, const DggUser &user,
                             const DggAssets &assets)
{
    TextElements out;
    for (const auto &word : words)
    {
        if (assets.isEmoteFor(word, user))
        {
            out.indexOfWord.push_back(static_cast<int>(out.names.size()));
            out.names.append(word);
        }
        else if (isMention(word) || linkparser::parse(word))
        {
            out.indexOfWord.push_back(static_cast<int>(out.names.size()));
            out.names.append(QString());
        }
        else
        {
            out.indexOfWord.push_back(-1);
        }
    }
    return out;
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

    const auto nickStyle = assets.nickStyleFor(msg.user);
    const auto &nickColor = nickStyle.color;
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
    auto *nickElement = builder.emplace<TextElement>(
        usernameText, MessageElementFlag::Username,
        nickColor ? MessageColor(*nickColor) : MessageColor::Text,
        FontStyle::ChatMediumBold);
    if (nickStyle.rainbow)
    {
        nickElement->addFlags(MessageElementFlag::DggRainbowUsername);
    }

    // Emotes look different depending on who says them and on what sits next
    // to them
    auto emoteContext = assets.emoteContextFor(msg.user, msg.isAction, backlog);
    emoteContext.greentext = msg.greentext;

    const QString simplified = msg.text.simplified();
    const QStringList words =
        simplified.split(QChar(u' '), Qt::SkipEmptyParts);
    const auto elements = collectElements(words, msg.user, assets);
    emoteContext.elements = elements.names;

    QString messageText;
    for (qsizetype i = 0; i < words.size(); i++)
    {
        const QString &word = words[i];
        if (!messageText.isEmpty())
        {
            messageText.append(u' ');
        }
        messageText.append(word);

        emoteContext.elementIndex =
            elements.indexOfWord[static_cast<size_t>(i)];
        appendWord(builder, word, msg.user, emoteContext, msg.greentext,
                   assets);
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

MessagePtrMut DggMessageBuilder::makeDeathMessage(DggChannel *channel,
                                                  const DggDeath &death,
                                                  bool backlog,
                                                  const DggAssets &assets)
{
    DggMessageBuilder builder(systemMessage, channel, death.time());
    if (backlog)
    {
        builder.markBacklog();
    }
    builder->flags.set(MessageFlag::ModerationAction);

    builder.emplace<ImageElement>(deathIcon(), MessageElementFlag::Text);

    const auto &user = death.user;
    const auto nickStyle = assets.nickStyleFor(user);
    const auto &nickColor = nickStyle.color;
    const QString nick = displayedNick(user.nick);
    auto *nickElement = builder.emplace<TextElement>(
        nick, MessageElementFlag::Username,
        nickColor ? MessageColor(*nickColor) : MessageColor::Text,
        FontStyle::ChatMediumBold);
    if (nickStyle.rainbow)
    {
        nickElement->addFlags(MessageElementFlag::DggRainbowUsername);
    }

    // The cause is in italics and can contain emotes
    auto emoteContext = assets.emoteContextFor(user, false, backlog);
    const QStringList words =
        death.cause.simplified().split(QChar(u' '), Qt::SkipEmptyParts);
    const auto elements = collectElements(words, user, assets);
    emoteContext.elements = elements.names;

    QString causeText;
    for (qsizetype i = 0; i < words.size(); i++)
    {
        const QString &word = words[i];
        if (!causeText.isEmpty())
        {
            causeText.append(u' ');
        }
        causeText.append(word);

        emoteContext.elementIndex =
            elements.indexOfWord[static_cast<size_t>(i)];
        if (auto emote = assets.emoteFor(word, user, emoteContext))
        {
            builder.emplace<EmoteElement>(emote, MessageElementFlag::Emote,
                                          MessageColor::Text);
        }
        else
        {
            builder.emplace<TextElement>(word, MessageElementFlag::Text,
                                         MessageColor::Text,
                                         FontStyle::ChatMediumItalic);
        }
    }

    const QString full = causeText.isEmpty() ? nick : nick + QChar(u' ') + causeText;
    builder->messageText = full;
    builder->searchText = full;

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
