#pragma once

#include <QDialog>

namespace qaflow {

struct AppContext;

/// Ventana de ajustes: aloja la SettingsView y se abre desde «Archivo → Ajustes» (Ctrl+,).
/// No es modal, porque los cambios se guardan al instante y algunos (idioma, tema) reconstruyen
/// la ventana principal mientras esta sigue abierta.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    /// Los servicios salen del contexto del proyecto; ver SettingsView para cuáles son opcionales.
    explicit SettingsDialog(const AppContext& ctx, QWidget* parent = nullptr);

signals:
    /// Reenvía los avisos de la vista para que la ventana principal los muestre en su toast.
    void toast(const QString& message, const QString& color);
};

} // namespace qaflow
