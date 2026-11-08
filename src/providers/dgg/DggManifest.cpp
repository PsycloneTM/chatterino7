#include "providers/dgg/DggManifest.hpp"

#include "providers/dgg/DggMessage.hpp"

#include <QJsonObject>

#include <algorithm>

namespace chatterino {

using namespace Qt::Literals;

namespace {

DggImageRef parseImage(const QJsonObject &entry)
{
    DggImageRef ref;
    const auto images = entry.value("image"_L1).toArray();
    if (images.isEmpty())
    {
        return ref;
    }

    const auto first = images.at(0).toObject();
    ref.url = first.value("url"_L1).toString();
    ref.width = first.value("width"_L1).toInt();
    ref.height = first.value("height"_L1).toInt();
    return ref;
}

}  // namespace

std::vector<DggEmoteDef> parseDggEmotes(const QJsonArray &json)
{
    std::vector<DggEmoteDef> out;
    out.reserve(static_cast<size_t>(json.size()));

    for (const auto &value : json)
    {
        const auto entry = value.toObject();
        DggEmoteDef def;
        def.prefix = entry.value("prefix"_L1).toString();
        if (def.prefix.isEmpty())
        {
            continue;
        }

        def.creator = entry.value("creator"_L1).toString();
        def.minimumSubTier = entry.value("minimumSubTier"_L1).toInt();
        def.patreonOnly = entry.value("patreonOnly"_L1).toBool();
        def.image = parseImage(entry);
        out.push_back(std::move(def));
    }

    return out;
}

std::vector<DggFlairDef> parseDggFlairs(const QJsonArray &json)
{
    std::vector<DggFlairDef> out;
    out.reserve(static_cast<size_t>(json.size()));

    for (const auto &value : json)
    {
        const auto entry = value.toObject();
        DggFlairDef def;
        def.name = entry.value("name"_L1).toString();
        if (def.name.isEmpty())
        {
            continue;
        }

        def.label = entry.value("label"_L1).toString();
        // `description` is null for most flairs, which toString() turns into
        // an empty string.
        def.description = entry.value("description"_L1).toString();
        def.hidden = entry.value("hidden"_L1).toBool();
        def.priority = entry.value("priority"_L1).toInt();
        def.color = entry.value("color"_L1).toString();
        def.rainbowColor = entry.value("rainbowColor"_L1).toBool();
        def.image = parseImage(entry);
        out.push_back(std::move(def));
    }

    return out;
}

bool dggCanUseEmote(const DggEmoteDef &emote, const DggUser &user)
{
    if (user.isPrivileged())
    {
        return true;
    }
    if (emote.patreonOnly)
    {
        return user.patreonMember;
    }
    return emote.minimumSubTier <= user.subTier;
}

const DggFlairDef *dggNickColorFlair(const std::vector<DggFlairDef> &flairs,
                                     const QStringList &features)
{
    const DggFlairDef *best = nullptr;
    for (const auto &flair : flairs)
    {
        if (!flair.colorsNick() || !features.contains(flair.name))
        {
            continue;
        }
        // strictly lower only, so the first flair in the manifest wins a tie
        if (best == nullptr || flair.priority < best->priority)
        {
            best = &flair;
        }
    }
    return best;
}

}  // namespace chatterino
