#pragma once

#include "common/Channel.hpp"

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

    const QString &getDisplayName() const override;

    bool canSendMessage() const override;
    bool isWritable() const override;
};

}  // namespace chatterino
