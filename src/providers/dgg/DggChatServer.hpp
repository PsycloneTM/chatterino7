#pragma once

#include <pajlada/signals/signalholder.hpp>
#include <QObject>

#include <chrono>
#include <memory>

namespace chatterino {

class Channel;
class DggChannel;
class DggWebSocket;
struct DggFrame;

/// Owns the destiny.gg chat channel and its websocket.
///
/// The connection is opened when the channel is first requested and closed
/// again when the last split showing it goes away.
class DggChatServer : public QObject
{
public:
    DggChatServer();
    ~DggChatServer() override;

    Q_DISABLE_COPY_MOVE(DggChatServer)

    void initialize();

    /// Returns the DGG channel, creating it if no split currently holds it.
    /// This also makes sure the websocket is connected.
    ///
    /// Must be called from the GUI thread.
    std::shared_ptr<Channel> getOrCreate();

    /// Returns the DGG channel if it currently exists, otherwise null.
    std::shared_ptr<DggChannel> channel() const;

private:
    void ensureConnected(const std::shared_ptr<DggChannel> &chan);

    void onSocketOpened();
    void onSocketClosed(std::chrono::milliseconds retryIn);
    void onFrame(const DggFrame &frame);

    std::weak_ptr<DggChannel> channel_;
    std::unique_ptr<DggWebSocket> socket_;
    pajlada::Signals::SignalHolder signalHolder_;
};

}  // namespace chatterino
