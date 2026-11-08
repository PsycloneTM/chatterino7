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
    auto flairs = parseDggFlairs(arr(R"([
        {"label":"Micro","name":"flair17","description":"May or may not have a small donger",
         "hidden":true,"priority":1,"color":"#FCE205","rainbowColor":false,
         "image":[{"url":"https://cdn.destiny.gg/4.50.1/flairs/x.png","name":"x.png","mime":"image/png","height":18,"width":20}]},
        {"label":"Admin","name":"admin","description":null,"hidden":true,
         "priority":1,"color":null,"rainbowColor":true,"image":[]},
        {"label":"nameless"}
    ])"));

    ASSERT_EQ(flairs.size(), 2U);

    EXPECT_EQ(flairs[0].name, "flair17");
    EXPECT_EQ(flairs[0].label, "Micro");
    EXPECT_EQ(flairs[0].description, "May or may not have a small donger");
    EXPECT_TRUE(flairs[0].hidden);
    EXPECT_EQ(flairs[0].priority, 1);
    EXPECT_EQ(flairs[0].color, "#FCE205");
    EXPECT_FALSE(flairs[0].rainbowColor);
    EXPECT_EQ(flairs[0].image.width, 20);
    EXPECT_EQ(flairs[0].image.height, 18);
    EXPECT_TRUE(flairs[0].colorsNick());

    // null description and null colour become empty strings
    EXPECT_EQ(flairs[1].name, "admin");
    EXPECT_TRUE(flairs[1].description.isEmpty());
    EXPECT_TRUE(flairs[1].color.isEmpty());
    EXPECT_TRUE(flairs[1].rainbowColor);
    EXPECT_TRUE(flairs[1].colorsNick());
    EXPECT_FALSE(flairs[1].image.isValid());
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
