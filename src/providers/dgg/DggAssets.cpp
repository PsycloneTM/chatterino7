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

namespace {

using namespace chatterino;
using namespace Qt::Literals;

constexpr auto EMOTES_URL = "https://cdn.destiny.gg/emotes/emotes.json"_L1;
constexpr auto FLAIRS_URL = "https://cdn.destiny.gg/flairs/flairs.json"_L1;

constexpr int REQUEST_TIMEOUT_MS = 15000;

/// DGG has a single resolution per image, so there's no 2x/3x set. The image
/// is drawn at the size the manifest gives (scale 1). `Image::fromUrl` caches
/// by URL, and the URLs contain the asset version, so a new manifest version
/// never shows a stale image.
ImageSet imageSetFor(const DggImageRef &ref)
{
    QSize size;
    if (ref.width > 0 && ref.height > 0)
    {
        size = QSize(ref.width, ref.height);
    }
    return ImageSet(Image::fromUrl(Url{ref.url}, 1.0, size));
}

EmotePtr makeEmote(const DggEmoteDef &def)
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
        .images = imageSetFor(def.image),
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
        .images = imageSetFor(def.image),
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
    this->pending_ = 2;

    this->fetch(EMOTES_URL, [this](const QJsonArray &json) {
        this->applyEmotes(json);
    });
    this->fetch(FLAIRS_URL, [this](const QJsonArray &json) {
        this->applyFlairs(json);
    });
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
        auto emote = makeEmote(def);
        auto prefix = def.prefix;
        emotes.emplace(std::move(prefix),
                       EmoteEntry{.def = std::move(def), .emote = emote});
    }

    qCDebug(chatterinoDgg) << "Loaded" << emotes.size() << "emotes";
    this->emotes_ = std::move(emotes);
}

void DggAssets::applyFlairs(const QJsonArray &json)
{
    auto defs = parseDggFlairs(json);
    if (defs.empty())
    {
        qCWarning(chatterinoDgg) << "The flair manifest has no flairs";
        return;
    }

    StringMap<EmotePtr> badges;
    for (const auto &def : defs)
    {
        if (def.image.isValid())
        {
            badges.emplace(def.name, makeBadge(def));
        }
    }

    qCDebug(chatterinoDgg) << "Loaded" << defs.size() << "flairs";
    this->flairs_ = std::move(defs);
    this->badges_ = std::move(badges);
}

EmotePtr DggAssets::emoteFor(QStringView word, const DggUser &user) const
{
    auto it = this->emotes_.find(word);
    if (it == this->emotes_.end())
    {
        return nullptr;
    }
    if (!dggCanUseEmote(it->second.def, user))
    {
        return nullptr;
    }
    return it->second.emote;
}

std::vector<EmotePtr> DggAssets::badgesFor(const DggUser &user) const
{
    std::vector<EmotePtr> out;
    for (const auto &feature : user.features)
    {
        auto it = this->badges_.find(feature);
        if (it != this->badges_.end())
        {
            out.push_back(it->second);
        }
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
