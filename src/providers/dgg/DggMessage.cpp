#include "providers/dgg/DggMessage.hpp"

#include "util/FormatTime.hpp"

#include <QCryptographicHash>
#include <QHash>
#include <QJsonArray>

#include <algorithm>
#include <limits>

namespace chatterino {

using namespace Qt::Literals;

namespace {

QStringList toStringList(const QJsonValue &value)
{
    QStringList out;
    for (const auto &entry : value.toArray())
    {
        out.append(entry.toString());
    }
    return out;
}

qint64 toInt64(const QJsonValue &value)
{
    return static_cast<qint64>(value.toDouble());
}

/// Mute lengths are seconds. This keeps a bogus huge value from overflowing
/// formatTime()'s int parameter.
QString durationText(qint64 seconds)
{
    constexpr qint64 max = std::numeric_limits<int>::max();
    return formatTime(static_cast<int>(std::min(seconds, max)));
}

/// " by <nick>" for a real moderator, nothing for the system user or when the
/// event doesn't name one.
QString byText(const DggUser &moderator)
{
    if (moderator.isSystem() || moderator.nick.isEmpty())
    {
        return {};
    }
    return u" by "_s + moderator.nick;
}

QString tierText(int tier)
{
    if (tier > 1)
    {
        return u" (Tier %1+)"_s.arg(tier);
    }
    return {};
}

/// The texts chat-gui has for error codes that have no special handling.
const QHash<QString, QString> &errorStrings()
{
    static const QHash<QString, QString> strings{
        {u"unknown"_s,
         u"Unknown error, this usually indicates an internal problem :("_s},
        {u"nopermission"_s,
         u"You do not have the required permissions to use that"_s},
        {u"protocolerror"_s, u"Invalid or badly formatted"_s},
        {u"needlogin"_s, u"You have to be logged in to use that"_s},
        {u"msgempty"_s, u"Message cannot be empty"_s},
        {u"msgtoolong"_s, u"Message cannot be longer than 512 characters"_s},
        {u"invalidmsg"_s, u"The message was invalid"_s},
        {u"throttled"_s,
         u"Throttled! You were trying to send messages too fast"_s},
        {u"duplicate"_s,
         u"The message is identical to the last one you sent"_s},
        {u"submode"_s, u"The channel is currently in subscriber only mode"_s},
        {u"needbanreason"_s, u"Providing a reason for the ban is mandatory"_s},
        {u"privmsgbanned"_s, u"Cannot send private messages while banned"_s},
        {u"requiresocket"_s, u"This chat requires WebSockets"_s},
        {u"toomanyconnections"_s, u"Only 5 concurrent connections allowed"_s},
        {u"socketerror"_s, u"Error contacting server"_s},
        {u"privmsgaccounttooyoung"_s,
         u"Your account is too recent to send private messages"_s},
        {u"notfound"_s, u"The user was not found"_s},
        {u"notconnected"_s, u"You have to be connected to use that"_s},
        {u"activepoll"_s, u"Poll already started."_s},
        {u"noactivepoll"_s, u"No poll started."_s},
        {u"alreadyvoted"_s, u"You have already voted!"_s},
        {u"nochatting"_s,
         u"You aren't allowed to chat. Either you haven't picked a username, "
         u"or a mod disabled your privileges."_s},
        {u"duplicatephrase"_s, u"Banned phrase already exists."_s},
        {u"nophrase"_s, u"Banned phrase doesn't exist."_s},
        {u"invalidphrase"_s,
         u"Invalid phrase provided. Either the phrase was empty, or its "
         u"regex was invalid."_s},
        {u"cantbanprotected"_s, u"Protected users can't die."_s},
    };
    return strings;
}

}  // namespace

DggUser DggUser::fromJson(const QJsonObject &obj)
{
    DggUser user;
    user.id = toInt64(obj.value("id"_L1));
    user.nick = obj.value("nick"_L1).toString();
    user.roles = toStringList(obj.value("roles"_L1));
    user.features = toStringList(obj.value("features"_L1));
    // `subscription` is null for users without one, which toObject() turns
    // into an empty object (tier 0).
    user.subTier = obj.value("subscription"_L1)
                       .toObject()
                       .value("tier"_L1)
                       .toInt();
    user.patreonMember = obj.value("patreonMember"_L1).toBool();
    return user;
}

bool DggUser::hasRole(const QString &role) const
{
    return this->roles.contains(role);
}

bool DggUser::isPrivileged() const
{
    return this->hasRole(u"MODERATOR"_s) || this->hasRole(u"PROTECTED"_s) ||
           this->hasRole(u"ADMIN"_s);
}

bool DggUser::isSubscriber() const
{
    return this->subTier > 0;
}

bool DggUser::isSystem() const
{
    return this->id == -1;
}

QDateTime DggChatMessage::time() const
{
    return QDateTime::fromMSecsSinceEpoch(this->timestampMs);
}

QDateTime DggDeath::time() const
{
    return QDateTime::fromMSecsSinceEpoch(this->timestampMs);
}

QString dggMessageHash(qint64 timestampMs, qint64 userId, const QString &text)
{
    QByteArray input = QByteArray::number(timestampMs);
    if (userId != 0)
    {
        input += QByteArray::number(userId);
    }
    input += text.toUtf8();
    return QString::fromLatin1(
        QCryptographicHash::hash(input, QCryptographicHash::Md5).toHex());
}

qint64 dggTimestampMs(const QJsonObject &obj, qint64 nowMs)
{
    const auto value = obj.value("timestamp"_L1);
    if (!value.isDouble())
    {
        return nowMs;
    }
    return toInt64(value);
}

std::optional<DggChatMessage> parseDggChatMessage(const QJsonObject &obj,
                                                  qint64 nowMs)
{
    DggChatMessage msg;
    msg.user = DggUser::fromJson(obj);
    msg.rawText = obj.value("data"_L1).toString();
    if (msg.user.nick.isEmpty() || msg.rawText.isEmpty())
    {
        return std::nullopt;
    }

    msg.timestampMs = dggTimestampMs(obj, nowMs);
    msg.hash = dggMessageHash(msg.timestampMs, msg.user.id, msg.rawText);

    // chat-gui strips a leading "/me " (any case) and styles the line as an
    // action.
    msg.isAction = msg.rawText.startsWith(u"/me "_s, Qt::CaseInsensitive);
    msg.text = msg.isAction ? msg.rawText.mid(4) : msg.rawText;

    msg.greentext = msg.text.startsWith(u'>') &&
                    (msg.user.isSubscriber() || msg.user.isPrivileged());
    return msg;
}

std::optional<DggDeath> parseDggDeath(const QJsonObject &obj, qint64 nowMs)
{
    DggDeath death;
    death.user = DggUser::fromJson(obj);
    if (death.user.nick.isEmpty())
    {
        return std::nullopt;
    }

    death.cause = obj.value("data"_L1).toString().trimmed();
    death.durationSeconds = toInt64(obj.value("duration"_L1));
    death.timestampMs = dggTimestampMs(obj, nowMs);
    return death;
}

QString dggModerationText(QStringView event, const QJsonObject &obj)
{
    const auto target = obj.value("data"_L1).toString();
    if (target.isEmpty())
    {
        return {};
    }

    const auto moderator = DggUser::fromJson(obj);
    const auto by = byText(moderator);

    if (event == "MUTE"_L1)
    {
        QString duration;
        const auto seconds = toInt64(obj.value("duration"_L1));
        if (seconds > 0)
        {
            duration = u" for "_s + durationText(seconds);
        }

        if (by.isEmpty())
        {
            return u"%1 has been muted%2."_s.arg(target, duration);
        }
        return u"%1 muted%2%3."_s.arg(target, by, duration);
    }
    if (event == "UNMUTE"_L1)
    {
        if (by.isEmpty())
        {
            return u"%1 has been unmuted."_s.arg(target);
        }
        return u"%1 unmuted%2."_s.arg(target, by);
    }
    if (event == "BAN"_L1)
    {
        if (by.isEmpty())
        {
            return u"%1 has been banned."_s.arg(target);
        }
        return u"%1 banned%2."_s.arg(target, by);
    }
    if (event == "UNBAN"_L1)
    {
        if (by.isEmpty())
        {
            return u"%1 has been unbanned."_s.arg(target);
        }
        return u"%1 unbanned%2."_s.arg(target, by);
    }

    return {};
}

QString dggSubOnlyText(const QJsonObject &obj)
{
    const bool on = obj.value("data"_L1).toString() == "on"_L1;
    const int tier = obj.value("tier"_L1).toInt();

    QString by;
    const auto nick = obj.value("nick"_L1).toString();
    if (!nick.isEmpty())
    {
        by = u" by "_s + nick;
    }

    return u"Subscriber only mode%1 %2%3."_s.arg(
        tierText(on ? std::max(tier, 1) : 0), on ? u"enabled"_s : u"disabled"_s,
        by);
}

QString dggErrorText(const QJsonObject &obj)
{
    const auto desc = obj.value("description"_L1).toString();

    if (desc == "banned"_L1)
    {
        return u"You have been banned! Check your profile "
               u"(https://www.destiny.gg/profile) for more information. "
               u"Subscribing or donating removes non-permanent bans."_s;
    }
    if (desc == "muted"_L1)
    {
        const auto seconds = toInt64(obj.value("muteTimeLeft"_L1));
        if (seconds > 0)
        {
            return u"You are temporarily muted! You can chat again in %1. "
                   u"Subscribe or donate to remove the mute immediately."_s
                .arg(durationText(seconds));
        }
        return u"You are temporarily muted! Subscribe or donate to remove "
               u"the mute immediately."_s;
    }
    if (desc == "submode"_L1)
    {
        const int tier = std::max(obj.value("tier"_L1).toInt(), 1);
        return u"The channel is currently in subscriber only mode%1"_s.arg(
            tierText(tier));
    }
    if (desc == "bannedphrase"_L1)
    {
        return u"Your message was blocked because it contained this banned "
               u"phrase: \"%1\"."_s.arg(obj.value("filtered"_L1).toString());
    }

    const auto known = errorStrings().value(desc);
    if (!known.isEmpty())
    {
        return known;
    }
    return desc.isEmpty() ? u"Unknown error."_s : desc;
}

}  // namespace chatterino
