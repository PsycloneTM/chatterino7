#include "providers/dgg/DggChatServer.hpp"

#include "common/QLogging.hpp"
#include "debug/AssertInGuiThread.hpp"
#include "providers/dgg/DggChannel.hpp"
#include "providers/dgg/DggFrame.hpp"
#include "providers/dgg/DggMessage.hpp"
#include "providers/dgg/DggMessageBuilder.hpp"
#include "providers/dgg/DggWebSocket.hpp"

#include <QDateTime>
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
    auto chan = this->channel_.lock();
    if (!chan)
    {
        return;
    }

    // The server sends its backlog right after the handshake, so this lands
    // before the history. The user and connection counts follow with NAMES.
    chan->addSystemMessage(u"Connected to destiny.gg chat as Guest."_s);
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
    const auto nowMs = QDateTime::currentMSecsSinceEpoch();
    const auto &name = frame.name;

    if (name == "MSG"_L1)
    {
        auto msg = parseDggChatMessage(obj, nowMs);
        if (!msg)
        {
            return;
        }
        if (!chan->markSeen(msg->hash))
        {
            // Already shown, e.g. in the backlog of a reconnect
            return;
        }

        chan->addMessage(
            DggMessageBuilder::makeChatMessage(chan.get(), *msg, frame.backlog),
            MessageContext::Original);
    }
    else if (name == "MUTE"_L1 || name == "UNMUTE"_L1 || name == "BAN"_L1 ||
             name == "UNBAN"_L1 || name == "SUBONLY"_L1)
    {
        const auto text = name == "SUBONLY"_L1 ? dggSubOnlyText(obj)
                                               : dggModerationText(name, obj);
        if (text.isEmpty())
        {
            return;
        }

        const auto timestampMs = dggTimestampMs(obj, nowMs);
        const auto key = u"%1|%2|%3|%4"_s.arg(
            name, QString::number(timestampMs),
            obj.value("data"_L1).toString(), obj.value("nick"_L1).toString());
        if (!chan->markSeen(key))
        {
            return;
        }

        chan->addMessage(
            DggMessageBuilder::makeSystemLine(
                chan.get(), QDateTime::fromMSecsSinceEpoch(timestampMs), text,
                frame.backlog, true),
            MessageContext::Original);
    }
    else if (name == "BROADCAST"_L1)
    {
        const auto text = obj.value("data"_L1).toString();
        if (text.isEmpty())
        {
            return;
        }

        const auto timestampMs = dggTimestampMs(obj, nowMs);
        const auto uuid = obj.value("uuid"_L1).toString();
        const auto key =
            u"BROADCAST|%1|%2"_s.arg(uuid.isEmpty() ? text : uuid,
                                      QString::number(timestampMs));
        if (!chan->markSeen(key))
        {
            return;
        }

        chan->addMessage(
            DggMessageBuilder::makeBroadcastMessage(
                chan.get(), QDateTime::fromMSecsSinceEpoch(timestampMs),
                DggUser::fromJson(obj.value("user"_L1).toObject()), text,
                frame.backlog),
            MessageContext::Original);
    }
    else if (name == "NAMES"_L1)
    {
        chan->addSystemMessage(
            u"Serving %1 connections and %2 users."_s
                .arg(obj.value("connectioncount"_L1).toInt())
                .arg(obj.value("usercount"_L1).toInt()));
    }
    else if (name == "ERR"_L1)
    {
        chan->addSystemMessage(dggErrorText(obj));
    }
    else if (name == "JOIN"_L1 || name == "QUIT"_L1 || name == "USERSDELTA"_L1)
    {
        // With ~1,500 users these would flood the split. They're shown nowhere
        // until the user list (phase 7).
    }
    else
    {
        qCDebug(chatterinoDgg) << "Unhandled event" << name;
    }
}

}  // namespace chatterino
