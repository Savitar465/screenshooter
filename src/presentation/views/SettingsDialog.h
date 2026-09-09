#pragma once

#include <QDialog>

namespace qaflow {

class SettingsStore;
class BugReportService;
class IGlobalHotkey;
class TestPublishService;

/// Ventana de ajustes: aloja la SettingsView y se abre desde «Archivo → Ajustes» (Ctrl+,).
/// No es modal, porque los cambios se guardan al instante y algunos (idioma, tema) reconstruyen
/// la ventana principal mientras esta sigue abierta.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    /// `hotkey` puede ser nullptr (tests); `captureBackend` es el texto informativo del método de captura.
    SettingsDialog(SettingsStore& settings, BugReportService& bugs, IGlobalHotkey* hotkey = nullptr,
                   const QString& captureBackend = QString(), TestPublishService* publish = nullptr, QWidget* parent = nullptr);

signals:
    /// Reenvía los avisos de la vista para que la ventana principal los muestre en su toast.
    void toast(const QString& message, const QString& color);
};

} // namespace qaflow
