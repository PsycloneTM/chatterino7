// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "messages/Image.hpp"
#include "messages/ImageEffect.hpp"

#include <QList>

namespace chatterino::detail {

/// @brief Renders `effect` onto every frame of `source`.
///
/// This is the expensive part of an effect, and is meant to run exactly once
/// per image (on a worker thread). The result is then displayed like any other
/// image, so nothing has to be filtered while painting.
///
/// - `Cursed`: Same number of frames and durations as `source`.
/// - `Party`: Animated images get their hue shifted over their duration (the
///   loop is repeated if it's very short, so the cycle isn't a strobe). A
///   still image becomes a short pre-rendered hue cycle.
///
/// `source` is returned unchanged for `ImageEffect::None` or if a frame can't
/// be processed. Doesn't need to run on the GUI thread.
QList<Frame> renderImageEffect(const QList<Frame> &source, ImageEffect effect);

}  // namespace chatterino::detail
