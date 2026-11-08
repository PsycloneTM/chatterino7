#pragma once

#include "providers/dgg/DggFrame.hpp"

#include <pajlada/signals/signalholder.hpp>
#include <QObject>

#include <chrono>
#include <memory>
#include <vector>

namespace chatterino {

class Channel;
class DggAssets;
class DggChannel;
class DggWebSocket;

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
    /// Entry point for frames from the websocket. Holds them back until the
    /// emote and flair manifests are loaded, so the backlog isn't built
    /// without emotes.
    void onFrame(const DggFrame &frame);
    void handleFrame(const DggFrame &frame);

    std::weak_ptr<DggChannel> channel_;
    std::unique_ptr<DggAssets> assets_;
    std::vector<DggFrame> pendingFrames_;
    std::unique_ptr<DggWebSocket> socket_;
    pajlada::Signals::SignalHolder signalHolder_;
};

}  // namespace chatterino
