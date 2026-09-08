#pragma once

#include <QString>
#include <functional>

namespace qaflow {

/// Atajo de teclado registrado en el sistema: se dispara aunque QAflow no tenga el foco.
/// La implementación depende de la plataforma (RegisterHotKey, XGrabKey, Carbon, portal de
/// Wayland); cuando ninguna está disponible `bind()` devuelve false y el atajo sólo funciona
/// dentro de la aplicación (QAction de ámbito aplicación).
class IGlobalHotkey {
public:
    virtual ~IGlobalHotkey() = default;

    /// Registra `sequence` (formato de QKeySequence, p. ej. "Ctrl+Shift+S") con `id` como clave.
    /// Sustituye el registro anterior del mismo id. Devuelve false si el sistema lo rechaza.
    virtual bool bind(const QString& id, const QString& sequence, std::function<void()> onActivated) = 0;
    virtual void unbind(const QString& id) = 0;
    virtual bool isBound(const QString& id) const = 0;

    /// Texto para Ajustes: "Atajo global registrado (Windows)", "No disponible en Wayland sin portal"…
    virtual QString status() const = 0;
};

} // namespace qaflow
