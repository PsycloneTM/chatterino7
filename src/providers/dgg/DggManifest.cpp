#include "providers/dgg/DggManifest.hpp"

#include "providers/dgg/DggMessage.hpp"

#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

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

std::vector<DggFlairDef> parseDggFlairCss(const QString &css,
                                          const QString &baseUrl)
{
    static const QRegularExpression comments(
        QStringLiteral(R"(/\*.*?\*/)"),
        QRegularExpression::DotMatchesEverythingOption);
    // Only plain `.flair.<name> { ... }` rules. `.user.<name>` and the like
    // don't match because of the `.flair.` part.
    static const QRegularExpression rule(
        QStringLiteral(R"((?<![A-Za-z0-9_-])\.flair\.([A-Za-z0-9_-]+)\s*\{([^}]*)\})"),
        QRegularExpression::MultilineOption);
    static const QRegularExpression image(
        QStringLiteral(
            R"(background-image\s*:\s*url\(\s*["']?([^"')]+?)["']?\s*\))"));
    static const QRegularExpression width(
        QStringLiteral(R"((?:^|[;\s])width\s*:\s*(\d+)px)"));
    static const QRegularExpression height(
        QStringLiteral(R"((?:^|[;\s])height\s*:\s*(\d+)px)"));
    static const QRegularExpression order(
        QStringLiteral(R"((?:^|[;\s])order\s*:\s*(-?\d+))"));
    static const QRegularExpression hidden(
        QStringLiteral(R"(display\s*:\s*none)"));

    const QUrl base(baseUrl);
    const QString clean = QString(css).remove(comments);

    std::vector<DggFlairDef> out;
    std::vector<bool> isHidden;
    std::vector<bool> hasOrder;

    auto it = rule.globalMatch(clean);
    while (it.hasNext())
    {
        const auto match = it.next();
        const QString name = match.captured(1);
        const QString body = match.captured(2);

        size_t index = out.size();
        for (size_t i = 0; i < out.size(); i++)
        {
            if (out[i].name == name)
            {
                index = i;
                break;
            }
        }
        if (index == out.size())
        {
            DggFlairDef def;
            def.name = name;
            def.label = name;
            out.push_back(std::move(def));
            isHidden.push_back(false);
            hasOrder.push_back(false);
        }

        auto &def = out[index];
        if (body.contains(hidden))
        {
            isHidden[index] = true;
        }
        if (auto m = image.match(body); m.hasMatch())
        {
            def.image.url = base.resolved(QUrl(m.captured(1).trimmed()))
                                .toString();
        }
        if (auto m = order.match(body); m.hasMatch())
        {
            def.order = m.captured(1).toInt();
            hasOrder[index] = true;
        }
        if (auto m = width.match(body); m.hasMatch())
        {
            def.image.width = m.captured(1).toInt();
        }
        if (auto m = height.match(body); m.hasMatch())
        {
            def.image.height = m.captured(1).toInt();
        }
    }

    std::vector<DggFlairDef> filtered;
    for (size_t i = 0; i < out.size(); i++)
    {
        if (!isHidden[i] && (out[i].image.isValid() || hasOrder[i]))
        {
            filtered.push_back(std::move(out[i]));
        }
    }
    return filtered;
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
