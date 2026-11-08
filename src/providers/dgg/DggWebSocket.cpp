#include "providers/dgg/DggWebSocket.hpp"

#include "common/QLogging.hpp"
#include "util/PostToThread.hpp"

#include <QJsonObject>
#include <QPointer>
#include <QRandomGenerator>

#include <algorithm>
#include <memory>
#include <string>
#include <utility>

namespace chatterino {

using namespace Qt::Literals;

namespace {

/// A connection must stay open this long to count as healthy. The server
/// completes the handshake before it authenticates, so a rejected connection
/// still opens first.
constexpr std::chrono::seconds STABLE_CONNECTION{5};

constexpr int64_t RETRY_WINDOW_MS = 3000;
constexpr int64_t RETRY_WINDOW_CAP_MS = 60000;
constexpr int64_t RETRY_MIN_MS = 500;

}  // namespace

struct DggWebSocket::Listener final : public WebSocketListener {
    Listener(QPointer<DggWebSocket> owner, uint64_t id)
        : owner(std::move(owner))
        , id(id)
    {
    }

    void onOpen() override
    {
        runInGuiThread([owner = this->owner, id = this->id] {
            if (owner)
            {
                owner->handleOpen(id);
            }
        });
    }

    void onTextMessage(QByteArray data) override
    {
        auto frame = parseDggFrame(data);
        if (!frame)
        {
            return;
        }

        std::vector<DggFrame> frames;
        if (frame->name == "HISTORY"_L1)
        {
            expandDggHistory(frame->payload, frames);
        }
        else if (frame->name == "NAMES"_L1)
        {
            // ~1500 user objects. Only the counts are used for now, so don't
            // copy the list over to the GUI thread.
            const auto obj = frame->payload.toObject();
            QJsonObject slim;
            slim.insert("connectioncount"_L1, obj.value("connectioncount"_L1));
            slim.insert("usercount"_L1,
                        static_cast<int>(obj.value("users"_L1).toArray().size()));
            frame->payload = slim;
            frames.push_back(std::move(*frame));
        }
        else
        {
            frames.push_back(std::move(*frame));
        }

        if (frames.empty())
        {
            return;
        }

        runInGuiThread([owner = this->owner, id = this->id,
                        frames = std::move(frames)]() mutable {
            if (owner)
            {
                owner->handleFrames(id, std::move(frames));
            }
        });
    }

    void onBinaryMessage(QByteArray /*data*/) override
    {
        // DGG only sends text frames.
    }

    void onClose(std::unique_ptr<WebSocketListener> /*self*/) override
    {
        runInGuiThread([owner = this->owner, id = this->id] {
            if (owner)
            {
                owner->handleClose(id);
            }
        });
    }

    QPointer<DggWebSocket> owner;
    uint64_t id;
};

QUrl DggWebSocket::defaultUrl()
{
    return QUrl(QStringLiteral("wss://chat.destiny.gg/ws"));
}

DggWebSocket::DggWebSocket(QUrl url)
    : url_(std::move(url))
{
    this->retryTimer_.setSingleShot(true);
    QObject::connect(&this->retryTimer_, &QTimer::timeout, this, [this] {
        this->connectNow();
    });
}

DggWebSocket::~DggWebSocket()
{
    this->stop();
    this->pool_.reset();
}

void DggWebSocket::start()
{
    if (this->wanted_)
    {
        return;
    }

    this->wanted_ = true;
    this->retryEnabled_ = true;
    this->retryAttempts_ = 0;
    this->connectNow();
}

void DggWebSocket::stop()
{
    this->wanted_ = false;
    this->retryTimer_.stop();
    this->generation_++;
    this->openedAt_.reset();
    this->handle_ = WebSocketHandle{};
}

void DggWebSocket::connectNow()
{
    if (!this->wanted_)
    {
        return;
    }

    this->generation_++;
    this->retryAttempts_++;
    this->openedAt_.reset();

    if (!this->pool_)
    {
        this->pool_.emplace(QStringLiteral("dgg"));
    }

    // The browser sends this Origin, and the server may check it.
    std::vector<std::pair<std::string, std::string>> headers;
    headers.emplace_back("Origin", "https://www.destiny.gg");

    qCDebug(chatterinoDgg) << "Connecting to" << this->url_
                           << "attempt:" << this->retryAttempts_;

    this->handle_ = this->pool_->createSocket(
        WebSocketOptions{
            .url = this->url_,
            .headers = std::move(headers),
        },
        std::make_unique<Listener>(QPointer<DggWebSocket>(this),
                                   this->generation_));
}

std::chrono::milliseconds DggWebSocket::retryDelay() const
{
    // Full jitter over a window that doubles with every failed attempt. The
    // jitter matters as much as the backoff, because a server restart
    // disconnects every client at once.
    const auto shift = std::min(this->retryAttempts_, 5U);
    const int64_t window =
        std::min<int64_t>(RETRY_WINDOW_CAP_MS, RETRY_WINDOW_MS << shift);
    const auto jitter = QRandomGenerator::global()->bounded(
        static_cast<qint64>(window - RETRY_MIN_MS));
    return std::chrono::milliseconds{RETRY_MIN_MS +
                                     static_cast<int64_t>(jitter)};
}

void DggWebSocket::handleOpen(uint64_t id)
{
    if (id != this->generation_)
    {
        return;
    }

    qCDebug(chatterinoDgg) << "Connection opened";
    this->openedAt_ = std::chrono::steady_clock::now();
    if (this->onOpened)
    {
        this->onOpened();
    }
}

void DggWebSocket::handleFrames(uint64_t id, std::vector<DggFrame> frames)
{
    for (const auto &frame : frames)
    {
        if (id != this->generation_)
        {
            // stop() or a reconnect happened inside a callback
            return;
        }

        if (frame.name == "ERR"_L1)
        {
            const auto desc =
                frame.payload.toObject().value("description"_L1).toString();
            qCDebug(chatterinoDgg) << "ERR" << desc;
            if (desc == "banned"_L1 || desc == "toomanyconnections"_L1)
            {
                this->retryEnabled_ = false;
            }
        }

        if (this->onFrameReceived)
        {
            this->onFrameReceived(frame);
        }
    }
}

void DggWebSocket::handleClose(uint64_t id)
{
    if (id != this->generation_)
    {
        return;
    }

    this->handle_ = WebSocketHandle{};

    const bool stable =
        this->openedAt_.has_value() &&
        (std::chrono::steady_clock::now() - *this->openedAt_) >=
            STABLE_CONNECTION;
    this->openedAt_.reset();
    if (stable)
    {
        this->retryAttempts_ = 0;
    }

    if (!this->wanted_ || !this->retryEnabled_)
    {
        qCDebug(chatterinoDgg) << "Connection closed, not retrying";
        if (this->onClosed)
        {
            this->onClosed(std::chrono::milliseconds{0});
        }
        return;
    }

    const auto delay = this->retryDelay();
    qCDebug(chatterinoDgg) << "Connection closed, retrying in" << delay.count()
                           << "ms";
    this->retryTimer_.start(delay);
    if (this->onClosed)
    {
        this->onClosed(delay);
    }
}

}  // namespace chatterino
