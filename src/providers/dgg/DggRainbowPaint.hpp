#pragma once

#include <memory>

namespace chatterino {

class Paint;

/// The paint for destiny.gg nicks with a rainbow flair: a static left-to-right
/// rainbow gradient, drawn through the same code as 7TV paints.
///
/// chat-gui animates its rainbow, this one doesn't move.
const std::shared_ptr<Paint> &dggRainbowPaint();

}  // namespace chatterino
