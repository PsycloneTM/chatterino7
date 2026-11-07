// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>

namespace chatterino {

/// A color effect that gets baked into the frames of an image.
///
/// These back BTTV's `c!`, `p!` and `s!` and FFZ's `ffzCursed` emote
/// modifiers. Effects that can be used together (`c! s!`, `p! s!`) have their
/// own value, so every combination is rendered (and cached) once.
enum class ImageEffect : std::uint8_t {
    None,
    /// Black and white, high contrast (`c!`, `ffzCursed`)
    Cursed,
    /// Hue cycling through the rainbow (`p!`)
    Party,
    /// Jittering around (`s!`)
    Shake,
    /// `Cursed` and `Shake` (`c! s!`)
    CursedShake,
    /// `Party` and `Shake` (`p! s!`)
    PartyShake,
};

/// The number of values in `ImageEffect` (including `None`)
inline constexpr std::size_t IMAGE_EFFECT_COUNT = 6;

}  // namespace chatterino
