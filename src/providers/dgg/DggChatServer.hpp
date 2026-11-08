#pragma once

#include <QObject>

#include <memory>

namespace chatterino {

class Channel;
class DggChannel;

/// Owns the destiny.gg chat channel (and, from phase 3, its websocket).
class DggChatServer : public QObject
{
public:
    DggChatServer();
    ~DggChatServer() override;

    Q_DISABLE_COPY_MOVE(DggChatServer)

    void initialize();

    /// Returns the DGG channel, creating it if no split currently holds it.
    ///
    /// Must be called from the GUI thread.
    std::shared_ptr<Channel> getOrCreate();

    /// Returns the DGG channel if it currently exists, otherwise null.
    std::shared_ptr<DggChannel> channel() const;

private:
    std::weak_ptr<DggChannel> channel_;
};

}  // namespace chatterino
