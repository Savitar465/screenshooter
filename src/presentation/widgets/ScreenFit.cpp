#include "ScreenFit.h"

#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QMainWindow>
#include <QScreen>
#include <QWidget>

#include <algorithm>

namespace qaflow {

namespace {

/// Marco que pone el gestor de ventanas (barra de título y bordes) mientras la ventana no existe aún y
/// no se puede medir.
constexpr int kFrameSide = 8;
constexpr int kFrameTop = 40;

/// Diálogos (también los de Qt: mensajes, ficheros…) y ventanas principales; no los menús ni los
/// tooltips, ni las ventanas maximizadas, que ya miden lo que la pantalla.
bool isFittable(const QWidget* w) {
    if (!w->isWindow() || !(qobject_cast<const QDialog*>(w) || qobject_cast<const QMainWindow*>(w))) return false;
    return !(w->windowState() & (Qt::WindowMaximized | Qt::WindowFullScreen));
}

} // namespace

void ScreenFit::install() {
    static ScreenFit* filter = nullptr;
    if (filter || !qApp) return;
    filter = new ScreenFit(qApp);
    qApp->installEventFilter(filter);
}

QRect ScreenFit::available(const QWidget* window) {
    const QWidget* parent = window->parentWidget() ? window->parentWidget()->window() : nullptr;
    QScreen* screen = parent && parent->isVisible() ? parent->screen() : window->screen();
    if (!screen) screen = QApplication::primaryScreen();
    if (!screen) return {};
    QRect area = screen->availableGeometry();
    // El marco real, si la ventana ya se mostró alguna vez; si no, uno típico.
    const QRect frame = window->frameGeometry(), inner = window->geometry();
    const bool measured = window->testAttribute(Qt::WA_WState_Created) && frame != inner;
    const int left = measured ? inner.left() - frame.left() : kFrameSide;
    const int top = measured ? inner.top() - frame.top() : kFrameTop;
    const int right = measured ? frame.right() - inner.right() : kFrameSide;
    const int bottom = measured ? frame.bottom() - inner.bottom() : kFrameSide;
    return area.adjusted(left, top, -right, -bottom);
}

void ScreenFit::fit(QWidget* window) { fit(window, available(window)); }

void ScreenFit::fit(QWidget* window, const QRect& area) {
    if (area.isEmpty()) return;
    const QSize room = area.size();
    const QSize minimum = window->minimumSize();
    if (minimum.width() > room.width() || minimum.height() > room.height())
        window->setMinimumSize(minimum.boundedTo(room));
    if (window->width() > room.width() || window->height() > room.height())
        window->resize(window->size().boundedTo(room));
    // Los diálogos se centran sobre su padre al mostrarse (con el tamaño ya recortado); una ventana que
    // ya tenía sitio, si asoma por fuera, vuelve a entrar.
    if (window->testAttribute(Qt::WA_Moved)) {
        QRect g = window->geometry();
        g.moveLeft(std::clamp(g.left(), area.left(), std::max(area.left(), area.right() - g.width() + 1)));
        g.moveTop(std::clamp(g.top(), area.top(), std::max(area.top(), area.bottom() - g.height() + 1)));
        if (g.topLeft() != window->geometry().topLeft()) window->move(g.topLeft() - (window->geometry().topLeft() - window->pos()));
    }
}

bool ScreenFit::eventFilter(QObject* watched, QEvent* event) {
    // El Show llega antes que el showEvent del diálogo (que lo centra) y que se cree la ventana nativa.
    if (event->type() == QEvent::Show && watched->isWidgetType()) {
        auto* w = static_cast<QWidget*>(watched);
        if (isFittable(w)) fit(w);
    }
    return QObject::eventFilter(watched, event);
}

} // namespace qaflow
