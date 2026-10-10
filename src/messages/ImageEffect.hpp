// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <cstdint>

namespace chatterino {

/// A color effect that gets baked into the frames of an image.
///
/// These back BTTV's `c!` and `p!` and FFZ's `ffzCursed` emote modifiers.
enum class ImageEffect : std::uint8_t {
    None,
    /// Black and white, high contrast (`c!`, `ffzCursed`)
    Cursed,
    /// Hue cycling through the rainbow (`p!`)
    Party,
};

/// The number of values in `ImageEffect` (including `None`)
inline constexpr std::size_t IMAGE_EFFECT_COUNT = 3;

}  // namespace chatterino
