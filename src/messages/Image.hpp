// SPDX-FileCopyrightText: 2017 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/Aliases.hpp"
#include "messages/SpriteSheet.hpp"
#include "util/DebugCount.hpp"

#include <QList>
#include <QPixmap>
#include <QString>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

namespace chatterino {

class Image;

}  // namespace chatterino

namespace chatterino::detail {

struct Frame {
    QPixmap image;
    int duration;
};

class Frames
{
public:
    Frames();
    /// With `oneShot`, the frames play once (from the first one, as soon as
    /// they are loaded) and the last one stays.
    /// With `loopFrom` (not -1), the frames play from the first one, as soon
    /// as they are loaded, and after the last one playback continues at
    /// frame `loopFrom` for good.
    Frames(QList<Frame> &&frames, bool oneShot = false,
           qsizetype loopFrom = -1);
    ~Frames();

    Frames(const Frames &) = delete;
    Frames &operator=(const Frames &) = delete;

    Frames(Frames &&) = delete;
    Frames &operator=(Frames &&) = delete;

    void clear();
    bool empty() const;
    bool animated() const;
    std::optional<QPixmap> current() const;
    /// Returns the size of the first frame, if one has been loaded.
    std::optional<QSize> frameSize() const;

private:
    struct Storage;
    struct CachedFrames;

    int64_t memoryUsage() const;
    std::unique_ptr<Storage> storage_;
};

QList<Frame> readFrames(QImageReader &reader, const Url &url,
                        std::optional<QSize> rescale = std::nullopt);
void assignFrames(std::weak_ptr<Image> weak, QList<Frame> parsed,
                  bool oneShot = false, qsizetype loopFrom = -1);

}  // namespace chatterino::detail

namespace chatterino {

class Image;
using ImagePtr = std::shared_ptr<Image>;

/// This class is thread safe.
class Image : public std::enable_shared_from_this<Image>
{
public:
    // Maximum amount of RAM used by the image in bytes.
    static constexpr int maxBytesRam = 20 * 1024 * 1024;

    ~Image();

    Image(const Image &) = delete;
    Image &operator=(const Image &) = delete;

    Image(Image &&) = delete;
    Image &operator=(Image &&) = delete;

    static ImagePtr fromUrl(const Url &url, qreal scale = 1,
                            QSize expectedSize = {});
    static ImagePtr fromResourcePixmap(const QPixmap &pixmap, qreal scale = 1);
    static ImagePtr getEmpty();

    static ImagePtr fromAutoscaledUrl(const Url &url, uint16_t autoScale);

    /// An image that shows the frames `spec` describes, cut out of the sheet
    /// at `url` (one image holding many frames side by side, or several
    /// variants of an emote). The sheet is downloaded and decoded once per
    /// `Image`, and the frames are cut out of it and animated like the frames
    /// of a GIF. Images with the same URL and spec are shared.
    ///
    /// For a static sprite sheet only its first source frame is used. When
    /// `spec` describes one frame at (0, 0) covering the entire source image,
    /// animated formats such as GIF remain animated and CSS colour filters
    /// are applied to each native frame.
    static ImagePtr fromSpriteSheet(const Url &url, const SpriteSheetSpec &spec);

    const Url &url() const;
    bool loaded() const;
    // either returns the current pixmap, or triggers loading it (lazy loading)
    std::optional<QPixmap> pixmapOrLoad() const;
    void load() const;
    qreal scale() const;
    bool isEmpty() const;
    int width() const;
    int height() const;
    QSizeF size() const;
    bool animated() const;

    bool operator==(const Image &image) = delete;

private:
    Image();
    Image(const Url &url, qreal scale, QSize expectedSize);
    Image(qreal scale);
    Image(const Url &url, SpriteSheetSpec spec);

    void setPixmap(const QPixmap &pixmap);
    void actuallyLoad();
    void expireFrames();

    const Url url_{};
    qreal scale_{1};
    /// @brief The expected size of this image once its loaded.
    ///
    /// This doesn't represent the actual size (it can be different) - it's
    /// just an estimation and provided to avoid (large) layout shifts when
    /// loading images.
    const QSize expectedSize_{16, 16};
    std::atomic_bool empty_{false};

    bool shouldLoad_{false};

    /// Size this image should take when loaded (in both dimensions).
    ///
    /// This is used for images that have an unknown scale when they're created
    /// (i.e. the scale is only known after the image is loaded).
    ///
    /// Upon creation, only `expectedSize_` is set to `(autoScale, autoScale)`.
    /// When the image is loaded, `scale_` is set to `autoScale / actualSize`.
    std::optional<uint16_t> autoScale_;

    mutable std::chrono::time_point<std::chrono::steady_clock> lastUsed_;

    // gui thread only
    std::unique_ptr<detail::Frames> frames_;

    /// If set, `url_` is a sprite sheet that is cut into frames on load.
    std::optional<SpriteSheetSpec> sprite_;

    friend class ImageExpirationPool;
    friend void detail::assignFrames(std::weak_ptr<Image>,
                                     QList<detail::Frame>, bool, qsizetype);
};

// forward-declarable function that calls Image::getEmpty() under the hood.
ImagePtr getEmptyImagePtr();

#ifndef DISABLE_IMAGE_EXPIRATION_POOL

class ImageExpirationPool
{
public:
    ImageExpirationPool();
    static ImageExpirationPool &instance();

    void addImagePtr(ImagePtr imgPtr);
    void removeImagePtr(Image *rawPtr);

    /**
     * @brief Frees frame data for all images that ImagePool deems to have expired.
     * 
     * Expiration is based on last accessed time of the Image, stored in Image::lastUsed_.
     * Must be ran in the GUI thread.
     */
    void freeOld();

    /*
     * Debug function that unloads all images in the pool. This is intended to
     * test for possible memory leaks from tracked images.
     */
    void freeAll();

    // Timer to periodically run freeOld()
    QTimer *freeTimer_;
    std::map<Image *, std::weak_ptr<Image>> allImages_;
    std::mutex mutex_;
};

#endif

}  // namespace chatterino
