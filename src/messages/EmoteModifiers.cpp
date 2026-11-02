// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "messages/EmoteModifiers.hpp"

#include "common/Literals.hpp"

#include <QJsonObject>

#include <array>

namespace {

using namespace chatterino;

struct ModifierEntry {
    const char16_t *name;
    EmoteModifierWord word;
};

const std::array MODIFIERS{
    // BTTV
    ModifierEntry{u"w!", {.modifiers = {.wide = true}, .appliesToNext = true}},
    ModifierEntry{u"h!",
                  {.modifiers = {.flipHorizontal = true},
                   .appliesToNext = true}},
    ModifierEntry{u"v!",
                  {.modifiers = {.flipVertical = true}, .appliesToNext = true}},
    ModifierEntry{u"l!",
                  {.modifiers = {.rotateLeft = true}, .appliesToNext = true}},
    ModifierEntry{u"r!",
                  {.modifiers = {.rotateRight = true}, .appliesToNext = true}},
    ModifierEntry{u"z!",
                  {.modifiers = {.noSpace = true}, .appliesToNext = true}},
    // FFZ
    ModifierEntry{u"ffzW",
                  {.modifiers = {.wide = true}, .appliesToNext = false}},
    ModifierEntry{u"ffzX",
                  {.modifiers = {.flipHorizontal = true},
                   .appliesToNext = false}},
    ModifierEntry{u"ffzY",
                  {.modifiers = {.flipVertical = true},
                   .appliesToNext = false}},
};

}  // namespace

namespace chatterino {

using namespace literals;

bool EmoteModifiers::any() const
{
    return this->noSpace || this->wide || this->flipHorizontal ||
           this->flipVertical || this->rotateLeft || this->rotateRight;
}

int EmoteModifiers::rotation() const
{
    return ((this->rotateRight ? 90 : 0) + (this->rotateLeft ? 270 : 0)) % 360;
}

bool EmoteModifiers::isSideways() const
{
    return this->rotation() == 90 || this->rotation() == 270;
}

void EmoteModifiers::merge(const EmoteModifiers &other)
{
    this->wide |= other.wide;
    this->flipHorizontal |= other.flipHorizontal;
    this->flipVertical |= other.flipVertical;
    this->rotateLeft |= other.rotateLeft;
    this->rotateRight |= other.rotateRight;
    this->noSpace |= other.noSpace;
}

QJsonObject EmoteModifiers::toJson() const
{
    QJsonObject obj;
    if (this->wide)
    {
        obj["wide"_L1] = true;
    }
    if (this->flipHorizontal)
    {
        obj["flipHorizontal"_L1] = true;
    }
    if (this->flipVertical)
    {
        obj["flipVertical"_L1] = true;
    }
    if (this->rotateLeft)
    {
        obj["rotateLeft"_L1] = true;
    }
    if (this->rotateRight)
    {
        obj["rotateRight"_L1] = true;
    }
    if (this->noSpace)
    {
        obj["noSpace"_L1] = true;
    }
    return obj;
}

std::optional<EmoteModifierWord> parseEmoteModifier(QStringView word)
{
    for (const auto &entry : MODIFIERS)
    {
        if (word == QStringView(entry.name))
        {
            return entry.word;
        }
    }
    return std::nullopt;
}

}  // namespace chatterino
