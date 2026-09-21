#include "ScreenCaptureService.h"

#include "infrastructure/capture/PortalScreenshot.h"
#include "infrastructure/capture/RegionSelector.h"

#include <QCoreApplication>
#include <QApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QScreen>
#include <QTimer>

namespace qaflow {

ScreenCaptureService::ScreenCaptureService(QObject* parent) : QObject(parent) {
    if (PortalScreenshot::isWaylandSession() && PortalScreenshot::isAvailable()) {
        m_portal = new PortalScreenshot(this);
        m_usePortal = true;
    }
}

QString ScreenCaptureService::backendName() const {
    return m_usePortal ? QStringLiteral("xdg-desktop-portal (Wayland)") : QStringLiteral("QScreen::grabWindow");
}

void ScreenCaptureService::capture(CaptureMode mode, Callback done) {
    QScreen* screen = m_picker.pick(appWindow());
    if (m_usePortal) { captureViaPortal(mode, screen, std::move(done)); return; }
    switch (mode) {
        case CaptureMode::FullScreen: grabFullScreen(screen, std::move(done)); break;
        case CaptureMode::ActiveWindow: grabActiveWindow(screen, std::move(done)); break;
        case CaptureMode::Region: grabRegion(screen, std::move(done)); break;
    }
}

QWidget* ScreenCaptureService::appWindow() const {
    return m_appWindow ? m_appWindow.data() : ScreenPicker::findAppWindow();
}

void ScreenCaptureService::withWindowHidden(QScreen* screen, const std::function<void(QScreen*, std::function<void()> restore)>& body) {
    QPointer<QWidget> win = appWindow();
    const bool hide = ScreenPicker::isOnScreen(win, screen);
    if (hide) win->hide();
    auto restore = [win, hide]() {
        if (hide && win) { win->show(); win->raise(); win->activateWindow(); }
    };
    QPointer<QScreen> target = screen;
    QTimer::singleShot(hide ? 250 : 0, [body, restore, target]() {
        body(target ? target.data() : QGuiApplication::primaryScreen(), restore);
    });
}

void ScreenCaptureService::grabFullScreen(QScreen* screen, Callback done) {
    withWindowHidden(screen, [done](QScreen* screen, std::function<void()> restore) {
        const QPixmap pm = screen->grabWindow(0);
        restore();
        if (pm.isNull()) done(CaptureResult{false, {}, QCoreApplication::translate("infrastructure", "No se pudo capturar la pantalla")});
        else done(CaptureResult{true, pm.toImage(), {}});
    });
}

/// Ventana activa: usa xdotool (X11) para conocer la geometría de la ventana enfocada.
/// Si no está disponible, o la ventana activa no está en la pantalla elegida, devuelve la pantalla completa.
void ScreenCaptureService::grabActiveWindow(QScreen* screen, Callback done) {
    withWindowHidden(screen, [done](QScreen* screen, std::function<void()> restore) {
        const QRect screenGeo = screen->geometry();
        const QPixmap full = screen->grabWindow(0);
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

void ScreenCaptureService::selectRegion(const QPixmap& full, const QRect& screenGeo, std::function<void()> restore, Callback done) {
    auto* selector = new RegionSelector(full);
    selector->setGeometry(screenGeo);
    QObject::connect(selector, &RegionSelector::regionSelected, [full, restore, done](const QRect& r) {
        restore();
        const qreal dpr = full.devicePixelRatio();
        done(CaptureResult{true, full.toImage().copy(QRect(r.topLeft() * dpr, r.size() * dpr)), {}});
    });
    QObject::connect(selector, &RegionSelector::cancelled, [restore, done]() {
        restore();
        done(CaptureResult{false, {}, QCoreApplication::translate("infrastructure", "Captura cancelada")});
    });
    selector->showFullScreen();
    selector->activateWindow();
}

void ScreenCaptureService::grabRegion(QScreen* screen, Callback done) {
    withWindowHidden(screen, [done](QScreen* screen, std::function<void()> restore) {
        const QRect screenGeo = screen->geometry();
        const QPixmap full = screen->grabWindow(0);
        if (full.isNull()) { restore(); done(CaptureResult{false, {}, QCoreApplication::translate("infrastructure", "No se pudo capturar la pantalla")}); return; }
        selectRegion(full, screenGeo, std::move(restore), std::move(done));
    });
}

/// Wayland: el portal devuelve el escritorio completo (todas las pantallas, en píxeles físicos).
/// Para «Pantalla completa» y «Región» se recorta la pantalla elegida; «Ventana activa»
/// delega en el selector interactivo del compositor.
void ScreenCaptureService::captureViaPortal(CaptureMode mode, QScreen* screen, Callback done) {
    withWindowHidden(screen, [this, mode, done](QScreen* chosen, std::function<void()> restore) {
        const bool interactive = mode == CaptureMode::ActiveWindow;
        QPointer<QScreen> target = chosen;
        m_portal->take(interactive, [mode, target, restore, done](const QImage& image, const QString& error) {
            if (image.isNull()) { restore(); done(CaptureResult{false, {}, error}); return; }
            if (mode == CaptureMode::ActiveWindow) { restore(); done(CaptureResult{true, image, {}}); return; }

            QScreen* screen = target ? target.data() : QGuiApplication::primaryScreen();
            const QRect screenGeo = screen->geometry();
            const qreal dpr = screen->devicePixelRatio();
            // Recorte de la pantalla actual dentro del escritorio virtual (si la imagen lo abarca).
            const QRect virtualGeo = screen->virtualGeometry();
            QImage shot = image;
            if (image.size() != (QSizeF(screenGeo.size()) * dpr).toSize() && image.width() >= virtualGeo.width()) {
                const qreal scale = static_cast<qreal>(image.width()) / virtualGeo.width();
                const QRect rel = screenGeo.translated(-virtualGeo.topLeft());
                shot = image.copy(QRect(QPoint(qRound(rel.x() * scale), qRound(rel.y() * scale)),
                                        QSize(qRound(rel.width() * scale), qRound(rel.height() * scale))));
            }
            QPixmap full = QPixmap::fromImage(shot);
            full.setDevicePixelRatio(static_cast<qreal>(shot.width()) / std::max(1, screenGeo.width()));
            if (mode == CaptureMode::FullScreen) { restore(); done(CaptureResult{true, shot, {}}); return; }
            selectRegion(full, screenGeo, std::move(restore), done);
        });
    });
}

} // namespace qaflow
