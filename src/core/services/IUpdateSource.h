#pragma once

#include "core/models/Update.h"

#include <functional>

namespace qaflow {

/// De dónde se leen las versiones publicadas de QAflow (las releases de GitHub). Asíncrona: responde por
/// callback en el hilo principal.
class IUpdateSource {
public:
    virtual ~IUpdateSource() = default;
    /// Las versiones publicadas más recientes, finales y previas, sin borradores. El canal lo aplica
    /// quien pregunta.
    virtual void fetchReleases(std::function<void(const UpdateCheckResult&)> done) = 0;

    struct DownloadResult {
        bool ok = false;
        QString error;
    };
    /// Descarga un fichero de la release a `path` sin cargarlo en memoria. El fichero sólo queda si la
    /// descarga terminó bien. `progress` recibe bytes recibidos y totales (-1 si no se saben).
    virtual void download(const QUrl& url, const QString& path, std::function<void(qint64, qint64)> progress,
                          std::function<void(const DownloadResult&)> done) = 0;
    /// Corta las descargas en curso: terminan con error.
    virtual void cancelDownloads() = 0;
};

/// Dónde se guardan las preferencias del buscador de actualizaciones. Son de la aplicación, no de un
/// proyecto.
class IUpdatePreferencesRepository {
public:
    virtual ~IUpdatePreferencesRepository() = default;
    virtual UpdatePreferences load() = 0;
    virtual void save(const UpdatePreferences& p) = 0;
};

} // namespace qaflow
