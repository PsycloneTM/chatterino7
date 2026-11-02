// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QStringView>

#include <optional>

class QJsonObject;

namespace chatterino {

/// Visual modifiers for an emote: BTTV's `w!`, `h!`, `v!`, `l!`, `r!`, `z!`
/// (applied to the next emote) and FFZ's `ffzW`, `ffzX`, `ffzY` (applied to the
/// previous emote).
struct EmoteModifiers {
    bool wide = false;
    bool flipHorizontal = false;
    bool flipVertical = false;
    bool rotateLeft = false;
    bool rotateRight = false;
    /// `z!`. Removes the space between the emote and the one before it.
    bool noSpace = false;

    bool operator==(const EmoteModifiers &other) const = default;

    /// Whether any modifier is set
    bool any() const;

    /// Clockwise rotation in degrees (0, 90, 180 or 270).
    /// Using both `l!` and `r!` cancels out.
    int rotation() const;

    /// Whether the emote is turned on its side (width and height swap)
    bool isSideways() const;

    /// Adds the modifiers of `other` to these
    void merge(const EmoteModifiers &other);

    QJsonObject toJson() const;
};

/// The modifier that a word (an emote) stands for.
struct EmoteModifierWord {
    EmoteModifiers modifiers;
    /// BTTV modifiers apply to the next emote, FFZ ones to the previous one.
    bool appliesToNext;
};

/// Returns the modifier `word` stands for, if it's one.
///
/// Modifiers are emotes themselves. This only checks the name, the caller is
/// responsible for checking that the emote exists (for example, BTTV's global
/// emotes might be disabled).
std::optional<EmoteModifierWord> parseEmoteModifier(QStringView word);

}  // namespace chatterino
