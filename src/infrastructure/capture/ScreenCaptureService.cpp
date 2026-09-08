#include "ScreenCaptureService.h"

#include "infrastructure/capture/RegionSelector.h"

#include <QCoreApplication>
#include <QApplication>
#include <QCursor>
#include <QProcess>
#include <QRegularExpression>
#include <QScreen>
#include <QTimer>

namespace qaflow {

ScreenCaptureService::ScreenCaptureService(QObject* parent) : QObject(parent) {}

void ScreenCaptureService::capture(CaptureMode mode, Callback done) {
    switch (mode) {
        case CaptureMode::FullScreen: grabFullScreen(std::move(done)); break;
        case CaptureMode::ActiveWindow: grabActiveWindow(std::move(done)); break;
        case CaptureMode::Region: grabRegion(std::move(done)); break;
    }
}

QPixmap ScreenCaptureService::grabScreenUnderCursor(QRect* screenGeometry) {
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (screenGeometry) *screenGeometry = screen->geometry();
    return screen->grabWindow(0);
}

/// Oculta la ventana, espera a que el compositor repinte y ejecuta `body`, que recibe
/// una función `restore` para volver a mostrar la ventana cuando termine.
void ScreenCaptureService::withWindowHidden(const std::function<void(std::function<void()> restore)>& body) {
    QPointer<QWidget> win = m_appWindow;
    const bool wasVisible = win && win->isVisible();
    if (wasVisible) win->hide();
    auto restore = [win, wasVisible]() {
        if (wasVisible && win) { win->show(); win->raise(); win->activateWindow(); }
    };
    QTimer::singleShot(wasVisible ? 250 : 0, [body, restore]() { body(restore); });
}

void ScreenCaptureService::grabFullScreen(Callback done) {
    withWindowHidden([done](std::function<void()> restore) {
        const QPixmap pm = grabScreenUnderCursor();
        restore();
        if (pm.isNull()) done(CaptureResult{false, {}, QCoreApplication::translate("infrastructure", "No se pudo capturar la pantalla")});
        else done(CaptureResult{true, pm.toImage(), {}});
    });
}

/// Ventana activa: usa xdotool (X11) para conocer la geometría de la ventana enfocada.
/// Si no está disponible, recorta nada y devuelve la pantalla completa.
void ScreenCaptureService::grabActiveWindow(Callback done) {
    withWindowHidden([done](std::function<void()> restore) {
        QRect screenGeo;
        const QPixmap full = grabScreenUnderCursor(&screenGeo);
        restore();
        if (full.isNull()) { done(CaptureResult{false, {}, QCoreApplication::translate("infrastructure", "No se pudo capturar la pantalla")}); return; }

        QRect win;
        QProcess p;
        p.start(QStringLiteral("xdotool"), {QStringLiteral("getactivewindow"), QStringLiteral("getwindowgeometry"), QStringLiteral("--shell")});
        if (p.waitForFinished(1500) && p.exitCode() == 0) {
            const QString out = QString::fromUtf8(p.readAllStandardOutput());
            auto val = [&](const char* key) {
                const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("%1=(-?\\d+)").arg(QLatin1String(key))).match(out);
                return m.hasMatch() ? m.captured(1).toInt() : -1;
            };
            const int x = val("X"), y = val("Y"), w = val("WIDTH"), h = val("HEIGHT");
            if (w > 0 && h > 0) win = QRect(x, y, w, h).translated(-screenGeo.topLeft()).intersected(QRect(QPoint(0, 0), screenGeo.size()));
        }
        QImage img = full.toImage();
        if (win.isValid() && !win.isEmpty()) {
            const qreal dpr = full.devicePixelRatio();
            img = img.copy(QRect(win.topLeft() * dpr, win.size() * dpr));
        }
        done(CaptureResult{true, img, {}});
    });
}

void ScreenCaptureService::grabRegion(Callback done) {
    withWindowHidden([done](std::function<void()> restore) {
        QRect screenGeo;
        const QPixmap full = grabScreenUnderCursor(&screenGeo);
        if (full.isNull()) { restore(); done(CaptureResult{false, {}, QCoreApplication::translate("infrastructure", "No se pudo capturar la pantalla")}); return; }

        auto* selector = new RegionSelector(full);
        selector->setGeometry(screenGeo);
        QObject::connect(selector, &RegionSelector::regionSelected, [full, restore, done](const QRect& r) {
            restore();
            const qreal dpr = full.devicePixelRatio();
            done(CaptureResult{true, full.toImage().copy(QRect(r.topLeft() * dpr, r.size() * dpr)), {}});
        });
        QObject::connect(selector, &RegionSelector::cancelled, [restore, done]() {
            restore();
            done(CaptureResult{false, {}, QStringLiteral("Captura cancelada")});
        });
        selector->showFullScreen();
        selector->activateWindow();
    });
}

} // namespace qaflow
