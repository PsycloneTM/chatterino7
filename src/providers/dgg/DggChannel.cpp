#include "providers/dgg/DggChannel.hpp"

namespace chatterino {

const QString &DggChannel::channelName()
{
    static const QString name(QStringLiteral("destiny.gg"));
    return name;
}

DggChannel::DggChannel()
    : Channel(DggChannel::channelName(), Channel::Type::Dgg)
{
}

DggChannel::~DggChannel() = default;

const QString &DggChannel::getDisplayName() const
{
    return this->getName();
}

bool DggChannel::canSendMessage() const
{
    // Read-only for now (see DGG_PLAN.md, phase 6)
    return false;
}

bool DggChannel::isWritable() const
{
    return false;
}

}  // namespace chatterino
