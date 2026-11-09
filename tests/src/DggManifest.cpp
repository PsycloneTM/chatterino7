#include "providers/dgg/DggManifest.hpp"

#include "providers/dgg/DggMessage.hpp"
#include "Test.hpp"

#include <QJsonArray>
#include <QJsonDocument>

using namespace chatterino;

namespace {

QJsonArray arr(const char *json)
{
    return QJsonDocument::fromJson(json).array();
}

DggUser userWith(int tier, bool patreon = false,
                 const QStringList &roles = {},
                 const QStringList &features = {})
{
    DggUser user;
    user.subTier = tier;
    user.patreonMember = patreon;
    user.roles = roles;
    user.features = features;
    return user;
}

}  // namespace

TEST(DggManifest, ParsesEmotes)
{
    auto emotes = parseDggEmotes(arr(R"([
        {"prefix":"OMEGALUL","creator":"someone","twitch":true,"theme":null,
         "minimumSubTier":0,
         "image":[{"url":"https://cdn.destiny.gg/4.68.0/emotes/a.png","name":"a.png","mime":"image/png","height":30,"width":82}]},
        {"prefix":"ALO","minimumSubTier":2,
         "image":[{"url":"https://cdn.destiny.gg/4.68.0/emotes/b.gif","mime":"image/gif","height":32,"width":32}]},
        {"creator":"no prefix, skipped"},
        "not an object"
    ])"));

    ASSERT_EQ(emotes.size(), 2U);

    EXPECT_EQ(emotes[0].prefix, "OMEGALUL");
    EXPECT_EQ(emotes[0].creator, "someone");
    EXPECT_EQ(emotes[0].minimumSubTier, 0);
    EXPECT_FALSE(emotes[0].patreonOnly);
    EXPECT_EQ(emotes[0].image.url,
              "https://cdn.destiny.gg/4.68.0/emotes/a.png");
    EXPECT_EQ(emotes[0].image.width, 82);
    EXPECT_EQ(emotes[0].image.height, 30);

    // creator is sometimes missing
    EXPECT_EQ(emotes[1].prefix, "ALO");
    EXPECT_TRUE(emotes[1].creator.isEmpty());
    EXPECT_EQ(emotes[1].minimumSubTier, 2);
}

TEST(DggManifest, EmoteWithoutImageIsKept)
{
    auto emotes = parseDggEmotes(arr(R"([{"prefix":"X","image":[]},{"prefix":"Y"}])"));
    ASSERT_EQ(emotes.size(), 2U);
    EXPECT_FALSE(emotes[0].image.isValid());
    EXPECT_FALSE(emotes[1].image.isValid());
}

TEST(DggManifest, ParsesFlairs)
{
    // Entries copied from the real manifest
    auto flairs = parseDggFlairs(arr(R"([
        {"label":"Micro","name":"flair17","description":"May or may not have a small donger",
         "hidden":true,"priority":1,"color":"#FCE205","rainbowColor":false,
         "image":[{"url":"https://cdn.destiny.gg/4.50.1/flairs/5d90d259a2c85.png","name":"5d90d259a2c85.png","mime":"image/png","height":18,"width":20}]},
        {"label":"Admin","name":"admin","description":null,"hidden":true,
         "priority":1,"color":"#EE1F1F","rainbowColor":false,
         "image":[{"url":"https://cdn.destiny.gg/4.50.1/flairs/admin.png","name":"admin.png","mime":"image/png","height":16,"width":16}]},
        {"label":"Anything Else? Subscriber Tier 5","name":"flair33","description":"",
         "hidden":false,"priority":3,"color":"#eb79da","rainbowColor":true},
        {"label":"nameless"}
    ])"));

    ASSERT_EQ(flairs.size(), 4U);

    EXPECT_EQ(flairs[0].name, "flair17");
    EXPECT_EQ(flairs[0].label, "Micro");
    EXPECT_EQ(flairs[0].description, "May or may not have a small donger");
    EXPECT_TRUE(flairs[0].hidden);
    EXPECT_EQ(flairs[0].priority, 1);
    EXPECT_EQ(flairs[0].color, "#FCE205");
    EXPECT_FALSE(flairs[0].rainbowColor);
    EXPECT_EQ(flairs[0].image.width, 20);
    EXPECT_EQ(flairs[0].image.height, 18);

    // null description becomes an empty string; Admin is plain red
    EXPECT_EQ(flairs[1].name, "admin");
    EXPECT_TRUE(flairs[1].description.isEmpty());
    EXPECT_EQ(flairs[1].color, "#EE1F1F");
    EXPECT_FALSE(flairs[1].rainbowColor);
    EXPECT_EQ(flairs[1].image.width, 16);

    // Tier 5 has a colour and is rainbow; this one has no image
    EXPECT_EQ(flairs[2].name, "flair33");
    EXPECT_EQ(flairs[2].color, "#eb79da");
    EXPECT_TRUE(flairs[2].rainbowColor);
    EXPECT_TRUE(flairs[2].colorsNick());
    EXPECT_FALSE(flairs[2].image.isValid());
}

TEST(DggManifest, EmotePermissions)
{
    DggEmoteDef everyone;
    everyone.prefix = "a";

    DggEmoteDef tier2;
    tier2.prefix = "b";
    tier2.minimumSubTier = 2;

    DggEmoteDef patreon;
    patreon.prefix = "c";
    patreon.patreonOnly = true;

    EXPECT_TRUE(dggCanUseEmote(everyone, userWith(0)));
    EXPECT_FALSE(dggCanUseEmote(tier2, userWith(0)));
    EXPECT_FALSE(dggCanUseEmote(tier2, userWith(1)));
    EXPECT_TRUE(dggCanUseEmote(tier2, userWith(2)));
    EXPECT_TRUE(dggCanUseEmote(tier2, userWith(5)));

    // Patreon-only emotes ignore the sub tier
    EXPECT_FALSE(dggCanUseEmote(patreon, userWith(5)));
    EXPECT_TRUE(dggCanUseEmote(patreon, userWith(0, true)));

    // Privileged users may use everything; flairs never grant privileges
    EXPECT_TRUE(dggCanUseEmote(tier2, userWith(0, false, {"MODERATOR"})));
    EXPECT_TRUE(dggCanUseEmote(patreon, userWith(0, false, {"ADMIN"})));
    EXPECT_FALSE(
        dggCanUseEmote(tier2, userWith(0, false, {}, {"MODERATOR", "ADMIN"})));
}

TEST(DggManifest, NickColorFlair)
{
    auto flairs = parseDggFlairs(arr(R"([
        {"name":"flair13","label":"T1","priority":100,"color":null,"rainbowColor":false},
        {"name":"flair1","label":"T2","priority":50,"color":"#111111","rainbowColor":false},
        {"name":"flair3","label":"T3","priority":30,"color":"#333333","rainbowColor":false},
        {"name":"tie-a","label":"A","priority":10,"color":"#aaaaaa","rainbowColor":false},
        {"name":"tie-b","label":"B","priority":10,"color":"#bbbbbb","rainbowColor":false}
    ])"));

    // no colourful flair
    EXPECT_EQ(dggNickColorFlair(flairs, {"flair13", "subscriber"}), nullptr);
    EXPECT_EQ(dggNickColorFlair(flairs, {}), nullptr);
    EXPECT_EQ(dggNickColorFlair(flairs, {"unknown"}), nullptr);

    // lowest priority number wins, whatever order the user's features are in
    auto *pick = dggNickColorFlair(flairs, {"flair1", "flair3", "flair13"});
    ASSERT_NE(pick, nullptr);
    EXPECT_EQ(pick->name, "flair3");

    // a tie keeps the manifest order
    pick = dggNickColorFlair(flairs, {"tie-b", "tie-a"});
    ASSERT_NE(pick, nullptr);
    EXPECT_EQ(pick->name, "tie-a");
}

TEST(DggManifest, RainbowFlairCountsAsColoured)
{
    auto flairs = parseDggFlairs(arr(R"([
        {"name":"rainbow","label":"R","priority":1,"color":null,"rainbowColor":true},
        {"name":"red","label":"Red","priority":2,"color":"#ff0000","rainbowColor":false}
    ])"));

    auto *pick = dggNickColorFlair(flairs, {"red", "rainbow"});
    ASSERT_NE(pick, nullptr);
    EXPECT_EQ(pick->name, "rainbow");
}

TEST(DggManifest, ParsesPatreonMemberFromMsg)
{
    auto obj = QJsonDocument::fromJson(
                   R"({"id":1,"nick":"a","patreonMember":true,"subscription":null})")
                   .object();
    auto user = DggUser::fromJson(obj);
    EXPECT_TRUE(user.patreonMember);
    EXPECT_EQ(user.subTier, 0);
}

TEST(DggManifest, FlairCss)
{
    const QString css = R"(
:root { --x: 1; }
/* .flair.commented { background-image: url("no.png"); } */
.flair.flair4 {
  background-image: url("69b8e8a871fe3.png");
  height: 16px;
  width: 17px;
  order: 127;
}
.flair.flair2 {
  display: none !important;
}
.flair.bot { background-image: url('bot.png'); height: 16px; width: 16px; }
.user.bot { color: #0088CC; }
.flair.flair33 {
  background-image: url(https://example.com/a/flair33.png);
  height: 19px;
  width: 18px;
}
.user.flair33 { color: transparent; }
.msg-chat:not(.msg-whisper) .user.flair33:hover::before { content: ""; }
.flair.flair127 {
  background-image: url("6ad2fa78d17d1.png");
  height: 18px;
  width: 18px;
  order: 50;
}
.flair.flair4 { width: 20px; }
)";

    auto flairs =
        parseDggFlairCss(css, "https://cdn.destiny.gg/flairs/flairs.css");

    // flair2 is hidden, `commented` is a comment, the `.user` rules are not
    // badges
    ASSERT_EQ(flairs.size(), 3U);

    EXPECT_EQ(flairs[0].name, "flair4");
    EXPECT_EQ(flairs[0].label, "flair4");
    EXPECT_EQ(flairs[0].image.url,
              "https://cdn.destiny.gg/flairs/69b8e8a871fe3.png");
    EXPECT_EQ(flairs[0].image.height, 16);
    // the later rule wins per property
    EXPECT_EQ(flairs[0].image.width, 20);
    EXPECT_FALSE(flairs[0].colorsNick());
    EXPECT_EQ(flairs[0].order, 127);
    EXPECT_EQ(flairs[1].order, 0);

    EXPECT_EQ(flairs[1].name, "bot");
    EXPECT_EQ(flairs[1].image.url, "https://cdn.destiny.gg/flairs/bot.png");

    EXPECT_EQ(flairs[2].name, "flair33");
    EXPECT_EQ(flairs[2].image.url, "https://example.com/a/flair33.png");
    EXPECT_EQ(flairs[2].image.width, 18);

    // The user-supplied flairs.css has this badge even though flairs.json
    // does not contain a matching entry.
    EXPECT_EQ(flairs[3].name, "flair127");
    EXPECT_EQ(flairs[3].image.url,
              "https://cdn.destiny.gg/flairs/6ad2fa78d17d1.png");
    EXPECT_EQ(flairs[3].image.width, 18);
    EXPECT_EQ(flairs[3].image.height, 18);
    EXPECT_EQ(flairs[3].order, 50);
}
