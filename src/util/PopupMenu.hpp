#pragma once

#include <QGuiApplication>
#include <QMenu>
#include <QPoint>
#include <QScreen>
#include <QWindow>

namespace chatterino {

/// Qt 6.11.2+ no longer moves a top-level window's screen before applying its
/// geometry (QWindow::setGeometry), so a menu created while its parent is on
/// one screen renders with that screen's DPI scale when opened on another
/// (mixed-DPI setups). Set the target screen explicitly before showing.
inline void popupMenuOnScreen(QMenu *menu, const QPoint &globalPos)
{
    if (auto *screen = QGuiApplication::screenAt(globalPos))
    {
        menu->winId();  // make sure the native window exists
        if (auto *handle = menu->windowHandle())
        {
            if (handle->screen() != screen)
            {
                handle->setScreen(screen);
            }
        }
    }
    menu->popup(globalPos);
}

}  // namespace chatterino
