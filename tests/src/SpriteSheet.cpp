#include "messages/SpriteSheet.hpp"

#include "Test.hpp"

using namespace chatterino;

namespace {

/// A sheet of `columns` frames of size `frame`. Frame `i` is filled with the
/// colour (10 * (i + 1), 20, 30), so a frame is told apart by its red value.
QImage makeSheet(int columns, QSize frame)
{
    QImage sheet(columns * frame.width(), frame.height(),
                 QImage::Format_ARGB32);
    for (int i = 0; i < columns; i++)
    {
        for (int y = 0; y < frame.height(); y++)
        {
            for (int x = 0; x < frame.width(); x++)
            {
                sheet.setPixel(i * frame.width() + x, y,
                               qRgba(10 * (i + 1), 20, 30, 255));
            }
        }
    }
    return sheet;
}

SpriteSheetSpec specFor(QSize frame, std::vector<QPoint> offsets,
                        int durationMs = 100)
{
    SpriteSheetSpec spec;
    spec.frameSize = frame;
    spec.offsets = std::move(offsets);
    spec.durationsMs.assign(spec.offsets.size(), durationMs);
    return spec;
}

constexpr qint64 PLENTY = 16 * 1024 * 1024;

}  // namespace

TEST(SpriteSheet, CutsTheFramesInPlayOrder)
{
    const auto sheet = makeSheet(3, QSize(4, 2));
    const auto spec =
        specFor(QSize(4, 2), {QPoint(8, 0), QPoint(0, 0), QPoint(4, 0)});

    const auto frames = sliceSpriteSheet(sheet, spec, PLENTY);
    ASSERT_EQ(frames.size(), 3U);

    EXPECT_EQ(frames[0].image.width(), 4);
    EXPECT_EQ(frames[0].image.height(), 2);
    EXPECT_EQ(qRed(frames[0].image.pixel(0, 0)), 30);
    EXPECT_EQ(qRed(frames[1].image.pixel(3, 1)), 10);
    EXPECT_EQ(qRed(frames[2].image.pixel(2, 0)), 20);
    EXPECT_EQ(qGreen(frames[2].image.pixel(2, 0)), 20);
    EXPECT_EQ(qBlue(frames[2].image.pixel(2, 0)), 30);
}

TEST(SpriteSheet, KeepsDurationsButNotBelowTheClockResolution)
{
    const auto sheet = makeSheet(2, QSize(4, 2));
    auto spec = specFor(QSize(4, 2), {QPoint(0, 0), QPoint(4, 0)});
    spec.durationsMs = {100, 5};

    const auto frames = sliceSpriteSheet(sheet, spec, PLENTY);
    ASSERT_EQ(frames.size(), 2U);
    EXPECT_EQ(frames[0].durationMs, 100);
    EXPECT_EQ(frames[1].durationMs, 20);
}

TEST(SpriteSheet, ASingleFrameHasNoDuration)
{
    const auto sheet = makeSheet(2, QSize(4, 2));
    const auto spec = specFor(QSize(4, 2), {QPoint(4, 0)}, 0);

    const auto frames = sliceSpriteSheet(sheet, spec, PLENTY);
    ASSERT_EQ(frames.size(), 1U);
    EXPECT_EQ(frames[0].durationMs, 0);
    EXPECT_EQ(qRed(frames[0].image.pixel(0, 0)), 20);
}

TEST(SpriteSheet, WhatIsOutsideOfTheSheetIsTransparent)
{
    // 12 px wide; a frame of 4 px at x = 10 has 2 px inside and 2 outside
    const auto sheet = makeSheet(3, QSize(4, 2));
    const auto spec = specFor(QSize(4, 2), {QPoint(10, 0)});

    const auto frames = sliceSpriteSheet(sheet, spec, PLENTY);
    ASSERT_EQ(frames.size(), 1U);
    EXPECT_EQ(qAlpha(frames[0].image.pixel(0, 0)), 255);
    EXPECT_EQ(qAlpha(frames[0].image.pixel(1, 0)), 255);
    EXPECT_EQ(qAlpha(frames[0].image.pixel(2, 0)), 0);
    EXPECT_EQ(qAlpha(frames[0].image.pixel(3, 0)), 0);
}

TEST(SpriteSheet, AnImageThatIsTooBigIsReducedToItsFirstFrame)
{
    const auto sheet = makeSheet(3, QSize(4, 2));
    const auto spec =
        specFor(QSize(4, 2), {QPoint(0, 0), QPoint(4, 0), QPoint(8, 0)});

    // one frame is 4 * 2 * 4 = 32 bytes, so three of them don't fit in 64
    const auto frames = sliceSpriteSheet(sheet, spec, 64);
    ASSERT_EQ(frames.size(), 1U);
    EXPECT_EQ(qRed(frames[0].image.pixel(0, 0)), 10);
}

TEST(SpriteSheet, NothingComesOutOfBadInput)
{
    const auto spec = specFor(QSize(4, 2), {QPoint(0, 0)});
    EXPECT_TRUE(sliceSpriteSheet(QImage(), spec, PLENTY).empty());

    const auto sheet = makeSheet(1, QSize(4, 2));
    EXPECT_TRUE(
        sliceSpriteSheet(sheet, specFor(QSize(0, 2), {QPoint(0, 0)}), PLENTY)
            .empty());
    EXPECT_TRUE(sliceSpriteSheet(sheet, specFor(QSize(4, 2), {}), PLENTY)
                    .empty());

    // one duration per frame
    auto mismatched = specFor(QSize(4, 2), {QPoint(0, 0), QPoint(4, 0)});
    mismatched.durationsMs.pop_back();
    EXPECT_FALSE(mismatched.isValid());
    EXPECT_TRUE(sliceSpriteSheet(sheet, mismatched, PLENTY).empty());
}

TEST(SpriteSheet, InvertFlipsTheColoursButNotTheAlpha)
{
    const auto sheet = makeSheet(1, QSize(2, 2));
    auto spec = specFor(QSize(2, 2), {QPoint(0, 0)}, 0);
    spec.filters.push_back({SpriteColorFilter::Kind::Invert, 1.0});

    const auto frames = sliceSpriteSheet(sheet, spec, PLENTY);
    ASSERT_EQ(frames.size(), 1U);
    const auto px = frames[0].image.pixel(0, 0);
    EXPECT_EQ(qRed(px), 245);
    EXPECT_EQ(qGreen(px), 235);
    EXPECT_EQ(qBlue(px), 225);
    EXPECT_EQ(qAlpha(px), 255);
}

TEST(SpriteSheet, OpacityOnlyTouchesTheAlpha)
{
    const auto sheet = makeSheet(1, QSize(2, 2));
    auto spec = specFor(QSize(2, 2), {QPoint(0, 0)}, 0);
    spec.filters.push_back({SpriteColorFilter::Kind::Opacity, 0.5});

    const auto frames = sliceSpriteSheet(sheet, spec, PLENTY);
    ASSERT_EQ(frames.size(), 1U);
    const auto px = frames[0].image.pixel(0, 0);
    EXPECT_EQ(qRed(px), 10);
    EXPECT_EQ(qGreen(px), 20);
    EXPECT_EQ(qBlue(px), 30);
    EXPECT_EQ(qAlpha(px), 128);
}

TEST(SpriteSheet, BrightnessScalesAndClamps)
{
    const auto sheet = makeSheet(1, QSize(2, 2));
    auto spec = specFor(QSize(2, 2), {QPoint(0, 0)}, 0);
    spec.filters.push_back({SpriteColorFilter::Kind::Brightness, 20.0});

    const auto frames = sliceSpriteSheet(sheet, spec, PLENTY);
    ASSERT_EQ(frames.size(), 1U);
    const auto px = frames[0].image.pixel(0, 0);
    // 10 * 20 = 200, 20 * 20 and 30 * 20 are over the top
    EXPECT_EQ(qRed(px), 200);
    EXPECT_EQ(qGreen(px), 255);
    EXPECT_EQ(qBlue(px), 255);
}

TEST(SpriteSheet, TheCacheKeyTellsSpecsApart)
{
    const auto a = specFor(QSize(4, 2), {QPoint(0, 0), QPoint(4, 0)});
    auto same = a;
    EXPECT_TRUE(a.cacheKey() == same.cacheKey());
    EXPECT_TRUE(a == same);

    auto otherOffset = a;
    otherOffset.offsets[1] = QPoint(8, 0);
    EXPECT_TRUE(a.cacheKey() != otherOffset.cacheKey());

    auto otherDuration = a;
    otherDuration.durationsMs[0] = 50;
    EXPECT_TRUE(a.cacheKey() != otherDuration.cacheKey());

    auto filtered = a;
    filtered.filters.push_back({SpriteColorFilter::Kind::Sepia, 1.0});
    EXPECT_TRUE(a.cacheKey() != filtered.cacheKey());
    EXPECT_FALSE(a == filtered);
}
