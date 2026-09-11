#pragma once

#include "core/models/Settings.h"
#include "core/services/ISecretStore.h"
#include "core/services/ISettingsRepository.h"

#include <QObject>
#include <memory>

namespace qaflow {

/// Ajustes del gestor de incidencias, de captura y generales (idioma, tema, bandeja). El token del gestor vive en el
/// ISecretStore (llavero del sistema); el repositorio sólo guarda lo no secreto.
class SettingsStore : public QObject {
    Q_OBJECT
public:
    SettingsStore(std::shared_ptr<ISettingsRepository> repo, std::shared_ptr<ISecretStore> secrets = nullptr, QObject* parent = nullptr);

    void load();

    const TrackerSettings& tracker() const { return m_tracker; }
    const CaptureSettings& capture() const { return m_capture; }
    const AppSettings& app() const { return m_app; }
    const RunShortcuts& runShortcuts() const { return m_runShortcuts; }

    void updateTracker(const std::function<void(TrackerSettings&)>& mutate);
    void updateCapture(const std::function<void(CaptureSettings&)>& mutate);
    void updateApp(const std::function<void(AppSettings&)>& mutate);
    void updateRunShortcuts(const std::function<void(RunShortcuts&)>& mutate);

    /// Dónde se guarda el token ("Llavero del sistema (secret-tool)", "Sin cifrar en QAflow.conf").
    QString secretBackend() const;
    bool secretsAreSecure() const;

signals:
    /// Cambios persistidos (no se emite al recargar los ajustes compartidos).
    void saved();
    void trackerChanged();
    void captureChanged();
    /// Idioma, tema o comportamiento de bandeja. Idioma y tema requieren reconstruir la ventana.
    void appChanged();
    /// Atajos de la ejecución: hay que volver a registrarlos en el sistema.
    void runShortcutsChanged();

private:
    static QString tokenKey(TrackerKind kind);

    std::shared_ptr<ISettingsRepository> m_repo;
    std::shared_ptr<ISecretStore> m_secrets;
    TrackerSettings m_tracker;
    CaptureSettings m_capture;
    AppSettings m_app;
    RunShortcuts m_runShortcuts;
};

} // namespace qaflow
