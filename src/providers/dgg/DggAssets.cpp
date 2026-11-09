#include "providers/dgg/DggAssets.hpp"

#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "debug/AssertInGuiThread.hpp"
#include "messages/Image.hpp"
#include "messages/ImageSet.hpp"
#include "providers/dgg/DggMessage.hpp"

#include <QJsonArray>
#include <QSize>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace {

using namespace chatterino;
using namespace Qt::Literals;

constexpr auto EMOTES_URL = "https://cdn.destiny.gg/emotes/emotes.json"_L1;
constexpr auto FLAIRS_URL = "https://cdn.destiny.gg/flairs/flairs.json"_L1;

/// chat-gui loads it from the same place as the manifest.
constexpr auto EMOTES_CSS_URL = "https://cdn.destiny.gg/emotes/emotes.css"_L1;

/// The manifest doesn't list every flair that has an image here.
constexpr auto FLAIRS_CSS_URL = "https://cdn.destiny.gg/flairs/flairs.css"_L1;

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

    this->fetch(EMOTES_URL, [this](const QJsonArray &json) {
        this->applyEmotes(json);
    });
    this->fetch(FLAIRS_URL, [this](const QJsonArray &json) {
        this->applyFlairs(json);
    });
    this->fetchText(EMOTES_CSS_URL, [this](const QString &css) {
        this->applyCss(css);
    });
    this->fetchText(FLAIRS_CSS_URL, [this](const QString &css) {
        this->applyFlairCss(css);
    });
}

void DggAssets::fetchText(const QString &url,
                          std::function<void(const QString &)> onData)
{
    std::weak_ptr<bool> weak = this->alive_;

    NetworkRequest(QUrl(url))
        .timeout(REQUEST_TIMEOUT_MS)
        .onSuccess([weak, onData = std::move(onData)](NetworkResult result) {
            if (weak.expired())
            {
                return;
            }
            onData(QString::fromUtf8(result.getData()));
        })
        .onError([weak, url](NetworkResult result) {
            if (weak.expired())
            {
                return;
            }
            qCWarning(chatterinoDgg)
                << "Failed to load" << url << result.formatError();
        })
        .finally([weak, this] {
            if (weak.expired())
            {
                return;
            }
            this->onFetchFinished();
        })
        .execute();
}

void DggAssets::fetch(const QString &url,
                      std::function<void(const QJsonArray &)> onData)
{
    std::weak_ptr<bool> weak = this->alive_;

    NetworkRequest(QUrl(url))
        .timeout(REQUEST_TIMEOUT_MS)
        .onSuccess([weak, onData = std::move(onData)](NetworkResult result) {
            if (weak.expired())
            {
                return;
            }
            onData(result.parseJsonArray());
        })
        .onError([weak, url](NetworkResult result) {
            if (weak.expired())
            {
                return;
            }
            qCWarning(chatterinoDgg)
                << "Failed to load" << url << result.formatError();
        })
        .finally([weak, this] {
            if (weak.expired())
            {
                return;
            }
            this->onFetchFinished();
        })
        .execute();
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

void DggAssets::applyFlairCss(const QString &css)
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
    this->rebuildBadges();
}

void DggAssets::rebuildBadges()
{
    // The stylesheet's `url("abc.png")` is relative to a *versioned* directory
    // (the manifest says `https://cdn.destiny.gg/4.50.1/flairs/abc.png`).
    // Resolving against the unversioned stylesheet URL gives a 404, which is
    // why CSS-only flairs like flair127 showed nothing. Take the directory
    // from any image in the manifest.
    QString cssBase = FLAIRS_CSS_URL;
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
    this->cssFlairs_.clear();
    if (!this->flairCss_.isEmpty())
    {
        this->cssFlairs_ = parseDggFlairCss(this->flairCss_, cssBase);
        qCDebug(chatterinoDgg) << "Loaded" << this->cssFlairs_.size()
                               << "flair images from the stylesheet, base"
                               << cssBase;
    }

    StringMap<EmotePtr> badges;
    for (const auto &def : this->flairs_)
    {
        if (def.image.isValid())
        {
            badges.emplace(def.name, makeBadge(def));
        }
    }

    // The manifest wins. The stylesheet fills in the flairs it has no image
    // for or doesn't list at all (chat-gui doesn't draw those, we do).
    size_t extra = 0;
    StringMap<int> orders;
    for (const auto &def : this->cssFlairs_)
    {
        orders.emplace(def.name, def.order);
        if (!def.image.isValid())
        {
            continue;
        }
        if (badges.emplace(def.name, makeBadge(def)).second)
        {
            extra++;
        }
    }
    if (extra > 0)
    {
        qCDebug(chatterinoDgg)
            << extra << "flair badges come from the stylesheet only";
    }

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
