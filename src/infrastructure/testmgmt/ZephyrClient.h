#pragma once

#include "core/services/ITestManagement.h"
#include "infrastructure/http/HttpClient.h"

#include <QJsonObject>
#include <memory>

namespace qaflow {

/// Cliente de Zephyr for Jira (ZAPI) sobre la misma instancia y las mismas credenciales que Jira.
///
/// ZAPI se sirve por dos rutas según la versión del plugin:
///  - `/rest/zapi/latest` — la API pública, incluida de fábrica desde Zephyr 5.6 (antes, add-on aparte);
///  - `/rest/zephyr/latest` — los mismos servicios publicados por el propio plugin, que es lo único
///    que hay en las versiones anteriores (por ejemplo la 5.3) y lo que usa su interfaz web.
/// El cliente prueba la primera y, si no responde, se queda con la segunda; `apiPath()` dice cuál salió.
class ZephyrClient : public HttpClient, public ITestManagement {
    Q_OBJECT
public:
    explicit ZephyrClient(QObject* parent = nullptr) : HttpClient(parent) {}

    void testConnection(const TrackerSettings& s, std::function<void(const ConnectionResult&)> done) override;
    void publish(const TrackerSettings& s, const PublishRequest& request, std::function<void(const PublishResult&)> done) override;

    /// Ruta detectada de la API ("/rest/zapi/latest"); vacía mientras no se haya detectado.
    QString apiPath() const { return m_api; }
    /// Rutas por las que se busca la API, en orden de preferencia.
    static QStringList apiCandidates();

    /// Estados de Zephyr: 1 pasa, 2 falla, 3 en curso, 4 bloqueado, -1 sin ejecutar.
    static int zephyrStatus(Verdict v);
    static int zephyrStatus(StepResult r);

    /// Fecha de un ciclo en el formato que espera Zephyr ("9/sep/26"), con la configuración
    /// regional del usuario de Jira; vacía si la fecha no lo es.
    static QString cycleDate(const QDateTime& dt, const QString& jiraLocale);

private:
    struct Job;   // estado de una publicación en curso (encadena decenas de peticiones)

    QNetworkRequest jira(const TrackerSettings& s, const QString& path) const;
    QNetworkRequest zephyr(const TrackerSettings& s, const QString& path) const;
    /// Averigua por qué ruta responde la API. Devuelve, además del motivo, si el fallo merece
    /// un reintento (red o 5xx) o es definitivo (aquí no hay Zephyr).
    void detect(const TrackerSettings& s, const QString& projectId, int candidate,
                std::function<void(bool ok, const QString& error, bool retryable)> done);
    /// Olvida la ruta detectada: la que quedó a medias de probar no vale para nadie.
    void forgetApi() { m_api.clear(); m_apiFor.clear(); }
    /// Resuelve el id numérico del proyecto y sus versiones a partir de la clave configurada.
    void resolveProject(const TrackerSettings& s, const QString& versionName,
                        std::function<void(bool, const QString& projectId, const QString& versionId, const QString& error)> done);

    /// Configuración regional del usuario de Jira, que es con la que Zephyr parsea las fechas.
    void resolveLocale(const std::shared_ptr<Job>& job, std::function<void()> done);
    /// `withDates` a false repite el ciclo sin fechas cuando Zephyr rechaza el formato de las suyas.
    void createCycle(const std::shared_ptr<Job>& job, bool withDates = true);
    void nextCase(const std::shared_ptr<Job>& job);
    void executeCase(const std::shared_ptr<Job>& job, const QString& issueId);
    /// Lee los resultados de paso que Zephyr crea con la ejecución y reparte las evidencias.
    void readStepResults(const std::shared_ptr<Job>& job, const QString& issueId);
    void writeNextStep(const std::shared_ptr<Job>& job, const QString& issueId);
    void uploadNext(const std::shared_ptr<Job>& job);
    /// PUT con comentario y, si el servidor lo rechaza, reintento sin él: `comment` no aparece en la
    /// documentación de ZAPI aunque las instalaciones lo aceptan, y el veredicto importa más que la nota.
    void putWithComment(const QNetworkRequest& req, QJsonObject body, const QString& comment, Handler done);

    QString m_api;        // ruta detectada, para no repetir la detección en cada publicación
    QString m_apiFor;     // instancia para la que vale `m_api`
    QString m_locale;     // configuración regional del usuario de Jira ("es_ES")
    QString m_localeFor;  // instancia para la que vale `m_locale`
};

} // namespace qaflow
