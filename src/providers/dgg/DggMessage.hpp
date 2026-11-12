#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QStringView>

#include <optional>

namespace chatterino {

/// The user fields that come with every `MSG`, `JOIN`, `QUIT`, `MUTE`, ...
/// event (`id`, `nick`, `roles`, `features`, `subscription`).
///
/// Note that `id` is the *user's* id (it sits next to `createdDate`), not an
/// id of the message.
struct DggUser {
    qint64 id = 0;
    QString nick;
    QStringList roles;

    /// Flair names (`flair17`, `admin`, `subscriber`, ...).
    QStringList features;

    /// 0 if the user isn't subscribed.
    int subTier = 0;

    /// Patreon members can use the Patreon-only emotes.
    bool patreonMember = false;

    static DggUser fromJson(const QJsonObject &obj);

    bool hasRole(const QString &role) const;

    /// Moderators, protected users and admins. Privileges come only from
    /// roles, never from flairs.
    bool isPrivileged() const;

    bool isSubscriber() const;

    /// The server sends events that aren't caused by a person (e.g. the
    /// website lifting a mute) as user -1.
    bool isSystem() const;
};

/// A `MSG` event, with the derived values the message builder needs.
struct DggChatMessage {
    DggUser user;

    /// The text exactly as sent, including a leading `/me `.
    QString rawText;

    /// The text to show: `rawText` without the `/me ` prefix.
    QString text;

    /// Unix time in milliseconds.
    qint64 timestampMs = 0;

    /// chat-gui's message hash (see dggMessageHash()). Unique enough to
    /// recognise a message that arrives twice, e.g. in a backlog replay.
    QString hash;

    /// The message started with `/me `.
    bool isAction = false;

    /// The message starts with `>` and the author may use greentext
    /// (subscribers and privileged users only).
    bool greentext = false;

    QDateTime time() const;
};

/// A `DEATH` event. Unlike `MUTE` and `BAN`, the user fields are those of the
/// user who died, and `data` is the cause (free text, may contain emotes).
struct DggDeath {
    DggUser user;

    /// What the site shows after the nick, e.g. `engaged in dishonorable PVP.`
    QString cause;

    /// How long the user is silenced for. 0 if the event has no duration.
    qint64 durationSeconds = 0;

    /// Unix time in milliseconds.
    qint64 timestampMs = 0;

    QDateTime time() const;
};

/// Parses a `DEATH` payload. Returns nullopt if it has no nick.
std::optional<DggDeath> parseDggDeath(const QJsonObject &obj, qint64 nowMs);

/// `md5(timestampMs + userId + text)` as lowercase hex, the same recipe
/// chat-gui uses to identify a message (and the server uses to name a
/// spotlighted one). A user id of 0 (unknown) counts as empty.
QString dggMessageHash(qint64 timestampMs, qint64 userId, const QString &text);

/// The event's `timestamp` (Unix ms), or `nowMs` if it has none.
qint64 dggTimestampMs(const QJsonObject &obj, qint64 nowMs);

/// Parses a `MSG` payload. Returns nullopt if it has no nick or no text.
std::optional<DggChatMessage> parseDggChatMessage(const QJsonObject &obj,
                                                  qint64 nowMs);

/// The line for `MUTE`, `UNMUTE`, `BAN` and `UNBAN`, worded like chat-gui.
/// Returns an empty string for any other event or a payload without a target.
QString dggModerationText(QStringView event, const QJsonObject &obj);

/// The line for `SUBONLY`.
QString dggSubOnlyText(const QJsonObject &obj);

/// A readable message for an `ERR` payload (`{"description": "<code>"}`).
QString dggErrorText(const QJsonObject &obj);

}  // namespace chatterino
