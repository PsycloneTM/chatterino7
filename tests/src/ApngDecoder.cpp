#include "messages/ApngDecoder.hpp"

#include "Test.hpp"

#include <QBuffer>
#include <QImage>

using namespace chatterino;

namespace {

#include "ApngDecoderFixtures.inc"

constexpr int W = 10;
constexpr int H = 8;

QByteArray bytesOf(const unsigned char *data, size_t size)
{
    return QByteArray(reinterpret_cast<const char *>(data),
                      static_cast<qsizetype>(size));
}

/// Compares the decoded frame with `RGBA` bytes (straight alpha). Fully
/// transparent pixels are equal whatever their colour is.
void expectFrameEquals(const QImage &actual, const unsigned char *rgba,
                       int frameIndex)
{
    ASSERT_EQ(actual.size(), QSize(W, H));
    const QImage straight = actual.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < H; y++)
    {
        for (int x = 0; x < W; x++)
        {
            const unsigned char *px = rgba + ((y * W + x) * 4);
            const QRgb got = straight.pixel(x, y);
            if (px[3] == 0)
            {
                EXPECT_EQ(qAlpha(got), 0)
                    << "frame " << frameIndex << " pixel " << x << "," << y;
                continue;
            }
            EXPECT_EQ(qRed(got), px[0])
                << "frame " << frameIndex << " pixel " << x << "," << y;
            EXPECT_EQ(qGreen(got), px[1])
                << "frame " << frameIndex << " pixel " << x << "," << y;
            EXPECT_EQ(qBlue(got), px[2])
                << "frame " << frameIndex << " pixel " << x << "," << y;
            EXPECT_EQ(qAlpha(got), px[3])
                << "frame " << frameIndex << " pixel " << x << "," << y;
        }
    }
}

void expectDecodes(const unsigned char *file, size_t fileSize,
                   const unsigned char *frames, const int *durations,
                   int count)
{
    const auto data = bytesOf(file, fileSize);
    ASSERT_TRUE(isApng(data));

    const auto animation = decodeApng(data);
    ASSERT_TRUE(animation.has_value());
    ASSERT_EQ(animation->size, QSize(W, H));
    ASSERT_EQ(animation->frames.size(), count);

    for (int i = 0; i < count; i++)
    {
        EXPECT_EQ(animation->frames[i].durationMs, durations[i])
            << "frame " << i;
        expectFrameEquals(animation->frames[i].image,
                          frames + (i * W * H * 4), i);
    }
}

}  // namespace

TEST(ApngDecoder, PlainFramesAndDurations)
{
    // The 4th frame asks for 5 ms, browsers (and Image.cpp) show it for 100
    expectDecodes(kNone, sizeof(kNone), kNoneFrames, kNoneDurations, 5);
}

TEST(ApngDecoder, DisposeBackgroundAndBlendOver)
{
    expectDecodes(kBackgroundOver, sizeof(kBackgroundOver),
                  kBackgroundOverFrames, kBackgroundOverDurations, 4);
}

TEST(ApngDecoder, DisposePrevious)
{
    expectDecodes(kPrevious, sizeof(kPrevious), kPreviousFrames,
                  kPreviousDurations, 4);
}

TEST(ApngDecoder, PaletteFrames)
{
    expectDecodes(kPalette, sizeof(kPalette), kPaletteFrames,
                  kPaletteDurations, 3);
}

TEST(ApngDecoder, StaticPngIsNotAnApng)
{
    QImage image(4, 4, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    ASSERT_TRUE(image.save(&buffer, "PNG"));

    EXPECT_FALSE(isApng(png));
    EXPECT_FALSE(decodeApng(png).has_value());
}

TEST(ApngDecoder, GarbageAndTruncatedInput)
{
    EXPECT_FALSE(isApng(QByteArray()));
    EXPECT_FALSE(isApng(QByteArrayLiteral("GIF89a......")));
    EXPECT_FALSE(decodeApng(QByteArrayLiteral("not a png at all")).has_value());

    // Cut in the middle of the frame data: either nothing or a clean failure,
    // never a crash
    const auto whole = bytesOf(kNone, sizeof(kNone));
    for (qsizetype cut : {qsizetype(20), qsizetype(60), whole.size() / 2,
                          whole.size() - 20})
    {
        (void)decodeApng(whole.left(cut));
    }
    EXPECT_FALSE(decodeApng(whole.left(30)).has_value());
}

TEST(ApngDecoder, MemoryLimit)
{
    const auto data = bytesOf(kNone, sizeof(kNone));
    // 5 frames of 10x8 RGBA = 1600 bytes
    EXPECT_TRUE(decodeApng(data, 1600).has_value());
    EXPECT_FALSE(decodeApng(data, 1599).has_value());
}
