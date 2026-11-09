#pragma once

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QSize>
#include <QtGlobal>

#include <optional>

namespace chatterino {

/// One fully composited frame of an APNG animation.
struct ApngFrame {
    /// The whole canvas after this frame was drawn (premultiplied ARGB32).
    QImage image;

    /// How long the frame is shown, in milliseconds (never 0).
    int durationMs = 100;
};

struct ApngAnimation {
    /// The canvas size (the `IHDR` size).
    QSize size;

    QList<ApngFrame> frames;
};

/// Whether `data` is a PNG that carries an animation-control chunk (`acTL`).
/// Qt's own PNG handler ignores that chunk and only returns the default
/// image, so such files must not go through it.
bool isApng(const QByteArray &data);

/// Decodes an APNG without needing an Qt image plugin: every frame's pixel
/// data is repackaged as a plain PNG and decoded by Qt's PNG handler, and the
/// frames are composited on the canvas as the APNG specification describes
/// (dispose ops none/background/previous, blend ops source/over, sub-frames
/// at an offset).
///
/// Returns nothing if `data` isn't a readable APNG with at least one frame,
/// or if the animation would need more than `maxBytes` of RAM for its frames.
/// The loop count of the file is ignored on purpose (the caller loops).
std::optional<ApngAnimation> decodeApng(const QByteArray &data,
                                        qint64 maxBytes = 256LL * 1024 * 1024);

}  // namespace chatterino
