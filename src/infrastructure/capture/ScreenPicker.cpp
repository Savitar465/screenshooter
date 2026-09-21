#include "ScreenPicker.h"

#include <QApplication>
#include <QCursor>
#include <QMainWindow>
#include <QScreen>

namespace qaflow {

namespace {
QScreen* screenUnderCursor() {
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    return screen ? screen : QGuiApplication::primaryScreen();
}
} // namespace

QScreen* ScreenPicker::pick(const QWidget* appWindow) const {
    switch (m_target) {
        case CaptureScreen::UnderCursor: break;
        case CaptureScreen::Fixed:
            for (QScreen* s : QGuiApplication::screens())
                if (s->name() == m_screenName) return s;
            break;   // desconectada: la del cursor
        case CaptureScreen::AwayFromApp: {
            const QList<QScreen*> screens = QGuiApplication::screens();
            if (screens.size() < 2) break;
            QScreen* appScreen = appWindow && appWindow->isVisible() ? appWindow->screen() : nullptr;
            if (!appScreen) break;   // QAflow está oculta o en la bandeja: cualquier pantalla vale
            // Entre las demás, la del cursor si está en una; si no, la principal; si no, la primera.
            QScreen* cursor = QGuiApplication::screenAt(QCursor::pos());
            if (cursor && cursor != appScreen) return cursor;
            QScreen* primary = QGuiApplication::primaryScreen();
            if (primary && primary != appScreen) return primary;
            for (QScreen* s : screens)
                if (s != appScreen) return s;
            break;
        }
    }
    return screenUnderCursor();
}

QWidget* ScreenPicker::findAppWindow() {
    for (QWidget* w : QApplication::topLevelWidgets())
        if (qobject_cast<QMainWindow*>(w) && w->isVisible()) return w;
    return nullptr;
}

bool ScreenPicker::isOnScreen(const QWidget* window, const QScreen* screen) {
    return window && screen && window->isVisible() && !window->isMinimized()
        && window->frameGeometry().intersects(screen->geometry());
}

} // namespace qaflow
