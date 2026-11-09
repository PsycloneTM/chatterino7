#include "providers/dgg/DggEmoteCss.hpp"

#include "Test.hpp"

using namespace chatterino;

namespace {

/// The rules of DGG's real `emotes.css` (as of 2026-11-08) for the emotes the
/// tests below look at, with comments left out. The real file is 250 kB.
const char *const FIXTURE_CSS = R"CSS(
.emote.Askers {
    background-image: url("66596cb5db931.png");
    height: 34px;
    width: 5544px;
}
.msg-chat .emote.Askers {
    margin-top: -34px;
    top: 8.5px;
}
.emote.Aware {
    background-image: url("66596d4136715.png");
    height: 32px;
    width: 1152px;
}
.msg-chat .emote.Aware {
    margin-top: -32px;
    top: 8px;
}
.emote.BINGQILIN {
    background-image: url("691926a52c4e6.avif");
    height: 32px;
    width: 6390px;
}
.msg-chat .emote.BINGQILIN {
    margin-top: -32px;
    top: 8px;
}
.emote.dggL {
    background-image: url("6a4e93d269835.png");
    height: 35px;
    width: 825px;
}
.msg-chat .emote.dggL {
    margin-top: -35px;
    top: 8.75px;
}
.emote.glorp {
    background-image: url("691a4c80becd1.png");
    height: 28px;
    width: 308px;
}
.msg-chat .emote.glorp {
    margin-top: -28px;
    top: 7px;
}
.emote.WWWWaiting {
    background-image: url("6918d770809b0.png");
    height: 32px;
    width: 77px;
}
.msg-chat .emote.WWWWaiting {
    margin-top: -32px;
    top: 8px;
}
@keyframes Askers-anim {
  100% { background-position: -5544px; }
}
@keyframes Askers-hover {
  100% { background-position: -5544px; }
}
.emote.Askers {
  width: 56px;
  margin-left: -4px;
  margin-right: -4px;
  background-position: 0px;
  background-size: 5544px 34px;
  animation: Askers-anim calc(3.5s / var(--animation-speed, 1)) steps(99) 4;
}
.emote.Askers:hover {
  animation: Askers-hover calc(3.5s / var(--animation-speed, 1)) steps(99) infinite;
}
.emote.Aware {
  height: 32px;
  width: 36px;
  animation: Aware-anim 1.3s steps(31) forwards;
}
.emote.Aware:hover {
  height: 32px;
  width: 36px;
  animation: Aware-hover 1.3s steps(31) forwards 1;
}
@keyframes Aware-anim {
  to { background-position: -1116px; }
}
@keyframes Aware-hover {
  to { background-position: -1116px; }
}
@keyframes BINGQILIN {
  0% { background-position: var(--start); }
  100% { background-position: var(--end); }
}
@keyframes BINGQILIN-hover {
  0% { background-position: var(--start); }
  100% { background-position: var(--end); }
}
.emote.BINGQILIN {
  --start: 0px;
  --end: -2880px;
  width: 32px;
  background-position: -2848px; 
  animation: BINGQILIN steps(90) 3s 1;
}
.emote.BINGQILIN:hover {
  animation: BINGQILIN-hover steps(90) 3s infinite;
}
.msg-chat.msg-user[data-username="igor"] .emote.BINGQILIN {
  --start: -2880px;
  --end: -6390px;
  width: 39px;
  background-position: -6351px;
}
.emote.dggL {
  height: 35px;
  width: 33px;
  background-position: 0px;
  transition: all 0.5s;
}
.msg-chat.msg-user[data-username="peachachoo"] .emote.dggL {
  background-position: -693px;
  transition: all 0.5s;
}
.msg-chat.msg-user .user.flair13 ~ .text .emote.dggL {
  
  background-position: -33px;
  transition: all 0.5s;
}
.msg-chat.msg-user .user.flair1 ~ .text .emote.dggL {
  
  background-position: -66px;
  transition: all 0.5s;
}
.msg-chat.msg-user .user.flair3 ~ .text .emote.dggL {
  
  background-position: -99px;
  transition: all 0.5s;
}
.msg-chat.msg-user .user.flair8 ~ .text .emote.dggL {
  
  background-position: -132px;
  transition: all 0.5s;
}
.msg-chat.msg-user .user.flair42 ~ .text .emote.dggL {
  
  background-position: -165px;
  animation: dggL-T5 3s steps(19) infinite;
  transition: all 0.5s;
}
@keyframes dggL-T5 {
  0% {
    background-position: -165px;
  }
  100% {
    background-position: -792px;
  }
}
.emote.dggL:hover {
  
  background-position: 0px;
  transition: all 0.5s;
  filter: brightness(1.25) drop-shadow(0px 0px 3px #ffffff);
}
.msg-chat.msg-user .user.flair13 ~ .text .emote.dggL:hover {
  
  background-position: -33px;
  transition: all 0.5s;
  filter: brightness(1.25) drop-shadow(0px 0px 1px #ffffff)
    drop-shadow(0px 0px 3px #00a6ff);
}
.msg-chat.msg-user .user.flair1 ~ .text .emote.dggL:hover {
  
  background-position: -66px;
  transition: all 0.5s;
  filter: brightness(1.25) drop-shadow(0px 0px 1px #ffffff)
    drop-shadow(0px 0px 3px #00ffe2);
}
.msg-chat.msg-user .user.flair3 ~ .text .emote.dggL:hover {
  
  background-position: -99px;
  transition: all 0.5s;
  filter: brightness(1.25) drop-shadow(0px 0px 1px #ffffff)
    drop-shadow(0px 0px 3px #49ff00);
}
.msg-chat.msg-user .user.flair8 ~ .text .emote.dggL:hover {
  
  background-position: -132px;
  transition: all 0.5s;
  filter: brightness(1.25) drop-shadow(0px 0px 1px #ffffff)
    drop-shadow(0px 0px 3px #ff00ef);
}
.msg-chat.msg-user .user.flair42 ~ .text .emote.dggL:hover {
  
  background-position: -165px;
  animation: dggL-T5 1.9s steps(19) infinite;
  transition: all 0.5s;
  filter: brightness(1.25) drop-shadow(0px 0px 1px #ffffff)
    drop-shadow(0px 0px 1px #ff0000) drop-shadow(0px 0px 1px #ff7f00)
    drop-shadow(0px 0px 1px #ffff00) drop-shadow(0px 0px 1px #00ff00)
    drop-shadow(0px 0px 1px #8b00ff) drop-shadow(0px 0px 2px #0000ff);
}
.emote.glorp {
    height: 28px;
    width: 28px;
    background-position: 0px;
    background-repeat: no-repeat;
}
[data-username="prsek"] .emote.glorp {
    height: 28px;
    width: 28px;
    background-position: -110px;
    background-repeat: no-repeat;
    filter: hue-rotate(10deg) brightness(0.9) saturate(1.1);
}
[data-username="destiny"] .emote.glorp {
    height: 28px;
    width: 28px;
    background-position: -110px;
    background-repeat: no-repeat;
    filter: hue-rotate(10deg) brightness(0.9) saturate(1.1);
}
.emote.glorp:hover {
    filter: brightness(1.1) drop-shadow(0px 0px 3px #f7f7d7);
    transition: ease-out .4s;
}
.emote.WWWWaiting {
  width: 36px;
}
.msg-chat.msg-user[data-username="mrmouton"] .emote.WWWWaiting {
  width: 41px;
  background-position: -36px;
}
.msg-chat.msg-user[data-username="jaydrvernanda"] .emote.WWWWaiting {
  width: 36px;
  background-position: -0px;
}
)CSS";

const DggEmoteCss &css()
{
    static const DggEmoteCss parsed =
        DggEmoteCss::parse(QString::fromUtf8(FIXTURE_CSS));
    return parsed;
}

DggEmoteContext userContext(const char *nick)
{
    DggEmoteContext context;
    context.username = QString::fromUtf8(nick);
    return context;
}

int totalMs(const DggEmoteStyle &style)
{
    int total = 0;
    for (const int duration : style.sprite.durationsMs)
    {
        total += duration;
    }
    return total;
}

}  // namespace

TEST(DggEmoteCss, ParsesTheFixture)
{
    EXPECT_FALSE(css().isEmpty());
    EXPECT_EQ(css().stats().emotes, 6U);
    EXPECT_EQ(css().stats().unsupportedRules, 0U);
}

TEST(DggEmoteCss, UnknownEmoteHasNoStyle)
{
    EXPECT_FALSE(css().hasEmote(QStringLiteral("NotAnEmote")));
    EXPECT_FALSE(css()
                     .resolve(QStringLiteral("NotAnEmote"), userContext("x"),
                              QSize(10, 10))
                     .valid);
}

TEST(DggEmoteCss, EmptyStylesheetResolvesNothing)
{
    const DggEmoteCss empty = DggEmoteCss::parse(QString());
    EXPECT_TRUE(empty.isEmpty());
    EXPECT_FALSE(empty.resolve(QStringLiteral("Askers"), userContext("x"),
                               QSize(5544, 34))
                     .valid);
}

TEST(DggEmoteCss, AskersShowsOneFrameOfNinetyNine)
{
    // The manifest says 5544x34. That's the whole sheet, not the emote.
    const auto style = css().resolve(QStringLiteral("Askers"),
                                     userContext("someone"), QSize(5544, 34));
    ASSERT_TRUE(style.valid);
    EXPECT_EQ(style.frameSize.width(), 56);
    EXPECT_EQ(style.frameSize.height(), 34);
    ASSERT_EQ(style.sprite.offsets.size(), 99U);
    EXPECT_EQ(style.sprite.offsets.front().x(), 0);
    EXPECT_EQ(style.sprite.offsets.back().x(), 98 * 56);
    EXPECT_EQ(style.sprite.durationsMs.size(), 99U);
    EXPECT_EQ(totalMs(style), 3500);
    EXPECT_TRUE(style.animated());
    EXPECT_FALSE(style.isWholeImage(QSize(5544, 34)));
    EXPECT_FALSE(css().dependsOnContext(QStringLiteral("Askers")));
}

TEST(DggEmoteCss, AwareHasThirtyOneFrames)
{
    const auto style = css().resolve(QStringLiteral("Aware"),
                                     userContext("someone"), QSize(1152, 32));
    ASSERT_TRUE(style.valid);
    EXPECT_EQ(style.frameSize.width(), 36);
    EXPECT_EQ(style.frameSize.height(), 32);
    ASSERT_EQ(style.sprite.offsets.size(), 31U);
    EXPECT_EQ(style.sprite.offsets.back().x(), 30 * 36);
    EXPECT_EQ(totalMs(style), 1300);
}

TEST(DggEmoteCss, BingqilinUsesIgorsRangeForIgor)
{
    const auto normal = css().resolve(QStringLiteral("BINGQILIN"),
                                      userContext("someone"), QSize(6390, 32));
    ASSERT_TRUE(normal.valid);
    EXPECT_EQ(normal.frameSize.width(), 32);
    ASSERT_EQ(normal.sprite.offsets.size(), 90U);
    EXPECT_EQ(normal.sprite.offsets.front().x(), 0);
    EXPECT_EQ(normal.sprite.offsets.back().x(), 2848);
    EXPECT_EQ(totalMs(normal), 3000);

    const auto igor = css().resolve(QStringLiteral("BINGQILIN"),
                                    userContext("igor"), QSize(6390, 32));
    ASSERT_TRUE(igor.valid);
    EXPECT_EQ(igor.frameSize.width(), 39);
    EXPECT_EQ(igor.frameSize.height(), 32);
    ASSERT_EQ(igor.sprite.offsets.size(), 90U);
    EXPECT_EQ(igor.sprite.offsets.front().x(), 2880);
    EXPECT_EQ(igor.sprite.offsets.back().x(), 6351);

    EXPECT_TRUE(css().dependsOnContext(QStringLiteral("BINGQILIN")));
}

TEST(DggEmoteCss, DggLPicksTheVariantOfTheUserAndTheTier)
{
    const QSize sheet(825, 35);

    const auto normal = css().resolve(QStringLiteral("dggL"),
                                      userContext("someone"), sheet);
    ASSERT_TRUE(normal.valid);
    EXPECT_EQ(normal.frameSize.width(), 33);
    EXPECT_EQ(normal.frameSize.height(), 35);
    ASSERT_EQ(normal.sprite.offsets.size(), 1U);
    EXPECT_EQ(normal.sprite.offsets[0].x(), 0);
    EXPECT_FALSE(normal.animated());

    // a username selector
    const auto peach = css().resolve(QStringLiteral("dggL"),
                                     userContext("peachachoo"), sheet);
    ASSERT_EQ(peach.sprite.offsets.size(), 1U);
    EXPECT_EQ(peach.sprite.offsets[0].x(), 693);

    // `.user.flair13 ~ .text`: the flair that colours the nick
    auto tier = userContext("someone");
    tier.messageClasses.append(QStringLiteral("flair13"));
    tier.nickColorFlair = QStringLiteral("flair13");
    const auto t1 = css().resolve(QStringLiteral("dggL"), tier, sheet);
    ASSERT_EQ(t1.sprite.offsets.size(), 1U);
    EXPECT_EQ(t1.sprite.offsets[0].x(), 33);

    // a flair the user has but that doesn't colour the nick doesn't count
    auto other = userContext("someone");
    other.messageClasses.append(QStringLiteral("flair13"));
    const auto none = css().resolve(QStringLiteral("dggL"), other, sheet);
    ASSERT_EQ(none.sprite.offsets.size(), 1U);
    EXPECT_EQ(none.sprite.offsets[0].x(), 0);

    EXPECT_TRUE(css().dependsOnContext(QStringLiteral("dggL")));
}

TEST(DggEmoteCss, GlorpIsTintedForSomeUsers)
{
    const QSize sheet(308, 28);

    const auto normal = css().resolve(QStringLiteral("glorp"),
                                      userContext("someone"), sheet);
    ASSERT_TRUE(normal.valid);
    EXPECT_EQ(normal.frameSize.width(), 28);
    EXPECT_EQ(normal.sprite.offsets[0].x(), 0);
    EXPECT_TRUE(normal.sprite.filters.empty());

    for (const char *nick : {"prsek", "destiny"})
    {
        const auto tinted =
            css().resolve(QStringLiteral("glorp"), userContext(nick), sheet);
        ASSERT_TRUE(tinted.valid);
        EXPECT_EQ(tinted.sprite.offsets[0].x(), 110);
        ASSERT_EQ(tinted.sprite.filters.size(), 3U);
        using Kind = SpriteColorFilter::Kind;
        EXPECT_TRUE(tinted.sprite.filters[0].kind == Kind::HueRotate);
        EXPECT_NEAR(tinted.sprite.filters[0].amount, 10.0, 1e-9);
        EXPECT_TRUE(tinted.sprite.filters[1].kind == Kind::Brightness);
        EXPECT_NEAR(tinted.sprite.filters[1].amount, 0.9, 1e-9);
        EXPECT_TRUE(tinted.sprite.filters[2].kind == Kind::Saturate);
        EXPECT_NEAR(tinted.sprite.filters[2].amount, 1.1, 1e-9);
    }
}

TEST(DggEmoteCss, WWWWaitingIsWiderForMrmouton)
{
    const QSize sheet(77, 32);

    const auto normal = css().resolve(QStringLiteral("WWWWaiting"),
                                      userContext("someone"), sheet);
    ASSERT_TRUE(normal.valid);
    EXPECT_EQ(normal.frameSize.width(), 36);
    EXPECT_EQ(normal.frameSize.height(), 32);
    EXPECT_EQ(normal.sprite.offsets[0].x(), 0);

    const auto mouton = css().resolve(QStringLiteral("WWWWaiting"),
                                      userContext("mrmouton"), sheet);
    ASSERT_TRUE(mouton.valid);
    EXPECT_EQ(mouton.frameSize.width(), 41);
    EXPECT_EQ(mouton.frameSize.height(), 32);
    EXPECT_EQ(mouton.sprite.offsets[0].x(), 36);
}

TEST(DggEmoteCss, WholeImageIsRecognised)
{
    DggEmoteStyle style;
    style.valid = true;
    style.frameSize = QSize(32, 32);
    style.sprite.frameSize = QSize(32, 32);
    style.sprite.offsets = {QPoint(0, 0)};
    style.sprite.durationsMs = {0};

    EXPECT_TRUE(style.isWholeImage(QSize(32, 32)));
    EXPECT_FALSE(style.isWholeImage(QSize(64, 32)));
    EXPECT_FALSE(style.isWholeImage(QSize()));

    style.sprite.filters.push_back({SpriteColorFilter::Kind::Invert, 1.0});
    EXPECT_FALSE(style.isWholeImage(QSize(32, 32)));
}

TEST(DggEmoteCss, StylesThatDrawTheSameShareAKey)
{
    const QSize sheet(825, 35);
    const auto a = css().resolve(QStringLiteral("dggL"),
                                 userContext("someone"), sheet);
    const auto b = css().resolve(QStringLiteral("dggL"),
                                 userContext("somebody"), sheet);
    const auto c = css().resolve(QStringLiteral("dggL"),
                                 userContext("peachachoo"), sheet);
    EXPECT_TRUE(a.cacheKey() == b.cacheKey());
    EXPECT_TRUE(a.cacheKey() != c.cacheKey());
}

TEST(DggEmoteCss, AskersPlaysFourTimesAndRestsOnTheFirstFrame)
{
    const auto style = css().resolve(QStringLiteral("Askers"),
                                     userContext("someone"), QSize(5544, 34));
    ASSERT_TRUE(style.valid);
    EXPECT_EQ(style.sprite.loopCount, 4);
    EXPECT_TRUE(style.sprite.playsOnce());
    // no `forwards`: it goes back to the element's own position
    EXPECT_EQ(style.sprite.restOffset, QPoint(0, 0));
}

TEST(DggEmoteCss, AwarePlaysOnceAndStaysOnTheLastFrame)
{
    const auto style = css().resolve(QStringLiteral("Aware"),
                                     userContext("someone"), QSize(1152, 32));
    ASSERT_TRUE(style.valid);
    EXPECT_EQ(style.sprite.loopCount, 1);
    // `forwards` keeps the end of the keyframes: the 32nd frame of the sheet,
    // which is one past the 31 that steps(31) shows
    EXPECT_EQ(style.sprite.restOffset, QPoint(31 * 36, 0));
}

TEST(DggEmoteCss, BingqilinPlaysOnceAndGoesBackToItsOwnFrame)
{
    const auto style = css().resolve(QStringLiteral("BINGQILIN"),
                                     userContext("someone"), QSize(6390, 32));
    ASSERT_TRUE(style.valid);
    EXPECT_EQ(style.sprite.loopCount, 1);
    EXPECT_EQ(style.sprite.restOffset, QPoint(2848, 0));
}

TEST(DggEmoteCss, InfiniteAnimationsKeepLooping)
{
    const auto style = css().resolve(QStringLiteral("dggL"),
                                     userContext("someone"), QSize(825, 35));
    ASSERT_TRUE(style.valid);
    if (style.animated())
    {
        EXPECT_EQ(style.sprite.loopCount, 0);
        EXPECT_FALSE(style.sprite.playsOnce());
    }
}
