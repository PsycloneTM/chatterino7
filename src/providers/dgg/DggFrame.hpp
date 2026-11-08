#pragma once

#include <QByteArrayView>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <optional>
#include <vector>

namespace chatterino {

/// One event from the destiny.gg chat websocket.
///
/// On the wire every event is a single text frame shaped like `<EVENT> <json>`,
/// split at the first space (e.g. `MSG {"nick":"...","data":"..."}`).
struct DggFrame {
    /// The event name, upper-cased (`MSG`, `JOIN`, `NAMES`, ...).
    QString name;

    /// The parsed JSON object or array. If the payload isn't valid JSON (or
    /// there's no payload), this holds the raw payload as a string.
    QJsonValue payload;

    /// True if this frame came out of a `HISTORY` backlog.
    bool backlog = false;
};

/// Parses one raw text frame. Returns nullopt if there's no event name.
std::optional<DggFrame> parseDggFrame(QByteArrayView raw);

/// `HISTORY` carries a JSON array of strings, and each string is itself a
/// full frame (e.g. `MSG {...}`). This parses every entry (marked as backlog)
/// and appends it to `out`. Entries that aren't valid frames are skipped.
void expandDggHistory(const QJsonValue &payload, std::vector<DggFrame> &out);

}  // namespace chatterino
