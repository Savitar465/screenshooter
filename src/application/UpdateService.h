#pragma once

#include "core/models/Update.h"
#include "core/services/IUpdateInstaller.h"
#include "core/services/IUpdateSource.h"

#include <QObject>
#include <QTimer>
#include <functional>
#include <memory>
#include <optional>

namespace qaflow {

/// Busca versiones nuevas de QAflow, como los IDE de JetBrains: en segundo plano al rato de arrancar y
/// después una vez al día, sin interrumpir; avisa una sola vez de cada versión y recuerda las que el
/// usuario decidió omitir. La búsqueda manual ("Buscar actualizaciones…") muestra también las omitidas.
///
/// Donde la instalación lo permite (`setInstaller`), además descarga el paquete de la versión nueva, lo
/// comprueba —la firma de las sumas con la clave de quien publica QAflow y la suma del paquete— y lo
/// instala para la próxima vez que se abra. Si no, sólo avisa y se descarga a mano.
///
/// Es de la aplicación, no de un proyecto: una sola instancia para todas las ventanas.
class UpdateService : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Checking, UpToDate, Available, Failed };
    /// La instalación de la versión disponible: descargando, lista para instalar, instalada (se aplica al
    /// cerrar QAflow) o fallida.
    enum class InstallState { Idle, Downloading, Ready, Scheduled, Failed };

    /// Cada cuánto se vuelve a buscar sola.
    static constexpr qint64 kCheckIntervalSecs = 24 * 60 * 60;

    UpdateService(std::shared_ptr<IUpdateSource> source, std::shared_ptr<IUpdatePreferencesRepository> prefs,
                  Version current, QObject* parent = nullptr);

    const Version& currentVersion() const { return m_current; }
    const UpdatePreferences& preferences() const { return m_prefs; }
    State state() const { return m_state; }
    /// Por qué falló la última búsqueda (con `State::Failed`).
    QString lastError() const { return m_error; }
    /// La versión nueva encontrada, si la hay.
    const std::optional<UpdateRelease>& available() const { return m_available; }

    void setAutoCheck(bool on);
    /// Cambiar de canal vuelve a buscar (si la búsqueda automática está activa): lo que había cambia.
    void setChannel(UpdateChannel channel);

    /// Arranca las búsquedas automáticas: la primera al rato de abrir y después cada hora se mira si ya
    /// toca (`kCheckIntervalSecs` desde la última que respondió).
    void start();
    /// ¿Toca buscar sola? Con la búsqueda automática activa y sin búsquedas recientes.
    bool isDue(const QDateTime& now) const;
    void checkIfDue();
    /// Búsqueda pedida por el usuario: muestra también la versión omitida. `done` corre al terminar; el
    /// resultado se lee en `state()`, `available()` y `lastError()`.
    void checkNow(std::function<void()> done = {});
    /// "Omitir esta versión": la búsqueda automática no vuelve a avisar de ella (sí de una posterior).
    void skip(const Version& version);

    /// Permite instalar solo: el instalador de esta instalación, con qué se comprueba la firma de las
    /// sumas y dónde se descargan los paquetes. Sin instalador o sin verificador sólo se ofrece la descarga.
    void setInstaller(std::shared_ptr<IUpdateInstaller> installer, std::shared_ptr<ISignatureVerifier> verifier,
                      QString downloadDir);
    /// ¿Se puede instalar sola la versión disponible? Hace falta instalador, su paquete en la release y las
    /// sumas firmadas.
    bool canInstall() const;
    /// Cómo se instala ("AppImage"…), para la interfaz; vacío sin instalador.
    QString installKind() const;
    InstallState installState() const { return m_installState; }
    QString installError() const { return m_installError; }
    /// Descarga y comprueba el paquete de la versión disponible: termina en `Ready` o en `Failed`.
    void downloadUpdate();
    /// Corta la descarga en curso (vuelve a `Idle`, sin error).
    void cancelDownload();
    /// Instala el paquete descargado y comprobado: termina en `Scheduled` o en `Failed`. Con `relaunch`,
    /// QAflow vuelve a abrirse en cuanto se cierre; cerrarla le toca a quien llama.
    bool install(bool relaunch);

signals:
    /// Cambió el estado, la versión disponible o las preferencias.
    void changed();
    /// Una búsqueda automática encontró una versión de la que todavía no se había avisado.
    void updateAvailable(const UpdateRelease& release);
    /// Cambió el estado de la instalación.
    void installChanged();
    /// Bytes del paquete recibidos y totales (-1 si no se saben).
    void downloadProgress(qint64 received, qint64 total);

private:
    void check(bool manual, std::function<void()> done);
    void finish(const UpdateCheckResult& result);
    void setInstallState(InstallState state, const QString& error = {});
    const UpdateAsset* packageAsset(const UpdateRelease& release) const;

    std::shared_ptr<IUpdateSource> m_source;
    std::shared_ptr<IUpdatePreferencesRepository> m_repo;
    Version m_current;
    UpdatePreferences m_prefs;
    State m_state = State::Idle;
    QString m_error;
    std::optional<UpdateRelease> m_available;
    bool m_manual = false;                       // la búsqueda en curso la pidió el usuario
    QList<std::function<void()>> m_waiting;      // callbacks de `checkNow` pendientes
    QString m_announced;                         // última versión de la que se avisó en esta sesión
    QTimer m_poll;

    std::shared_ptr<IUpdateInstaller> m_installer;
    std::shared_ptr<ISignatureVerifier> m_verifier;
    QString m_downloadDir;
    InstallState m_installState = InstallState::Idle;
    QString m_installError;
    Version m_installVersion;   // de qué versión es la instalación en curso
    QString m_package;          // el paquete descargado y comprobado
    int m_downloadId = 0;       // las respuestas de una descarga cancelada no cuentan
};

} // namespace qaflow
