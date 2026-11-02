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
         {u"w!", u"h!", u"v!", u"l!", u"r!", u"c!", u"p!", u"s!", u"z!"})
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
    EXPECT_EQ(parseEmoteModifier(u"c!")->modifiers.colorEffect,
              ImageEffect::Cursed);
    EXPECT_EQ(parseEmoteModifier(u"p!")->modifiers.colorEffect,
              ImageEffect::Party);
    EXPECT_TRUE(parseEmoteModifier(u"s!")->modifiers.shake);
    EXPECT_TRUE(parseEmoteModifier(u"z!")->modifiers.noSpace);
}

TEST(EmoteModifiers, ParseFfz)
{
    for (const auto *word : {u"ffzW", u"ffzX", u"ffzY", u"ffzCursed"})
    {
        auto parsed = parseEmoteModifier(word);
        ASSERT_TRUE(parsed) << QStringView(word).toString().toStdString();
        EXPECT_FALSE(parsed->appliesToNext);
        EXPECT_TRUE(parsed->modifiers.any());
    }

    EXPECT_TRUE(parseEmoteModifier(u"ffzW")->modifiers.wide);
    EXPECT_TRUE(parseEmoteModifier(u"ffzX")->modifiers.flipHorizontal);
    EXPECT_TRUE(parseEmoteModifier(u"ffzY")->modifiers.flipVertical);
    EXPECT_EQ(parseEmoteModifier(u"ffzCursed")->modifiers.colorEffect,
              ImageEffect::Cursed);
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
    EmoteModifiers modifiers = {.wide = true, .shake = true};
    modifiers.merge({.flipVertical = true, .noSpace = true});
    EXPECT_EQ(modifiers, (EmoteModifiers{
                             .wide = true,
                             .flipVertical = true,
                             .shake = true,
                             .noSpace = true,
                         }));

    // Color effects don't combine, the last one wins
    modifiers.merge({.colorEffect = ImageEffect::Cursed});
    EXPECT_EQ(modifiers.colorEffect, ImageEffect::Cursed);
    modifiers.merge({.colorEffect = ImageEffect::Party});
    EXPECT_EQ(modifiers.colorEffect, ImageEffect::Party);
    modifiers.merge({.wide = true});
    EXPECT_EQ(modifiers.colorEffect, ImageEffect::Party);
}

TEST(EmoteModifiers, ImageEffect)
{
    EXPECT_EQ(EmoteModifiers{}.imageEffect(), ImageEffect::None);
    EXPECT_EQ((EmoteModifiers{.wide = true, .rotateLeft = true}.imageEffect()),
              ImageEffect::None);
    EXPECT_EQ((EmoteModifiers{.colorEffect = ImageEffect::Cursed}.imageEffect()),
              ImageEffect::Cursed);
    EXPECT_EQ((EmoteModifiers{.colorEffect = ImageEffect::Party}.imageEffect()),
              ImageEffect::Party);
    EXPECT_EQ((EmoteModifiers{.shake = true}.imageEffect()),
              ImageEffect::Shake);
    EXPECT_EQ((EmoteModifiers{.colorEffect = ImageEffect::Cursed, .shake = true}
                   .imageEffect()),
              ImageEffect::CursedShake);
    EXPECT_EQ((EmoteModifiers{.colorEffect = ImageEffect::Party, .shake = true}
                   .imageEffect()),
              ImageEffect::PartyShake);
}

TEST(EmoteModifiers, ToJson)
{
    EXPECT_TRUE(EmoteModifiers{}.toJson().isEmpty());

    auto json = EmoteModifiers{
        .wide = true,
        .colorEffect = ImageEffect::Party,
        .noSpace = true,
    }.toJson();
    EXPECT_EQ(json.size(), 3);
    EXPECT_TRUE(json["wide"].toBool());
    EXPECT_TRUE(json["party"].toBool());
    EXPECT_TRUE(json["noSpace"].toBool());
}
