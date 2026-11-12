// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "singletons/helper/GifTimer.hpp"

#include "Application.hpp"
#include "singletons/Settings.hpp"
#include "singletons/WindowManager.hpp"

#include <QApplication>

#include <algorithm>

namespace chatterino {

void GIFTimer::initialize()
{
    this->timer.setInterval(GIF_FRAME_LENGTH);
    this->timer.setTimerType(Qt::PreciseTimer);

    getSettings()->animateEmotes.connect([this](bool enabled, auto) {
        if (enabled)
        {
            this->timer.start();
        }
        else
        {
            this->timer.stop();
        }
    });

    this->wall_.start();

    QObject::connect(&this->timer, &QTimer::timeout, [this] {
        // position follows wall time (like a browser), not the number of
        // ticks: QTimer drops ticks while the GUI thread is busy
        const auto now = this->wall_.elapsed();
        const auto delta = now - this->lastWall_;
        this->lastWall_ = now;

        // the clock never pauses (a browser's doesn't either): when the
        // window is idle only the redraws stop, and animations come back
        // in the right phase
        this->position_ += static_cast<unsigned long>(
            std::clamp<qint64>(delta, 1, 60000));

        if (getSettings()->animationsWhenFocused &&
            this->openOverlayWindows_ == 0 &&
            QApplication::activeWindow() == nullptr)
        {
            return;
        }

        this->signal.invoke();
        getApp()->getWindows()->repaintGifEmotes();
    });
}

}  // namespace chatterino
