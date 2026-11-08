#include "providers/dgg/DggChatServer.hpp"

#include "debug/AssertInGuiThread.hpp"
#include "providers/dgg/DggChannel.hpp"

namespace chatterino {

DggChatServer::DggChatServer() = default;

DggChatServer::~DggChatServer() = default;

void DggChatServer::initialize()
{
    // Phase 3 connects the websocket here.
}

std::shared_ptr<Channel> DggChatServer::getOrCreate()
{
    assertInGuiThread();

    if (auto existing = this->channel_.lock())
    {
        return existing;
    }

    auto chan = std::make_shared<DggChannel>();
    this->channel_ = chan;
    return chan;
}

std::shared_ptr<DggChannel> DggChatServer::channel() const
{
    return this->channel_.lock();
}

}  // namespace chatterino
