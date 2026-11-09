#include "messages/ApngDecoder.hpp"

#include <QPainter>

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace {

constexpr unsigned char PNG_SIGNATURE[8] = {
    0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A,
};

constexpr int MAX_CANVAS_DIMENSION = 8192;
constexpr int MAX_FRAMES = 2000;

enum DisposeOp : unsigned char {
    DisposeNone = 0,
    DisposeBackground = 1,
    DisposePrevious = 2,
};

enum BlendOp : unsigned char {
    BlendSource = 0,
    BlendOver = 1,
};

struct Chunk {
    std::array<char, 4> type{};
    const unsigned char *data = nullptr;
    quint32 length = 0;

    bool is(const char (&name)[5]) const
    {
        return std::memcmp(this->type.data(), name, 4) == 0;
    }
};

quint32 readBE32(const unsigned char *p)
{
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) |
           (quint32(p[2]) << 8) | quint32(p[3]);
}

quint32 readBE16(const unsigned char *p)
{
    return (quint32(p[0]) << 8) | quint32(p[1]);
}

void appendBE32(QByteArray &out, quint32 value)
{
    out.append(char((value >> 24) & 0xFF));
    out.append(char((value >> 16) & 0xFF));
    out.append(char((value >> 8) & 0xFF));
    out.append(char(value & 0xFF));
}

quint32 crc32Update(quint32 crc, const unsigned char *data, size_t size)
{
    static const auto table = [] {
        std::array<quint32, 256> t{};
        for (quint32 n = 0; n < 256; n++)
        {
            quint32 c = n;
            for (int k = 0; k < 8; k++)
            {
                c = (c & 1) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
            }
            t[n] = c;
        }
        return t;
    }();

    for (size_t i = 0; i < size; i++)
    {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

void appendChunk(QByteArray &out, const char (&type)[5],
                 const unsigned char *payload, size_t size)
{
    appendBE32(out, static_cast<quint32>(size));
    out.append(type, 4);
    if (size > 0)
    {
        out.append(reinterpret_cast<const char *>(payload),
                   static_cast<qsizetype>(size));
    }

    quint32 crc = 0xFFFFFFFFU;
    crc = crc32Update(crc, reinterpret_cast<const unsigned char *>(type), 4);
    crc = crc32Update(crc, payload, size);
    appendBE32(out, crc ^ 0xFFFFFFFFU);
}

/// Splits the file into chunks. Fails on a bad signature or a chunk that runs
/// past the end of the file (a missing IEND is tolerated).
bool splitChunks(const QByteArray &data, std::vector<Chunk> &out)
{
    if (data.size() < 8 ||
        std::memcmp(data.constData(), PNG_SIGNATURE, sizeof(PNG_SIGNATURE)) != 0)
    {
        return false;
    }

    const auto *bytes = reinterpret_cast<const unsigned char *>(data.constData());
    const quint64 size = static_cast<quint64>(data.size());
    quint64 offset = 8;
    while (offset + 12 <= size)
    {
        const quint32 length = readBE32(bytes + offset);
        if (quint64(length) + 12 > size - offset)
        {
            return false;
        }

        Chunk chunk;
        std::memcpy(chunk.type.data(), bytes + offset + 4, 4);
        chunk.data = bytes + offset + 8;
        chunk.length = length;
        out.push_back(chunk);

        offset += quint64(length) + 12;
        if (chunk.is("IEND"))
        {
            break;
        }
    }
    return !out.empty();
}

struct FrameData {
    // fcTL
    quint32 width = 0;
    quint32 height = 0;
    quint32 x = 0;
    quint32 y = 0;
    quint32 delayNum = 0;
    quint32 delayDen = 0;
    unsigned char dispose = DisposeNone;
    unsigned char blend = BlendSource;

    /// Concatenated `IDAT` / `fdAT` payloads
    QByteArray compressed;
};

}  // namespace

namespace chatterino {

bool isApng(const QByteArray &data)
{
    if (data.size() < 8 ||
        std::memcmp(data.constData(), PNG_SIGNATURE, sizeof(PNG_SIGNATURE)) != 0)
    {
        return false;
    }

    const auto *bytes = reinterpret_cast<const unsigned char *>(data.constData());
    const quint64 size = static_cast<quint64>(data.size());
    quint64 offset = 8;
    while (offset + 12 <= size)
    {
        const quint32 length = readBE32(bytes + offset);
        if (quint64(length) + 12 > size - offset)
        {
            return false;
        }
        if (std::memcmp(bytes + offset + 4, "acTL", 4) == 0)
        {
            return true;
        }
        if (std::memcmp(bytes + offset + 4, "IDAT", 4) == 0 ||
            std::memcmp(bytes + offset + 4, "IEND", 4) == 0)
        {
            // acTL has to come before the first IDAT
            return false;
        }
        offset += quint64(length) + 12;
    }
    return false;
}

std::optional<ApngAnimation> decodeApng(const QByteArray &data, qint64 maxBytes)
{
    std::vector<Chunk> chunks;
    if (!splitChunks(data, chunks))
    {
        return std::nullopt;
    }

    const unsigned char *ihdr = nullptr;
    // PLTE and tRNS have to be in front of every frame's IDAT
    std::vector<const Chunk *> shared;
    std::vector<FrameData> frames;
    bool seenIdat = false;
    // Whether the frame being filled takes the IDAT chunks (the default
    // image is only part of the animation if an fcTL precedes it)
    bool currentTakesIdat = false;
    bool haveFrame = false;

    for (const auto &chunk : chunks)
    {
        if (chunk.is("IHDR"))
        {
            if (chunk.length != 13)
            {
                return std::nullopt;
            }
            ihdr = chunk.data;
        }
        else if (chunk.is("PLTE") || chunk.is("tRNS"))
        {
            if (!seenIdat)
            {
                shared.push_back(&chunk);
            }
        }
        else if (chunk.is("fcTL"))
        {
            if (chunk.length != 26)
            {
                return std::nullopt;
            }
            if (frames.size() >= MAX_FRAMES)
            {
                return std::nullopt;
            }

            FrameData frame;
            frame.width = readBE32(chunk.data + 4);
            frame.height = readBE32(chunk.data + 8);
            frame.x = readBE32(chunk.data + 12);
            frame.y = readBE32(chunk.data + 16);
            frame.delayNum = readBE16(chunk.data + 20);
            frame.delayDen = readBE16(chunk.data + 22);
            frame.dispose = chunk.data[24];
            frame.blend = chunk.data[25];
            frames.push_back(std::move(frame));
            haveFrame = true;
            currentTakesIdat = !seenIdat;
        }
        else if (chunk.is("IDAT"))
        {
            seenIdat = true;
            if (haveFrame && currentTakesIdat)
            {
                frames.back().compressed.append(
                    reinterpret_cast<const char *>(chunk.data),
                    static_cast<qsizetype>(chunk.length));
            }
        }
        else if (chunk.is("fdAT"))
        {
            if (chunk.length < 4)
            {
                return std::nullopt;
            }
            if (haveFrame)
            {
                frames.back().compressed.append(
                    reinterpret_cast<const char *>(chunk.data + 4),
                    static_cast<qsizetype>(chunk.length - 4));
            }
        }
    }

    if (ihdr == nullptr)
    {
        return std::nullopt;
    }

    const quint32 canvasWidth = readBE32(ihdr);
    const quint32 canvasHeight = readBE32(ihdr + 4);
    if (canvasWidth == 0 || canvasHeight == 0 ||
        canvasWidth > MAX_CANVAS_DIMENSION ||
        canvasHeight > MAX_CANVAS_DIMENSION)
    {
        return std::nullopt;
    }

    // Frames without any data (a broken file) are dropped
    frames.erase(std::remove_if(frames.begin(), frames.end(),
                                [](const FrameData &f) {
                                    return f.compressed.isEmpty();
                                }),
                 frames.end());
    if (frames.empty())
    {
        return std::nullopt;
    }

    const double canvasBytes = double(canvasWidth) * double(canvasHeight) * 4.0;
    if (canvasBytes * double(frames.size()) > double(maxBytes))
    {
        return std::nullopt;
    }

    const QSize canvasSize(static_cast<int>(canvasWidth),
                           static_cast<int>(canvasHeight));
    QImage canvas(canvasSize, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);

    ApngAnimation animation;
    animation.size = canvasSize;
    animation.frames.reserve(static_cast<qsizetype>(frames.size()));

    // What the canvas looked like before the previous frame was drawn
    QImage beforePrevious;
    const FrameData *previous = nullptr;

    for (auto &frame : frames)
    {
        if (frame.width == 0 || frame.height == 0 ||
            quint64(frame.x) + frame.width > canvasWidth ||
            quint64(frame.y) + frame.height > canvasHeight)
        {
            return std::nullopt;
        }

        // Dispose of the previous frame
        if (previous != nullptr)
        {
            QPainter painter(&canvas);
            painter.setCompositionMode(QPainter::CompositionMode_Source);
            const QRect rect(static_cast<int>(previous->x),
                             static_cast<int>(previous->y),
                             static_cast<int>(previous->width),
                             static_cast<int>(previous->height));
            if (previous->dispose == DisposeBackground)
            {
                painter.fillRect(rect, Qt::transparent);
            }
            else if (previous->dispose == DisposePrevious &&
                     !beforePrevious.isNull())
            {
                painter.drawImage(rect.topLeft(), beforePrevious, rect);
            }
        }

        // Rebuild this frame as a stand-alone PNG
        QByteArray png(reinterpret_cast<const char *>(PNG_SIGNATURE), 8);
        {
            unsigned char header[13];
            std::memcpy(header, ihdr, 13);
            header[0] = static_cast<unsigned char>((frame.width >> 24) & 0xFF);
            header[1] = static_cast<unsigned char>((frame.width >> 16) & 0xFF);
            header[2] = static_cast<unsigned char>((frame.width >> 8) & 0xFF);
            header[3] = static_cast<unsigned char>(frame.width & 0xFF);
            header[4] = static_cast<unsigned char>((frame.height >> 24) & 0xFF);
            header[5] = static_cast<unsigned char>((frame.height >> 16) & 0xFF);
            header[6] = static_cast<unsigned char>((frame.height >> 8) & 0xFF);
            header[7] = static_cast<unsigned char>(frame.height & 0xFF);
            appendChunk(png, "IHDR", header, sizeof(header));
        }
        for (const auto *s : shared)
        {
            if (s->is("PLTE"))
            {
                appendChunk(png, "PLTE", s->data, s->length);
            }
            else
            {
                appendChunk(png, "tRNS", s->data, s->length);
            }
        }
        appendChunk(png, "IDAT",
                    reinterpret_cast<const unsigned char *>(
                        frame.compressed.constData()),
                    static_cast<size_t>(frame.compressed.size()));
        appendChunk(png, "IEND", nullptr, 0);

        QImage part;
        if (!part.loadFromData(png, "PNG") || part.isNull() ||
            part.width() != int(frame.width) ||
            part.height() != int(frame.height))
        {
            return std::nullopt;
        }

        // The very first frame can't "go back": treat it as background
        const bool first = previous == nullptr;
        unsigned char dispose = frame.dispose;
        if (first && dispose == DisposePrevious)
        {
            dispose = DisposeBackground;
        }

        if (dispose == DisposePrevious)
        {
            beforePrevious = canvas.copy();
        }
        else
        {
            beforePrevious = QImage();
        }

        {
            QPainter painter(&canvas);
            painter.setCompositionMode(frame.blend == BlendOver
                                           ? QPainter::CompositionMode_SourceOver
                                           : QPainter::CompositionMode_Source);
            painter.drawImage(QPoint(static_cast<int>(frame.x),
                                     static_cast<int>(frame.y)),
                              part);
        }

        // Same delay rules as for GIFs (browsers treat <= 10 ms as 100 ms)
        int duration = 100;
        if (frame.delayDen == 0)
        {
            duration = int(frame.delayNum * 10U);
        }
        else
        {
            duration = int((quint64(frame.delayNum) * 1000U) / frame.delayDen);
        }
        if (duration <= 10)
        {
            duration = 100;
        }
        duration = std::max(20, duration);

        animation.frames.append(ApngFrame{
            .image = canvas.copy(),
            .durationMs = duration,
        });

        // Remember the effective dispose op for the next iteration
        frame.dispose = dispose;
        previous = &frame;
    }

    return animation;
}

}  // namespace chatterino
