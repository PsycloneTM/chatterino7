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

DggChannel::~DggChannel()
{
    this->aboutToClose.invoke();
}

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

bool DggChannel::markSeen(const QString &key)
{
    if (this->seen_.contains(key))
    {
        return false;
    }

    this->seen_.insert(key);
    this->seenOrder_.push_back(key);
    while (this->seenOrder_.size() > MAX_SEEN_KEYS)
    {
        this->seen_.remove(this->seenOrder_.front());
        this->seenOrder_.pop_front();
    }
    return true;
}

}  // namespace chatterino
