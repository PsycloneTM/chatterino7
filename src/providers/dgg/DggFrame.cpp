#include "providers/dgg/DggFrame.hpp"

#include <QJsonDocument>
#include <QJsonParseError>

namespace chatterino {

std::optional<DggFrame> parseDggFrame(QByteArrayView raw)
{
    const auto space = raw.indexOf(' ');
    const auto nameView = space < 0 ? raw : raw.first(space);
    if (nameView.isEmpty())
    {
        return std::nullopt;
    }

    DggFrame frame;
    frame.name = QString::fromUtf8(nameView).toUpper();

    if (space < 0)
    {
        frame.payload = QString();
        return frame;
    }

    const auto body = raw.sliced(space + 1);

    QJsonParseError error{};
    const auto doc = QJsonDocument::fromJson(body.toByteArray(), &error);
    if (error.error == QJsonParseError::NoError && doc.isObject())
    {
        frame.payload = doc.object();
    }
    else if (error.error == QJsonParseError::NoError && doc.isArray())
    {
        frame.payload = doc.array();
    }
    else
    {
        frame.payload = QString::fromUtf8(body);
    }

    return frame;
}

void expandDggHistory(const QJsonValue &payload, std::vector<DggFrame> &out)
{
    const auto entries = payload.toArray();
    out.reserve(out.size() + static_cast<size_t>(entries.size()));
    for (const auto &entry : entries)
    {
        auto frame = parseDggFrame(entry.toString().toUtf8());
        if (!frame)
        {
            continue;
        }
        frame->backlog = true;
        out.push_back(std::move(*frame));
    }
}

}  // namespace chatterino
