// SPDX-FileCopyrightText: 2017 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "messages/Image.hpp"

#include "Application.hpp"
#include "common/Common.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "controllers/emotes/EmoteController.hpp"
#include "debug/AssertInGuiThread.hpp"
#include "debug/Benchmark.hpp"
#include "singletons/helper/GifTimer.hpp"
#include "singletons/WindowManager.hpp"
#include "util/DebugCount.hpp"
#include "util/Helpers.hpp"
#include "util/PostToThread.hpp"

#include <boost/functional/hash.hpp>
#include <pajlada/signals/scoped-connection.hpp>
#include <QBuffer>
#include <QFile>
#include <QImageReader>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <numeric>
#include <limits>
#include <utility>

// Duration between each check of every Image instance
const auto IMAGE_POOL_CLEANUP_INTERVAL = std::chrono::minutes(1);
// Duration since last usage of Image pixmap before expiration of frames
const auto IMAGE_POOL_IMAGE_LIFETIME = std::chrono::minutes(10);

namespace {

/// A sprite animation that plays this often or more is cut off here (the frames
/// are kept in a list, once per play).
constexpr int MAX_SPRITE_PLAYS = 50;

using namespace chatterino;

std::pair<QSize, bool> limitedAutoScaleSize(
    QSize original, std::optional<uint16_t> optAutoScale)
{
    if (!optAutoScale)
    {
        return {original, false};
    }
    // 2x ought to be enough for everyone.
    int maxAutoScale = *optAutoScale * 2;
    int maxDim = std::max({original.width(), original.height(), 1});
    if (maxDim <= maxAutoScale)
    {
        return {original, false};
    }
    return {
        original.scaled({maxAutoScale, maxAutoScale},
                        Qt::KeepAspectRatioByExpanding),
        true,
    };
}

}  // namespace

namespace chatterino::detail {

struct Frames::Storage {
    Storage() = default;
    virtual ~Storage() = default;
    Storage(const Storage &) = delete;
    Storage &operator=(const Storage &) = delete;
    Storage(Storage &&) = delete;
    Storage &operator=(Storage &&) = delete;

    virtual int64_t memoryUsage() const = 0;
    virtual bool empty() const = 0;
    virtual bool animated() const = 0;
    virtual void start(GIFTimer *timer) = 0;
    virtual std::optional<QPixmap> current() const = 0;
    virtual std::optional<QSize> frameSize() const = 0;

    pajlada::Signals::ScopedConnection gifTimerConnection;
};

/// Stores all decoded frames in memory.
struct Frames::CachedFrames : Storage {
    CachedFrames() = default;
    CachedFrames(QList<Frame> frames, bool oneShot)
        : items(std::move(frames))
        , oneShot(oneShot)
    {
    }
    ~CachedFrames() override = default;
    CachedFrames(const CachedFrames &) = delete;
    CachedFrames &operator=(const CachedFrames &) = delete;
    CachedFrames(CachedFrames &&) = delete;
    CachedFrames &operator=(CachedFrames &&) = delete;

    int64_t memoryUsage() const override
    {
        int64_t usage = 0;
        for (const auto &frame : this->items)
        {
            auto sz = frame.image.size();
            auto area = sz.width() * sz.height();
            auto memory = area * frame.image.depth() / 8;
            usage += memory;
        }
        return usage;
    }

    bool empty() const override
    {
        return this->items.empty();
    }

    bool animated() const override
    {
        return this->items.size() > 1;
    }

    void start(GIFTimer *timer) override
    {
        if (!this->animated())
        {
            return;
        }

        this->gifTimerConnection = timer->signal.connect([this] {
            this->advance();
        });

        if (this->oneShot)
        {
            // starts at the first frame, whatever the clock says
            this->index = 0;
            this->durationOffset = 0;
            return;
        }

        const auto totalLength =
            std::accumulate(this->items.begin(), this->items.end(), 0UL,
                            [](auto init, auto &&frame) {
                                return init + frame.duration;
                            });
        if (totalLength == 0)
        {
            this->durationOffset = 0;
        }
        else
        {
            this->durationOffset =
                std::min<int>(int(timer->position() % totalLength), 60000);
        }
        this->processOffset();
    }

    void advance()
    {
        this->durationOffset += GIF_FRAME_LENGTH;
        this->processOffset();
    }

    void processOffset()
    {
        if (this->items.isEmpty())
        {
            return;
        }

        if (this->oneShot)
        {
            // never wraps: the last frame stays
            while (this->index + 1 < this->items.size() &&
                   this->durationOffset > this->items.at(this->index).duration)
            {
                this->durationOffset -= this->items.at(this->index).duration;
                this->index++;
            }
            if (this->index + 1 >= this->items.size())
            {
                // resting; keeps the offset from growing for ever
                this->durationOffset = 0;
            }
            return;
        }

        while (true)
        {
            this->index %= this->items.size();
            if (this->durationOffset > this->items.at(this->index).duration)
            {
                this->durationOffset -= this->items.at(this->index).duration;
                this->index = (this->index + 1) % this->items.size();
            }
            else
            {
                break;
            }
        }
    }

    std::optional<QPixmap> current() const override
    {
        if (this->empty())
        {
            return std::nullopt;
        }
        return this->items[this->index].image;
    }

    std::optional<QSize> frameSize() const override
    {
        if (this->empty())
        {
            return std::nullopt;
        }
        return this->items.front().image.size();
    }

    QList<Frame> items;
    QList<Frame>::size_type index{0};
    int durationOffset{0};

    /// Plays once and stays on the last frame
    bool oneShot = false;
};

Frames::Frames()
    : storage_(std::make_unique<CachedFrames>(QList<Frame>{}, false))
{
    DebugCount::increase(DebugObject::Image);
}

Frames::Frames(QList<Frame> &&frames, bool oneShot)
    : storage_(std::make_unique<CachedFrames>(std::move(frames), oneShot))
{
    assertInGuiThread();
    auto *app = tryGetApp();
    if (app == nullptr)
    {
        qCDebug(chatterinoImage)
            << "Frames constructor called while app is shutting down";
        return;
    }

    DebugCount::increase(DebugObject::Image);
    if (!this->empty())
    {
        DebugCount::increase(DebugObject::LoadedImage);
    }

    if (this->animated())
    {
        DebugCount::increase(DebugObject::AnimatedImage);

        this->storage_->start(app->getEmotes()->getGIFTimer());
    }

    DebugCount::increase(DebugObject::BytesImageCurrent, this->memoryUsage());
    DebugCount::increase(DebugObject::BytesImageLoaded, this->memoryUsage());
}

Frames::~Frames()
{
    assertInGuiThread();
    DebugCount::decrease(DebugObject::Image);
    if (!this->empty())
    {
        DebugCount::decrease(DebugObject::LoadedImage);
    }

    if (this->animated())
    {
        DebugCount::decrease(DebugObject::AnimatedImage);
    }
    DebugCount::decrease(DebugObject::BytesImageCurrent, this->memoryUsage());
    DebugCount::increase(DebugObject::BytesImageUnloaded, this->memoryUsage());
}

int64_t Frames::memoryUsage() const
{
    return this->storage_->memoryUsage();
}

void Frames::clear()
{
    assertInGuiThread();
    if (!this->empty())
    {
        DebugCount::decrease(DebugObject::LoadedImage);
    }
    DebugCount::decrease(DebugObject::BytesImageCurrent, this->memoryUsage());
    DebugCount::increase(DebugObject::BytesImageUnloaded, this->memoryUsage());

    this->storage_ = std::make_unique<CachedFrames>(QList<Frame>{}, false);
}

bool Frames::empty() const
{
    return this->storage_->empty();
}

bool Frames::animated() const
{
    return this->storage_->animated();
}

std::optional<QPixmap> Frames::current() const
{
    return this->storage_->current();
}

std::optional<QSize> Frames::frameSize() const
{
    return this->storage_->frameSize();
}

QList<Frame> readFrames(QImageReader &reader, const Url &url,
                        std::optional<QSize> rescale,
                        qint64 maxBytes = std::numeric_limits<qint64>::max())
{
    QList<Frame> frames;
    const int reportedCount = reader.imageCount();
    const bool supportsAnimation = reader.supportsAnimation();
    // Some image handlers cannot report their frame count up front. In that
    // case, keep reading while the handler says another frame is available.
    // A static image with no count is still one frame.
    const int frameLimit = reportedCount > 0
                               ? reportedCount
                               : (supportsAnimation ? 512 : 1);
    qint64 decodedBytes = 0;

    for (int index = 0; index < frameLimit && reader.canRead(); ++index)
    {
        QImage image = reader.read();
        if (image.isNull())
        {
            break;
        }

        // It seems that browsers have special logic for fast animations.
        // This implements Chrome and Firefox's behavior which uses a duration
        // of 100 ms for any frames that specify a duration of <= 10 ms.
        // See http://webkit.org/b/36082 for more information.
        // https://github.com/SevenTV/chatterino7/issues/46#issuecomment-1010595231
        int duration = reader.nextImageDelay();
        if (duration <= 10)
        {
            duration = 100;
        }
        duration = std::max(20, duration);

        if (rescale)
        {
            assert(!isGuiThread());
            // We're not on the main thread - put some effort into scaling.
            image = image.scaled(*rescale, Qt::IgnoreAspectRatio,
                                 Qt::SmoothTransformation);
        }

        const qint64 frameBytes = static_cast<qint64>(image.width()) *
                                  static_cast<qint64>(image.height()) * 4;
        if (decodedBytes + frameBytes > maxBytes && !frames.empty())
        {
            qCDebug(chatterinoImage)
                << "Stopped decoding animation at the image memory limit for"
                << url.string << "after" << frames.size() << "frames";
            break;
        }
        decodedBytes += frameBytes;
        frames.append(Frame{
            .image = QPixmap::fromImage(std::move(image)),
            .duration = duration,
        });
    }

    if (frames.empty())
    {
        qCDebug(chatterinoImage) << "Error while reading image" << url.string
                                 << ": '" << reader.errorString() << "'";
    }
    else if (supportsAnimation || frames.size() > 1)
    {
        qCDebug(chatterinoImage)
            << "Decoded animated image" << url.string << "as" << frames.size()
            << "frames (reported" << reportedCount << ")";
    }

    return frames;
}

void assignFrames(std::weak_ptr<Image> weak, QList<Frame> parsed, bool oneShot)
{
    static bool isPushQueued;

    auto cb = [parsed = std::move(parsed), weak = std::move(weak),
               oneShot]() mutable {
        auto shared = weak.lock();
        if (!shared)
        {
            return;
        }
        shared->frames_ =
            std::make_unique<detail::Frames>(std::move(parsed), oneShot);
        if (shared->autoScale_)
        {
            // FIXME: We should actually scale the pixmaps. However, we'd also
            //        need to cache that.
            auto frameSize = shared->frames_->frameSize();
            if (frameSize)
            {
                shared->scale_ =
                    static_cast<qreal>(*shared->autoScale_) /
                    std::max(std::min(frameSize->width(), frameSize->height()),
                             1);
            }
        }

        // Avoid too many layouts in one event-loop iteration
        //
        // This callback is called for every image, so there might be multiple
        // callbacks queued on the event-loop in this iteration, but we only
        // want to generate one invalidation.
        if (!isPushQueued)
        {
            isPushQueued = true;
            // We don't use postToThread here, because that would run immediately.
            // We explicitly want to queue a callback after the current ones.
            QMetaObject::invokeMethod(
                qApp,
                [] {
                    isPushQueued = false;
                    auto *app = tryGetApp();
                    if (app != nullptr)
                    {
                        app->getWindows()->forceLayoutChannelViews();
                    }
                },
                Qt::QueuedConnection);
        }
    };

    postToGuiThread(cb);
}

}  // namespace chatterino::detail

namespace chatterino {

// IMAGE2
Image::~Image()
{
#ifndef DISABLE_IMAGE_EXPIRATION_POOL
    ImageExpirationPool::instance().removeImagePtr(this);
#endif

    if (this->empty_ && !this->frames_)
    {
        // No data in this image, don't bother trying to release it
        // The reason we do this check is that we keep a few (or one) static empty image around that are deconstructed at the end of the programs lifecycle, and we want to prevent the isGuiThread call to be called after the QApplication has been exited
        return;
    }

    if (isAppAboutToQuit())
    {
        if (this->frames_)
        {
            std::ignore = this->frames_.release();
        }
        return;
    }

    // Ensure the destructor for our frames is called in the GUI thread
    // If the Image destructor is called outside of the GUI thread, move the
    // ownership of the frames to the GUI thread, otherwise the frames will be
    // destructed as part as we go out of scope
    if (!isGuiThread())
    {
        postToThread([frames = this->frames_.release()]() {
            delete frames;
        });
    }
}

ImagePtr Image::fromUrl(const Url &url, qreal scale, QSize expectedSize)
{
    static std::unordered_map<Url, std::weak_ptr<Image>> cache;
    static std::mutex mutex;

    std::lock_guard<std::mutex> lock(mutex);

    auto shared = cache[url].lock();

    if (!shared)
    {
        cache[url] = shared = ImagePtr(new Image(url, scale, expectedSize));
    }

    return shared;
}

ImagePtr Image::fromSpriteSheet(const Url &url, const SpriteSheetSpec &spec)
{
    static std::unordered_map<std::string, std::weak_ptr<Image>> cache;
    static std::mutex mutex;

    std::lock_guard<std::mutex> lock(mutex);

    const auto key = (url.string + QChar(u'#') + spec.cacheKey()).toStdString();
    auto shared = cache[key].lock();

    if (!shared)
    {
        cache[key] = shared = ImagePtr(new Image(url, spec));
    }

    return shared;
}

ImagePtr Image::fromAutoscaledUrl(const Url &url, uint16_t autoScale)
{
    auto shared = Image::fromUrl(url, 1.0, {autoScale, autoScale});
    shared->autoScale_ = autoScale;

    return shared;
}

ImagePtr Image::fromResourcePixmap(const QPixmap &pixmap, qreal scale)
{
    using key_t = std::pair<const QPixmap *, qreal>;
    static std::unordered_map<key_t, std::weak_ptr<Image>, boost::hash<key_t>>
        cache;
    static std::mutex mutex;

    std::lock_guard<std::mutex> lock(mutex);

    auto it = cache.find({&pixmap, scale});
    if (it != cache.end())
    {
        auto shared = it->second.lock();
        if (shared)
        {
            return shared;
        }

        cache.erase(it);
    }

    auto newImage = ImagePtr(new Image(scale));

    newImage->setPixmap(pixmap);

    // store in cache
    cache.insert({{&pixmap, scale}, std::weak_ptr<Image>(newImage)});

    return newImage;
}

ImagePtr Image::getEmpty()
{
    static auto empty = ImagePtr(new Image);
    return empty;
}

ImagePtr getEmptyImagePtr()
{
    return Image::getEmpty();
}

Image::Image()
    : empty_(true)
{
}

Image::Image(const Url &url, qreal scale, QSize expectedSize)
    : url_(url)
    , scale_(scale)
    , expectedSize_(expectedSize.isValid() ? expectedSize
                                           : (QSize(16, 16) * scale))
    , shouldLoad_(true)
    , frames_(std::make_unique<detail::Frames>())
{
}

Image::Image(qreal scale)
    : scale_(scale)
    , frames_(std::make_unique<detail::Frames>())
{
}

Image::Image(const Url &url, SpriteSheetSpec spec)
    : url_(url)
    , scale_(1)
    , expectedSize_(spec.frameSize)
    , shouldLoad_(true)
    , frames_(std::make_unique<detail::Frames>())
    , sprite_(std::move(spec))
{
}

void Image::setPixmap(const QPixmap &pixmap)
{
    auto setFrames = [shared = this->shared_from_this(), pixmap]() {
        shared->frames_ = std::make_unique<detail::Frames>(
            QList<detail::Frame>{detail::Frame{pixmap, 1}});
    };

    if (isGuiThread())
    {
        setFrames();
    }
    else
    {
        postToThread(setFrames);
    }
}

const Url &Image::url() const
{
    return this->url_;
}

bool Image::loaded() const
{
    assertInGuiThread();

    if (!this->frames_)
    {
        return false;
    }

    return this->frames_->current().has_value();
}

std::optional<QPixmap> Image::pixmapOrLoad() const
{
    assertInGuiThread();

    if (!this->frames_)
    {
        return std::nullopt;
    }

    // Mark the image as just used.
    // Any time this Image is painted, this method is invoked.
    // See src/messages/layouts/MessageLayoutElement.cpp ImageLayoutElement::paint, for example.
    this->lastUsed_ = std::chrono::steady_clock::now();

    this->load();

    return this->frames_->current();
}

void Image::load() const
{
    assertInGuiThread();

    if (this->shouldLoad_)
    {
        Image *this2 = const_cast<Image *>(this);
        this2->shouldLoad_ = false;
        this2->actuallyLoad();
#ifndef DISABLE_IMAGE_EXPIRATION_POOL
        ImageExpirationPool::instance().addImagePtr(this2->shared_from_this());
#endif
    }
}

qreal Image::scale() const
{
    return this->scale_;
}

bool Image::isEmpty() const
{
    return this->empty_;
}

bool Image::animated() const
{
    assertInGuiThread();

    if (!this->frames_)
    {
        return false;
    }

    return this->frames_->animated();
}

int Image::width() const
{
    assertInGuiThread();

    if (!this->frames_)
    {
        return 0;
    }

    if (auto size = this->frames_->frameSize())
    {
        return static_cast<int>(size->width() * this->scale_);
    }

    // No frames loaded, use the expected size
    return static_cast<int>(this->expectedSize_.width() * this->scale_);
}

int Image::height() const
{
    assertInGuiThread();

    if (!this->frames_)
    {
        return 0;
    }

    if (auto size = this->frames_->frameSize())
    {
        return static_cast<int>(size->height() * this->scale_);
    }

    // No frames loaded, use the expected size
    return static_cast<int>(this->expectedSize_.height() * this->scale_);
}

QSizeF Image::size() const
{
    assertInGuiThread();

    if (!this->frames_)
    {
        return {0, 0};
    }

    if (auto size = this->frames_->frameSize())
    {
        return size->toSizeF() * this->scale_;
    }

    // No frames loaded, use the expected size
    return this->expectedSize_.toSizeF() * this->scale_;
}

void Image::actuallyLoad()
{
    auto weak = weakOf(this);
    auto onSuccess = [weak](const auto &result) {
        auto shared = weak.lock();
        if (!shared)
        {
            return;
        }

        assert(!isAppAboutToQuit());

        QBuffer buffer;
        buffer.setData(result.getData());
        QImageReader reader(&buffer);

        if (!reader.canRead())
        {
            qCDebug(chatterinoImage)
                << "Error: image cant be read " << shared->url().string;
            shared->empty_ = true;
            return;
        }

        const auto size = reader.size();
        if (size.isEmpty())
        {
            shared->empty_ = true;
            return;
        }

        const int reportedFrameCount = reader.imageCount();
        const bool supportsAnimation = reader.supportsAnimation();
        qCDebug(chatterinoImage)
            << "Image decoder" << reader.format() << "size" << size
            << "frames" << reportedFrameCount << "supports animation"
            << supportsAnimation << "url" << shared->url().string;

        if (reportedFrameCount <= 0 && !reader.canRead())
        {
            qCDebug(chatterinoImage)
                << "Error: image has no readable frame " << shared->url().string
                << ": " << reader.errorString();
            shared->empty_ = true;
            return;
        }

        if (shared->sprite_)
        {
            const auto &spec = *shared->sprite_;
            // Some emotes are animated GIFs with CSS filters rather than
            // sprite sheets. In that case the CSS describes one frame at the
            // origin covering the entire image (mhm is the current example).
            // Keep every native animation frame and apply the filters to each
            // one; reading only the first frame here would silently freeze it.
            const bool fullImageFrame =
                spec.offsets.size() == 1 && spec.offsets.front() == QPoint(0, 0) &&
                spec.frameSize == size;
            if (supportsAnimation && fullImageFrame)
            {
                auto frames = detail::readFrames(
                    reader, shared->url(), std::nullopt, Image::maxBytesRam);
                if (!spec.filters.empty())
                {
                    for (auto &frame : frames)
                    {
                        frame.image = QPixmap::fromImage(
                            applySpriteFilters(frame.image.toImage(), spec.filters));
                    }
                }
                if (frames.empty())
                {
                    shared->empty_ = true;
                    return;
                }
                assignFrames(shared, std::move(frames));
                return;
            }

            // A static sprite sheet is decoded as a whole, and the frames are
            // cut out of it. That's done here (and not on the GUI thread)
            // because it can be a lot of pixels. For animated sprite-sheet
            // files, the first source frame is still the sheet we slice.
            const QImage sheet = reader.read();
            if (sheet.isNull())
            {
                qCDebug(chatterinoImage)
                    << "Error: sprite sheet can't be decoded"
                    << shared->url().string << ": " << reader.errorString();
                shared->empty_ = true;
                return;
            }

            auto sliced = sliceSpriteSheet(sheet, *shared->sprite_,
                                           Image::maxBytesRam);
            if (sliced.empty())
            {
                shared->empty_ = true;
                return;
            }

            // Reuse the sprite spec declared above in this lambda.
            const auto toFrame = [](SpriteFrame &frame) {
                return detail::Frame{
                    .image = QPixmap::fromImage(std::move(frame.image)),
                    .duration = frame.durationMs > 0 ? frame.durationMs : 100,
                };
            };

            // One more frame than there are offsets: the one a limited
            // animation rests on
            const bool oneShot =
                spec.playsOnce() && sliced.size() == spec.offsets.size() + 1;

            QList<detail::Frame> frames;
            if (oneShot)
            {
                // The pixmaps are shared, so playing the frames several
                // times costs a few pointers
                const int plays = std::min(spec.loopCount, MAX_SPRITE_PLAYS);
                const auto cycle = sliced.size() - 1;
                std::vector<detail::Frame> once;
                once.reserve(cycle);
                for (size_t i = 0; i < cycle; i++)
                {
                    once.push_back(toFrame(sliced[i]));
                }
                frames.reserve(static_cast<qsizetype>(cycle * static_cast<size_t>(plays) + 1));
                for (int play = 0; play < plays; play++)
                {
                    for (const auto &frame : once)
                    {
                        frames.append(frame);
                    }
                }
                auto rest = toFrame(sliced.back());
                // never left again
                rest.duration = 1000;
                frames.append(std::move(rest));
            }
            else
            {
                frames.reserve(static_cast<qsizetype>(sliced.size()));
                for (auto &frame : sliced)
                {
                    frames.append(toFrame(frame));
                }
            }

            qCDebug(chatterinoImage)
                << "Sprite sheet" << shared->url().string << "->"
                << sliced.size() << "frames, played"
                << (oneShot ? "a limited number of times" : "forever");

            assignFrames(shared, std::move(frames), oneShot);
            return;
        }

        auto [limitedSize, needsLimit] =
            limitedAutoScaleSize(size, shared->autoScale_);

        // use "double" to prevent int overflows
        const int estimatedFrameCount =
            reportedFrameCount > 0 ? reportedFrameCount : 1;
        if (double(limitedSize.width()) * double(limitedSize.height()) *
                double(estimatedFrameCount) * 4.0 >
            double(Image::maxBytesRam))
        {
            qCDebug(chatterinoImage) << "image too large in RAM";

            shared->empty_ = true;
            postToGuiThread([] {
                if (auto *app = tryGetApp())
                {
                    app->getWindows()->forceLayoutChannelViews();
                }
            });
            return;
        }

        auto parsed = detail::readFrames(
            reader, shared->url(),
            makeConditionedOptional(needsLimit, limitedSize), Image::maxBytesRam);

        assignFrames(shared, parsed);
    };
    auto onError = [weak](const auto & /*result*/) {
        auto shared = weak.lock();
        if (!shared)
        {
            return false;
        }

        // fourtf: is this the right thing to do?
        shared->empty_ = true;

        return true;
    };

    if (this->url_.string.startsWith(u":/"))
    {
        QThreadPool::globalInstance()->start([onSuccess = std::move(onSuccess),
                                              onError = std::move(onError),
                                              url = this->url_.string] {
            QByteArray data;
            {
                QFile file(url);
                if (!file.open(QFile::ReadOnly))
                {
                    onError(NetworkResult{
                        NetworkResult::NetworkError::ContentNotFoundError,
                        404,
                        {},
                    });
                    return;
                }
                data = file.readAll();
            }
            onSuccess(NetworkResult{
                NetworkResult::NetworkError::NoError,
                200,
                std::move(data),
            });
        });
    }
    else
    {
        NetworkRequest(this->url().string)
            .concurrent()
            .cache()
            .onSuccess(std::move(onSuccess))
            .onError(std::move(onError))
            .execute();
    }
}

void Image::expireFrames()
{
    assertInGuiThread();
    if (!this->frames_)
    {
        return;
    }

    this->frames_->clear();
    this->shouldLoad_ = true;  // Mark as needing load again
}

#ifndef DISABLE_IMAGE_EXPIRATION_POOL

ImageExpirationPool::ImageExpirationPool()
    : freeTimer_(new QTimer)
{
    QObject::connect(this->freeTimer_, &QTimer::timeout, [this] {
        if (isGuiThread())
        {
            this->freeOld();
        }
        else
        {
            postToThread([this] {
                this->freeOld();
            });
        }
    });

    this->freeTimer_->start(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            IMAGE_POOL_CLEANUP_INTERVAL));
}

ImageExpirationPool &ImageExpirationPool::instance()
{
    static auto *instance = new ImageExpirationPool;
    return *instance;
}

void ImageExpirationPool::addImagePtr(ImagePtr imgPtr)
{
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->allImages_.emplace(imgPtr.get(), std::weak_ptr<Image>(imgPtr));
}

void ImageExpirationPool::removeImagePtr(Image *rawPtr)
{
    std::lock_guard<std::mutex> lock(this->mutex_);
    this->allImages_.erase(rawPtr);
}

void ImageExpirationPool::freeAll()
{
    {
        std::lock_guard<std::mutex> lock(this->mutex_);
        for (auto it = this->allImages_.begin(); it != this->allImages_.end();)
        {
            auto img = it->second.lock();
            img->expireFrames();
            it = this->allImages_.erase(it);
        }
    }
    this->freeOld();
}

void ImageExpirationPool::freeOld()
{
    std::lock_guard<std::mutex> lock(this->mutex_);

    size_t numExpired = 0;
    size_t eligible = 0;

    auto now = std::chrono::steady_clock::now();
    for (auto it = this->allImages_.begin(); it != this->allImages_.end();)
    {
        auto img = it->second.lock();
        if (!img)
        {
            // This can only really happen from a race condition because ~Image
            // should remove itself from the ImageExpirationPool automatically.
            it = this->allImages_.erase(it);
            continue;
        }

        if (img->frames_->empty())
        {
            // No frame data, nothing to do
            ++it;
            continue;
        }

        ++eligible;

        // Check if image has expired and, if so, expire its frame data
        auto diff = now - img->lastUsed_;
        if (diff > IMAGE_POOL_IMAGE_LIFETIME)
        {
            ++numExpired;
            img->expireFrames();
            // erase without mutex locking issue
            it = this->allImages_.erase(it);
            continue;
        }

        ++it;
    }

#    ifndef NDEBUG
    qCDebug(chatterinoImage) << "freed frame data for" << numExpired << "/"
                             << eligible << "eligible images";
#    endif
    DebugCount::set(DebugObject::LastImageGcExpired, numExpired);
    DebugCount::set(DebugObject::LastImageGcEligible, eligible);
    DebugCount::set(DebugObject::LastImageGcLeft, this->allImages_.size());
}

#endif

}  // namespace chatterino
