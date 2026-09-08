#pragma once

#include "core/services/IGlobalHotkey.h"

#include <QAbstractNativeEventFilter>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>

namespace qaflow {

class PortalShortcuts;

/// Atajos globales del sistema, uno por plataforma:
///  - Windows: RegisterHotKey + WM_HOTKEY.
///  - Linux/X11: XGrabKey sobre la ventana raíz + eventos xcb (con y sin Bloq Num / Bloq Mayús).
///  - Linux/Wayland: portal org.freedesktop.portal.GlobalShortcuts (el compositor decide y puede
///    pedir confirmación al usuario; KDE Plasma ≥ 5.27, GNOME ≥ 48).
///  - macOS: Carbon RegisterEventHotKey.
/// Si la plataforma no ofrece nada, `bind()` devuelve false y `status()` explica por qué.
class GlobalHotkey : public QObject, public QAbstractNativeEventFilter, public IGlobalHotkey {
    Q_OBJECT
public:
    explicit GlobalHotkey(QObject* parent = nullptr);
    ~GlobalHotkey() override;

    bool bind(const QString& id, const QString& sequence, std::function<void()> onActivated) override;
    void unbind(const QString& id) override;
    bool isBound(const QString& id) const override;
    QString status() const override { return m_status; }

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

    /// Dispara el atajo con ese id nativo (lo usan los manejadores de la plataforma).
    void activate(int nativeId);

private:
    struct Binding {
        QString id;
        QString sequence;
        std::function<void()> callback;
        int nativeId = 0;
        unsigned mods = 0;      // máscara de modificadores nativa
        unsigned key = 0;       // tecla virtual / keysym / keycode
        bool bound = false;
        void* handle = nullptr; // EventHotKeyRef en macOS
    };

    bool registerNative(Binding& b);
    void unregisterNative(Binding& b);
    void installFilter();

    QList<Binding> m_bindings;
    QString m_status;
    int m_nextId = 1;
    bool m_filterInstalled = false;
    void* m_platform = nullptr;   // manejador Carbon en macOS
    PortalShortcuts* m_portal = nullptr;
};

} // namespace qaflow
