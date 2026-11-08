#include "providers/dgg/DggChatServer.hpp"

#include "common/QLogging.hpp"
#include "debug/AssertInGuiThread.hpp"
#include "providers/dgg/DggChannel.hpp"
#include "providers/dgg/DggFrame.hpp"
#include "providers/dgg/DggWebSocket.hpp"

#include <QJsonObject>

namespace chatterino {

using namespace Qt::Literals;

DggChatServer::DggChatServer() = default;

DggChatServer::~DggChatServer() = default;

void DggChatServer::initialize()
{
    // The websocket is created when the channel is first opened.
}

std::shared_ptr<Channel> DggChatServer::getOrCreate()
{
    assertInGuiThread();

    auto chan = this->channel_.lock();
    if (!chan)
    {
        chan = std::make_shared<DggChannel>();
        this->channel_ = chan;

        this->signalHolder_.managedConnect(chan->aboutToClose, [this] {
            if (this->socket_)
            {
                this->socket_->stop();
            }
        });
    }

    this->ensureConnected(chan);
    return chan;
}

std::shared_ptr<DggChannel> DggChatServer::channel() const
{
    return this->channel_.lock();
}

void DggChatServer::ensureConnected(const std::shared_ptr<DggChannel> &chan)
{
    if (!this->socket_)
    {
        this->socket_ = std::make_unique<DggWebSocket>();
        this->socket_->onOpened = [this] {
            this->onSocketOpened();
        };
        this->socket_->onClosed = [this](std::chrono::milliseconds retryIn) {
            this->onSocketClosed(retryIn);
        };
        this->socket_->onFrameReceived = [this](const DggFrame &frame) {
            this->onFrame(frame);
        };
    }

    if (!this->socket_->isStarted())
    {
        chan->addSystemMessage(u"Connecting to destiny.gg chat..."_s);
        this->socket_->start();
    }
}

void DggChatServer::onSocketOpened()
{
    // Nothing to show yet: the server follows up with NAMES, which says
    // "connected".
}

void DggChatServer::onSocketClosed(std::chrono::milliseconds retryIn)
{
    auto chan = this->channel_.lock();
    if (!chan)
    {
        return;
    }

    if (retryIn.count() > 0)
    {
        chan->addSystemMessage(
            u"Disconnected from destiny.gg chat. Reconnecting in %1 seconds..."_s
                .arg(static_cast<double>(retryIn.count()) / 1000.0, 0, 'f',
                     1));
    }
    else
    {
        chan->addSystemMessage(u"Disconnected from destiny.gg chat."_s);
    }
}

void DggChatServer::onFrame(const DggFrame &frame)
{
    auto chan = this->channel_.lock();
    if (!chan)
    {
        return;
    }

    const auto obj = frame.payload.toObject();

    if (frame.name == "NAMES"_L1)
    {
        chan->addSystemMessage(
            u"Connected to destiny.gg chat as Guest. Serving %1 connections and %2 users."_s
                .arg(obj.value("connectioncount"_L1).toInt())
                .arg(obj.value("usercount"_L1).toInt()));
    }
    else if (frame.name == "ERR"_L1)
    {
        const auto desc = obj.value("description"_L1).toString();
        if (desc == "banned"_L1)
        {
            chan->addSystemMessage(
                u"You have been banned from destiny.gg chat."_s);
        }
        else if (desc == "toomanyconnections"_L1)
        {
            chan->addSystemMessage(
                u"Too many connections to destiny.gg chat from this address."_s);
        }
        else
        {
            chan->addSystemMessage(u"destiny.gg chat error: %1"_s.arg(desc));
        }
    }
    else if (frame.name == "MSG"_L1)
    {
        // Phase 3 placeholder: Phase 4 replaces this with DggMessageBuilder.
        chan->addSystemMessage(
            u"%1: %2"_s.arg(obj.value("nick"_L1).toString(),
                            obj.value("data"_L1).toString()));
    }
    else
    {
        qCDebug(chatterinoDgg) << "Unhandled event" << frame.name;
    }
}

}  // namespace chatterino
