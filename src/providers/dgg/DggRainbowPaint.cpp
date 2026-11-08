#include "providers/dgg/DggRainbowPaint.hpp"

#include "providers/seventv/paints/LinearGradientPaint.hpp"

#include <QColor>
#include <QGradient>
#include <QString>

using namespace Qt::Literals;

namespace chatterino {

const std::shared_ptr<Paint> &dggRainbowPaint()
{
    static const std::shared_ptr<Paint> PAINT =
        std::make_shared<LinearGradientPaint>(
            u"DGG rainbow"_s, u"dgg-rainbow"_s, std::nullopt,
            QGradientStops{
                {0.0, QColor(0xff, 0x40, 0x40)},
                {0.2, QColor(0xff, 0xc0, 0x20)},
                {0.4, QColor(0x60, 0xe0, 0x40)},
                {0.6, QColor(0x30, 0xd0, 0xe0)},
                {0.8, QColor(0x50, 0x70, 0xff)},
                {1.0, QColor(0xc0, 0x50, 0xff)},
            },
            // no repeat, left to right (90 degrees, see LinearGradientPaint)
            false, 90.0F, std::vector<PaintDropShadow>{});
    return PAINT;
}

}  // namespace chatterino
