#include "providers/dgg/DggAssets.hpp"

#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "debug/AssertInGuiThread.hpp"
#include "messages/Image.hpp"
#include "messages/ImageSet.hpp"
#include "providers/dgg/DggMessage.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QSize>
#include <QUrl>

#include <algorithm>
#include <set>
#include <utility>

namespace {

using namespace chatterino;
using namespace Qt::Literals;

/// DGG moved newer assets to `r2cdn.destiny.gg` (that's where flair127 lives,
/// for example), and the older host can serve stale lists. Ask the new host
/// first and fall back to the old one.
constexpr auto NEW_CDN = "https://r2cdn.destiny.gg/"_L1;
constexpr auto OLD_CDN = "https://cdn.destiny.gg/"_L1;

std::vector<QString> cdnUrls(const QString &path)
{
    // chat-gui adds `?_=<cache key>` to get around the CDN cache
    const auto stamp = QDateTime::currentSecsSinceEpoch() / 60;
    return {
        QString(NEW_CDN) + path + u"?_="_s + QString::number(stamp),
        QString(OLD_CDN) + path,
    };
}

QString directoryOf(const QString &url)
{
    QString clean = url;
    if (const auto q = clean.indexOf(u'?'); q >= 0)
    {
        clean.truncate(q);
    }
    return clean.left(clean.lastIndexOf(u'/') + 1);
}

constexpr int REQUEST_TIMEOUT_MS = 15000;

/// DGG has a single resolution per image, so there's no 2x/3x set. The image
/// is drawn at the size the manifest gives (scale 1). `Image::fromUrl` caches
/// by URL, and the URLs contain the asset version, so a new manifest version
/// never shows a stale image.
QSize sheetSizeOf(const DggImageRef &ref)
{
    if (ref.width > 0 && ref.height > 0)
    {
        return QSize(ref.width, ref.height);
    }
    return {};
}

/// `style` is what the stylesheet says about the emote. If it shows a part of
/// the image (a frame of a sprite sheet, or one variant of several) or
/// colours it, the image is a sprite sheet that is cut into frames. Otherwise,
/// the image is shown as it is (which keeps the animation of a GIF).
ImageSet imageSetFor(const DggImageRef &ref, const DggEmoteStyle &style)
{
    const QSize size = sheetSizeOf(ref);
    if (style.isScaledWholeImage())
    {
        // `background-size`: the whole (possibly animated) image, drawn at
        // another size. A square target is the common case (BlueWave: 32px).
        const auto target = style.backgroundSize;
        if (target.width() == target.height())
        {
            return ImageSet(Image::fromAutoscaledUrl(
                Url{ref.url}, static_cast<uint16_t>(target.width())));
        }
        if (!size.isEmpty())
        {
            return ImageSet(Image::fromUrl(
                Url{ref.url},
                static_cast<qreal>(target.width()) / size.width(), size));
        }
    }
    if (style.valid && !style.isWholeImage(size))
    {
        return ImageSet(Image::fromSpriteSheet(Url{ref.url}, style.sprite));
    }
    return ImageSet(Image::fromUrl(Url{ref.url}, 1.0, size));
}

EmotePtr makeEmote(const DggEmoteDef &def, const DggEmoteStyle &style)
{
    QString tooltip = def.prefix.toHtmlEscaped() + u"<br>DGG Emote"_s;
    if (def.patreonOnly)
    {
        tooltip += u"<br>Patreon only"_s;
    }
    else if (def.minimumSubTier > 0)
    {
        tooltip += u"<br>Subscriber tier %1+"_s.arg(def.minimumSubTier);
    }
    if (!def.creator.isEmpty())
    {
        tooltip += u"<br>By: "_s + def.creator.toHtmlEscaped();
    }

    return std::make_shared<const Emote>(Emote{
        .name = EmoteName{def.prefix},
        .images = imageSetFor(def.image, style),
        .tooltip = Tooltip{tooltip},
        .author = EmoteAuthor{def.creator},
    });
}

EmotePtr makeBadge(const DggFlairDef &def)
{
    QString tooltip = def.label.toHtmlEscaped();
    if (!def.description.isEmpty())
    {
        tooltip += u"<br>"_s + def.description.toHtmlEscaped();
    }

    return std::make_shared<const Emote>(Emote{
        .name = EmoteName{def.label},
        .images = imageSetFor(def.image, {}),
        .tooltip = Tooltip{tooltip},
    });
}

}  // namespace

namespace chatterino {

DggAssets::DggAssets()
    : alive_(std::make_shared<bool>(true))
{
}

DggAssets::~DggAssets() = default;

void DggAssets::load()
{
    assertInGuiThread();

    if (this->loading_)
    {
        return;
    }
    this->loading_ = true;
    this->pending_ = 4;

    this->fetch(cdnUrls(QStringLiteral("emotes/emotes.json")),
                [this](const QJsonArray &json) {
                    this->applyEmotes(json);
                });
    this->fetch(cdnUrls(QStringLiteral("flairs/flairs.json")),
                [this](const QJsonArray &json) {
                    this->applyFlairs(json);
                });
    this->fetchText(cdnUrls(QStringLiteral("emotes/emotes.css")),
                    [this](const QString &css, const QString &) {
                        this->applyCss(css);
                    });
    this->fetchText(cdnUrls(QStringLiteral("flairs/flairs.css")),
                    [this](const QString &css, const QString &url) {
                        this->applyFlairCss(css, url);
                    });
}

namespace {

void fetchFirst(std::shared_ptr<const std::vector<QString>> urls, size_t index,
                std::weak_ptr<bool> weak,
                std::function<void(NetworkResult, const QString &)> onData,
                std::function<void()> done)
{
    const QString url = (*urls)[index];

    NetworkRequest(QUrl(url))
        .timeout(REQUEST_TIMEOUT_MS)
        .onSuccess([weak, url, onData, done](NetworkResult result) {
            if (weak.expired())
            {
                return;
            }
            onData(std::move(result), url);
            done();
        })
        .onError([weak, urls, index, url, onData, done](NetworkResult result) {
            if (weak.expired())
            {
                return;
            }
            qCWarning(chatterinoDgg)
                << "Failed to load" << url << result.formatError();
            if (index + 1 < urls->size())
            {
                fetchFirst(urls, index + 1, weak, onData, done);
                return;
            }
            done();
        })
        .execute();
}

}  // namespace

void DggAssets::fetchText(
    std::vector<QString> urls,
    std::function<void(const QString &, const QString &)> onData)
{
    fetchFirst(
        std::make_shared<const std::vector<QString>>(std::move(urls)), 0,
        this->alive_,
        [onData = std::move(onData)](NetworkResult result, const QString &url) {
            onData(QString::fromUtf8(result.getData()), url);
        },
        [this] {
            this->onFetchFinished();
        });
}

void DggAssets::fetch(std::vector<QString> urls,
                      std::function<void(const QJsonArray &)> onData)
{
    fetchFirst(
        std::make_shared<const std::vector<QString>>(std::move(urls)), 0,
        this->alive_,
        [onData = std::move(onData)](NetworkResult result, const QString &) {
            onData(result.parseJsonArray());
        },
        [this] {
            this->onFetchFinished();
        });
}

void DggAssets::onFetchFinished()
{
    this->pending_--;
    if (this->pending_ > 0)
    {
        return;
    }

    this->loading_ = false;
    if (!this->ready_)
    {
        this->ready_ = true;
        this->ready.invoke();
    }
}

void DggAssets::applyEmotes(const QJsonArray &json)
{
    auto defs = parseDggEmotes(json);
    if (defs.empty())
    {
        // An unreadable or empty answer keeps what we had.
        qCWarning(chatterinoDgg) << "The emote manifest has no emotes";
        return;
    }

    StringMap<EmoteEntry> emotes;
    emotes.reserve(defs.size());
    for (auto &def : defs)
    {
        if (!def.image.isValid())
        {
            continue;
        }
        auto prefix = def.prefix;
        emotes.emplace(std::move(prefix), EmoteEntry{.def = std::move(def)});
    }

    qCDebug(chatterinoDgg) << "Loaded" << emotes.size() << "emotes";
    this->emotes_ = std::move(emotes);

    // The emotes look different with and without the stylesheet, and it may
    // have arrived first.
    this->rebuildEmotes();
}

void DggAssets::applyCss(const QString &css)
{
    auto parsed = DggEmoteCss::parse(css);
    if (parsed.isEmpty())
    {
        // Keep what we had, like for the manifests.
        qCWarning(chatterinoDgg) << "The emote stylesheet has no usable rules";
        return;
    }

    const auto &stats = parsed.stats();
    qCDebug(chatterinoDgg) << "Loaded the emote stylesheet:" << stats.rules
                           << "rules for" << stats.emotes << "emotes,"
                           << stats.keyframes << "keyframes,"
                           << stats.unsupportedRules << "rules skipped";
    for (const auto &reason : parsed.describeUnsupported())
    {
        qCDebug(chatterinoDgg) << "  skipped:" << reason;
    }

    this->css_ = std::move(parsed);
    this->rebuildEmotes();
}

void DggAssets::rebuildEmotes()
{
    this->styled_.clear();

    for (auto &item : this->emotes_)
    {
        auto &entry = item.second;

        DggEmoteStyle style;
        entry.contextual = false;
        if (this->css_.hasEmote(entry.def.prefix))
        {
            style = this->css_.resolve(entry.def.prefix, {},
                                       sheetSizeOf(entry.def.image));
            entry.contextual = this->css_.dependsOnContext(entry.def.prefix);
        }
        entry.emote = makeEmote(entry.def, style);
        entry.style = std::move(style);
    }
}

DggEmoteStyle DggAssets::withOwnPlay(DggEmoteStyle style) const
{
    style.sprite.instanceId = ++this->nextPlayId_;
    return style;
}

void DggAssets::applyFlairs(const QJsonArray &json)
{
    auto defs = parseDggFlairs(json);
    if (defs.empty())
    {
        qCWarning(chatterinoDgg) << "The flair manifest has no flairs";
        return;
    }

    qCDebug(chatterinoDgg) << "Loaded" << defs.size() << "flairs";
    this->flairs_ = std::move(defs);
    this->rebuildBadges();
}

void DggAssets::applyFlairCss(const QString &css, const QString &sourceUrl)
{
    if (css.trimmed().isEmpty())
    {
        // Keep what we had, like for the manifests.
        qCWarning(chatterinoDgg) << "The flair stylesheet is empty";
        return;
    }

    // Parsed in rebuildBadges: the image URLs in it are relative, and which
    // directory they belong to is only known once the manifest has arrived.
    this->flairCss_ = css;
    this->flairCssBase_ = directoryOf(sourceUrl);
    this->rebuildBadges();
}

void DggAssets::rebuildBadges()
{
    // The stylesheet's `url("abc.png")` is relative to the directory it was
    // served from (https://r2cdn.destiny.gg/flairs/ for flair127). The old
    // host keeps files in a versioned directory instead, so when the
    // stylesheet only came from there, borrow that directory from the manifest.
    QString cssBase = this->flairCssBase_;
    if (!cssBase.startsWith(QString(NEW_CDN)))
    {
        for (const auto &def : this->flairs_)
        {
            const auto &url = def.image.url;
            const auto slash = url.lastIndexOf(u'/');
            if (def.image.isValid() && slash > 0)
            {
                cssBase = url.left(slash + 1);
                break;
            }
        }
    }
    if (cssBase.isEmpty())
    {
        cssBase = QString(NEW_CDN) + u"flairs/"_s;
    }
    this->cssFlairs_.clear();
    if (!this->flairCss_.isEmpty())
    {
        this->cssFlairs_ = parseDggFlairCss(this->flairCss_, cssBase);
        qCDebug(chatterinoDgg) << "Loaded" << this->cssFlairs_.size()
                               << "flair images from the stylesheet, base"
                               << cssBase;
    }

    // The stylesheet is the main source for badge images and their order.
    // The manifest only fills in flairs the stylesheet has no image for
    // (its images are the same ones in most cases).
    StringMap<EmotePtr> badges;
    StringMap<int> orders;
    for (const auto &def : this->cssFlairs_)
    {
        orders.emplace(def.name, def.order);
        if (!def.image.isValid())
        {
            continue;
        }
        qCDebug(chatterinoDgg) << "  flair" << def.name << "->" << def.image.url
                               << def.image.width << "x" << def.image.height;
        badges.emplace(def.name, makeBadge(def));
    }

    size_t fromManifest = 0;
    for (const auto &def : this->flairs_)
    {
        if (def.image.isValid() && badges.emplace(def.name, makeBadge(def)).second)
        {
            fromManifest++;
        }
    }
    qCDebug(chatterinoDgg) << badges.size() << "flair badges," << fromManifest
                           << "of them only from the manifest";

    this->badges_ = std::move(badges);
    this->badgeOrder_ = std::move(orders);
}

EmotePtr DggAssets::emoteFor(QStringView word, const DggUser &user) const
{
    return this->emoteFor(word, user,
                          this->emoteContextFor(user, false, false));
}

EmotePtr DggAssets::emoteFor(QStringView word, const DggUser &user,
                             const DggEmoteContext &context) const
{
    auto it = this->emotes_.find(word);
    if (it == this->emotes_.end())
    {
        return nullptr;
    }

    const auto &entry = it->second;
    if (!dggCanUseEmote(entry.def, user))
    {
        return nullptr;
    }
    if (!entry.contextual)
    {
        if (entry.style.valid && entry.style.sprite.playsOnce())
        {
            return makeEmote(entry.def, this->withOwnPlay(entry.style));
        }
        return entry.emote;
    }

    const auto style = this->css_.resolve(entry.def.prefix, context,
                                          sheetSizeOf(entry.def.image));
    if (style.valid && style.sprite.playsOnce())
    {
        return makeEmote(entry.def, this->withOwnPlay(style));
    }
    const QString key = entry.def.prefix + QChar(u'|') + style.cacheKey();
    if (auto cached = this->styled_.find(key); cached != this->styled_.end())
    {
        return cached->second;
    }

    auto emote = makeEmote(entry.def, style);
    this->styled_.emplace(key, emote);
    return emote;
}

DggEmoteContext DggAssets::emoteContextFor(const DggUser &user, bool isAction,
                                           bool backlog) const
{
    DggEmoteContext context;
    context.username = user.nick.toLower();
    context.messageClasses = user.features;
    if (isAction)
    {
        context.messageClasses.append(QStringLiteral("msg-me"));
    }
    if (backlog)
    {
        context.messageClasses.append(QStringLiteral("msg-historical"));
    }

    // The nick only gets the class of the flair that colours it
    if (const auto *flair = dggNickColorFlair(this->flairs_, user.features))
    {
        context.nickColorFlair = flair->name;
    }
    return context;
}

std::vector<EmotePtr> DggAssets::badgesFor(const DggUser &user) const
{
    std::vector<std::pair<int, EmotePtr>> found;
    for (const auto &feature : user.features)
    {
        auto it = this->badges_.find(feature);
        if (it == this->badges_.end())
        {
            static std::set<QString> reported;
            if (feature.startsWith(u"flair") && reported.insert(feature).second)
            {
                qCDebug(chatterinoDgg)
                    << "No badge image for" << feature
                    << "(not in flairs.css with an image, nor in flairs.json)";
            }
            continue;
        }
        int order = 0;
        if (auto o = this->badgeOrder_.find(feature);
            o != this->badgeOrder_.end())
        {
            order = o->second;
        }
        found.emplace_back(order, it->second);
    }

    // Stable, so equal orders keep the order of the features
    std::stable_sort(found.begin(), found.end(),
                     [](const auto &a, const auto &b) {
                         return a.first < b.first;
                     });

    std::vector<EmotePtr> out;
    out.reserve(found.size());
    for (auto &entry : found)
    {
        out.push_back(std::move(entry.second));
    }
    return out;
}

DggAssets::NickStyle DggAssets::nickStyleFor(const DggUser &user) const
{
    NickStyle style;

    const auto *flair = dggNickColorFlair(this->flairs_, user.features);
    if (flair == nullptr)
    {
        return style;
    }

    style.rainbow = flair->rainbowColor;
    if (!flair->color.isEmpty())
    {
        QColor color(flair->color);
        if (color.isValid())
        {
            style.color = color;
        }
    }
    return style;
}

}  // namespace chatterino
