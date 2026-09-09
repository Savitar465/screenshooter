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
    void createTest(const TrackerSettings& s, const PublishCase& c, std::function<void(const CreateTestResult&)> done) override;

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

    /// Tipo de incidencia con el que se crean los Tests: el de los ajustes o "Test", que es el que
    /// instala Zephyr.
    static QString testTypeName(const TrackerSettings& s);
    /// Descripción del Test creado a partir de un caso: sus precondiciones y de dónde sale.
    static QString testDescription(const PublishCase& c);

private:
    struct Job;   // estado de una publicación en curso (encadena decenas de peticiones)

    /// Lo que Zephyr necesita del proyecto: trabaja con ids numéricos, no con claves.
    struct Project {
        QString id;
        QString versionId;
        QString testTypeId;   // tipo de incidencia de los Tests; vacío si el proyecto no lo tiene
    };

    QNetworkRequest jira(const TrackerSettings& s, const QString& path) const;
    QNetworkRequest zephyr(const TrackerSettings& s, const QString& path) const;
    /// Averigua por qué ruta responde la API. Devuelve, además del motivo, si el fallo merece
    /// un reintento (red o 5xx) o es definitivo (aquí no hay Zephyr).
    void detect(const TrackerSettings& s, const QString& projectId, int candidate,
                std::function<void(bool ok, const QString& error, bool retryable)> done);
    /// Olvida la ruta detectada: la que quedó a medias de probar no vale para nadie.
    void forgetApi() { m_api.clear(); m_apiFor.clear(); }
    /// La ruta ya detectada para esta instancia, o una detección nueva si todavía no se sabe.
    void ensureApi(const TrackerSettings& s, const QString& projectId,
                   std::function<void(bool ok, const QString& error, bool retryable)> done);
    /// Resuelve los ids numéricos del proyecto: el suyo, el de la versión y el del tipo de
    /// incidencia con el que se crean los Tests.
    void resolveProject(const TrackerSettings& s, const QString& versionName,
                        std::function<void(bool, const Project& project, const QString& error)> done);

    /// Configuración regional del usuario de Jira, que es con la que Zephyr parsea las fechas.
    void resolveLocale(const std::shared_ptr<Job>& job, std::function<void()> done);
    /// `withDates` a false repite el ciclo sin fechas cuando Zephyr rechaza el formato de las suyas.
    void createCycle(const std::shared_ptr<Job>& job, bool withDates = true);
    /// Crea en Jira el issue de tipo Test que representa al caso (título y precondiciones).
    void postTestIssue(const TrackerSettings& s, const Project& project, const PublishCase& c,
                       std::function<void(bool ok, const QString& issueId, const QString& key, const QString& error, bool retryable)> done);
    /// Añade al Test los pasos del caso, uno a uno; devuelve con su motivo los que no entraron.
    void postTestSteps(const TrackerSettings& s, const QString& issueId, const PublishCase& c, int step,
                       const QStringList& failed, std::function<void(const QStringList& failed)> done);

    void nextCase(const std::shared_ptr<Job>& job);
    /// Estrena en Jira el Test del caso que aún no está enlazado a ninguno y sigue con su ejecución.
    void createTestForCase(const std::shared_ptr<Job>& job);
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
