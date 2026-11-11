#pragma once

#include "common/Channel.hpp"

#include <QSet>
#include <QString>

#include <deque>

namespace chatterino {

/// The single destiny.gg chat room.
///
/// DGG only has one room, so unlike KickChannel there's no per-channel lookup
/// or live status. The channel is read-only until sending is implemented.
class DggChannel : public Channel
{
public:
    /// The name used to identify the DGG channel in layouts and logs.
    static const QString &channelName();

    DggChannel();
    ~DggChannel() override;

    /// Fired from the destructor, so the server can drop the connection once
    /// no split shows DGG chat anymore.
    pajlada::Signals::NoArgSignal aboutToClose;

    const QString &getDisplayName() const override;

    bool canSendMessage() const override;
    bool isWritable() const override;

    /// Remembers that an event with this key was shown.
    ///
    /// Returns false if it was already seen, so the caller can skip it. This
    /// is what keeps the backlog the server replays after a reconnect from
    /// showing messages twice. Only the most recent keys are kept.
    ///
    /// Must be called from the GUI thread.
    bool markSeen(const QString &key);

    /// Greys out every message already in the channel from this nick, like a
    /// timeout does on Twitch (the Disabled flag draws the dimmed overlay).
    ///
    /// System lines (including the mute notice itself) are left alone, and
    /// messages that arrive later aren't touched, so someone who is unmuted
    /// speaks normally again. Must be called from the GUI thread.
    void disableMessagesFrom(const QString &nick);

private:
    static constexpr size_t MAX_SEEN_KEYS = 2000;

    QSet<QString> seen_;
    std::deque<QString> seenOrder_;
};

}  // namespace chatterino
