// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "messages/EmoteModifiers.hpp"

#include "Test.hpp"

#include <QJsonObject>

using namespace chatterino;

TEST(EmoteModifiers, ParseBttv)
{
    for (const auto *word :
         {u"w!", u"h!", u"v!", u"l!", u"r!", u"z!"})
    {
        auto parsed = parseEmoteModifier(word);
        ASSERT_TRUE(parsed) << QStringView(word).toString().toStdString();
        EXPECT_TRUE(parsed->appliesToNext);
        EXPECT_TRUE(parsed->modifiers.any());
    }

    EXPECT_TRUE(parseEmoteModifier(u"w!")->modifiers.wide);
    EXPECT_TRUE(parseEmoteModifier(u"h!")->modifiers.flipHorizontal);
    EXPECT_TRUE(parseEmoteModifier(u"v!")->modifiers.flipVertical);
    EXPECT_TRUE(parseEmoteModifier(u"l!")->modifiers.rotateLeft);
    EXPECT_TRUE(parseEmoteModifier(u"r!")->modifiers.rotateRight);
    EXPECT_TRUE(parseEmoteModifier(u"z!")->modifiers.noSpace);
}

TEST(EmoteModifiers, ParseFfz)
{
    for (const auto *word : {u"ffzW", u"ffzX", u"ffzY"})
    {
        auto parsed = parseEmoteModifier(word);
        ASSERT_TRUE(parsed) << QStringView(word).toString().toStdString();
        EXPECT_FALSE(parsed->appliesToNext);
        EXPECT_TRUE(parsed->modifiers.any());
    }

    EXPECT_TRUE(parseEmoteModifier(u"ffzW")->modifiers.wide);
    EXPECT_TRUE(parseEmoteModifier(u"ffzX")->modifiers.flipHorizontal);
    EXPECT_TRUE(parseEmoteModifier(u"ffzY")->modifiers.flipVertical);
}

TEST(EmoteModifiers, ParseNotAModifier)
{
    EXPECT_FALSE(parseEmoteModifier(u""));
    EXPECT_FALSE(parseEmoteModifier(u"Kappa"));
    EXPECT_FALSE(parseEmoteModifier(u"w"));
    EXPECT_FALSE(parseEmoteModifier(u"W!"));
    EXPECT_FALSE(parseEmoteModifier(u"ffzw"));
    EXPECT_FALSE(parseEmoteModifier(u"w! "));
}

TEST(EmoteModifiers, Rotation)
{
    EmoteModifiers modifiers;
    EXPECT_EQ(modifiers.rotation(), 0);
    EXPECT_FALSE(modifiers.isSideways());

    modifiers.rotateRight = true;
    EXPECT_EQ(modifiers.rotation(), 90);
    EXPECT_TRUE(modifiers.isSideways());

    modifiers = {.rotateLeft = true};
    EXPECT_EQ(modifiers.rotation(), 270);
    EXPECT_TRUE(modifiers.isSideways());

    // `l!` and `r!` cancel each other out
    modifiers.rotateRight = true;
    EXPECT_EQ(modifiers.rotation(), 0);
    EXPECT_FALSE(modifiers.isSideways());
}

TEST(EmoteModifiers, Merge)
{
    EmoteModifiers modifiers = {.wide = true, .rotateLeft = true};
    modifiers.merge({.flipVertical = true, .noSpace = true});
    EXPECT_EQ(modifiers, (EmoteModifiers{
                             .wide = true,
                             .flipVertical = true,
                             .rotateLeft = true,
                             .noSpace = true,
                         }));

    // Merging something that's already set changes nothing
    modifiers.merge({.wide = true});
    EXPECT_TRUE(modifiers.wide);
    EXPECT_TRUE(modifiers.rotateLeft);
}

TEST(EmoteModifiers, ToJson)
{
    EXPECT_TRUE(EmoteModifiers{}.toJson().isEmpty());

    auto json = EmoteModifiers{
        .wide = true,
        .rotateLeft = true,
        .noSpace = true,
    }.toJson();
    EXPECT_EQ(json.size(), 3);
    EXPECT_TRUE(json["wide"].toBool());
    EXPECT_TRUE(json["rotateLeft"].toBool());
    EXPECT_TRUE(json["noSpace"].toBool());
}
