#include "ZephyrClient.h"

#include "infrastructure/tracker/JiraAuth.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocale>
#include <QUrl>

#include <algorithm>

namespace qaflow {

namespace {
constexpr const char* kUnscheduled = "-1";   // versión "Unscheduled" de Zephyr

/// Una evidencia con su destino ya resuelto: la ejecución o el resultado de un paso concreto.
struct Upload {
    QString path;
    QString entityId;
    QByteArray entityType;   // EXECUTION o TESTSTEPRESULT
};
} // namespace

/// Estado de una publicación: se pasa por los callbacks encadenados de todo el proceso.
struct ZephyrClient::Job {
    TrackerSettings settings;
    PublishRequest request;
    std::function<void(const PublishResult&)> done;
    PublishResult result;
    Project project;
    QString locale;            // la del usuario de Jira, para las fechas del ciclo
    int index = 0;             // caso que se está publicando
    QString executionId;       // ejecución del caso en curso
    /// Resultados de paso que Zephyr creó para esa ejecución, emparejados con lo que se ejecutó.
    QList<QPair<QString, RunRecordStep>> pendingSteps;
    QList<Upload> uploads;     // evidencias del caso, ya resueltas a su destino

    const PublishCase& current() const { return request.cases[index]; }
    /// ¿Se actualiza un ciclo ya publicado en vez de crear uno?
    bool updating() const { return !request.cycleId.trimmed().isEmpty(); }
    void skip(const QString& reason) { result.skipped << reason; }
    void finish() { done(result); }
};

QStringList ZephyrClient::apiCandidates() {
    return {QStringLiteral("/rest/zapi/latest"), QStringLiteral("/rest/zephyr/latest")};
}

int ZephyrClient::zephyrStatus(Verdict v) {
    switch (v) {
        case Verdict::Superado: return 1;
        case Verdict::Fallido: return 2;
        case Verdict::Bloqueado: return 4;
    }
    return -1;
}

int ZephyrClient::zephyrStatus(StepResult r) {
    switch (r) {
        case StepResult::Pass: return 1;
        case StepResult::Fail: return 2;
        case StepResult::Block: return 4;
        case StepResult::Skip: return -1;   // N/A: en Zephyr se queda sin ejecutar
    }
    return -1;
}

QString ZephyrClient::cycleDate(const QDateTime& dt, const QString& jiraLocale) {
    if (!dt.isValid()) return {};
    // Zephyr espera "9/sep/26" y lo parsea con Joda-Time, que distingue mayúsculas, en la
    // configuración regional del usuario de Jira: en un Jira en español "9/Sep/26" es un error.
    // Y las abreviaturas son las de `java.locale.providers=COMPAT`, con el que arranca Jira,
    // siempre de tres letras — no las del CLDR moderno que da Qt ("sept"), que también rechaza.
    const QLocale locale = jiraLocale.isEmpty() ? QLocale::c() : QLocale(jiraLocale);
    const QDate date = dt.date();
    // Las cifras van en ASCII a propósito: `QLocale::toString` las daría en el sistema de
    // numeración del idioma, y Zephyr sólo entiende las arábigas.
    return QStringLiteral("%1/%2/%3").arg(QString::number(date.day()),
                                          locale.toString(date, QStringLiteral("MMM")).left(3),
                                          QString::number(date.year() % 100).rightJustified(2, QLatin1Char('0')));
}

QString ZephyrClient::testTypeName(const TrackerSettings& s) {
    const QString configured = s.zephyrTestType.trimmed();
    // "Test" es el tipo de incidencia que instala Zephyr; en un Jira traducido se llama de otra
    // manera y entonces lo dicen los ajustes.
    return configured.isEmpty() ? QStringLiteral("Test") : configured;
}

QString ZephyrClient::testDescription(const PublishCase& c, const QString& cycleName) {
    QStringList parts;
    if (!c.preconditions.trimmed().isEmpty()) parts << c.preconditions.trimmed();
    // De dónde salió el Test: el caso de QAflow es el original, y el ciclo lo distingue de los
    // Tests del mismo caso en otros ciclos, que son otros issues.
    parts << (cycleName.trimmed().isEmpty()
                  ? QCoreApplication::translate("infrastructure", "Creado por QAflow a partir del caso %1").arg(c.caseId)
                  : QCoreApplication::translate("infrastructure", "Creado por QAflow a partir del caso %1 para el ciclo «%2»").arg(c.caseId, cycleName.trimmed()));
    return parts.join(QStringLiteral("\n\n"));
}

QNetworkRequest ZephyrClient::jira(const TrackerSettings& s, const QString& path) const {
    QNetworkRequest req = jsonRequest(s.baseUrl() + path);
    req.setRawHeader("Authorization", jiraAuthorization(s));
    return req;
}

QNetworkRequest ZephyrClient::zephyr(const TrackerSettings& s, const QString& path) const {
    QNetworkRequest req = jira(s, m_api + path);
    // Jira exige esta cabecera para aceptar peticiones a los servicios internos de los plugins.
    req.setRawHeader("X-Requested-With", "XMLHttpRequest");
    req.setRawHeader("X-Atlassian-Token", "no-check");
    return req;
}

void ZephyrClient::putWithComment(const QNetworkRequest& req, QJsonObject body, const QString& comment, Handler done) {
    const QString text = comment.trimmed();
    if (text.isEmpty()) { sendCustom("PUT", req, QJsonDocument(body).toJson(QJsonDocument::Compact), std::move(done)); return; }
    QJsonObject withComment = body;
    withComment[QStringLiteral("comment")] = text;
    sendCustom("PUT", req, QJsonDocument(withComment).toJson(QJsonDocument::Compact),
               [this, req, body, done](const Response& r) {
                   if (r.ok || r.retryable) { done(r); return; }
                   // Rechazo del contenido: reintento sin la nota, que el veredicto es lo importante.
                   sendCustom("PUT", req, QJsonDocument(body).toJson(QJsonDocument::Compact), done);
               });
}

// ---- Detección de la API y del proyecto -----------------------------------------------------

void ZephyrClient::detect(const TrackerSettings& s, const QString& projectId, int candidate,
                          std::function<void(bool, const QString&, bool)> done) {
    const QStringList candidates = apiCandidates();
    if (candidate >= candidates.size()) {
        // Ninguna respondió: la ruta que quedó puesta al probar no vale, y volver a intentarlo
        // tampoco arreglaría un plugin que no está.
        forgetApi();
        done(false, QCoreApplication::translate("infrastructure", "Jira responde pero no se encuentra la API de Zephyr (ni /rest/zapi/latest ni "
                        "/rest/zephyr/latest). Comprueba que el plugin de Zephyr está instalado y activo."),
             false);
        return;
    }
    m_api = candidates[candidate];
    // Listar los ciclos del proyecto: existe en las dos rutas y comprueba de paso los permisos.
    const QString path = QStringLiteral("/cycle?projectId=%1&versionId=%2").arg(projectId, QString::fromLatin1(kUnscheduled));
    get(zephyr(s, path), [this, s, projectId, candidate, done](const Response& r) {
        if (r.ok) { m_apiFor = s.baseUrl(); done(true, {}, false); return; }
        // 404 = esa ruta no existe aquí; cualquier otro fallo (401, red) es real y no se disimula.
        if (r.status != 404 && r.status != 405) { forgetApi(); done(false, r.error, r.retryable); return; }
        detect(s, projectId, candidate + 1, done);
    });
}

void ZephyrClient::ensureApi(const TrackerSettings& s, const QString& projectId,
                             std::function<void(bool, const QString&, bool)> done) {
    if (!m_api.isEmpty() && m_apiFor == s.baseUrl()) { done(true, {}, false); return; }
    detect(s, projectId, 0, std::move(done));
}

void ZephyrClient::resolveProject(const TrackerSettings& s, const QString& versionName,
                                  std::function<void(bool, const Project&, const QString&)> done) {
    if (s.project.trimmed().isEmpty()) { done(false, {}, QCoreApplication::translate("infrastructure", "Indica la clave del proyecto")); return; }
    // Zephyr trabaja con ids numéricos, no con claves: hay que traducir SHOP → 13500, la versión a
    // su id y el tipo de incidencia "Test" al suyo, que es con el que se crean los Tests que faltan.
    const QString typeWanted = testTypeName(s);
    get(jira(s, QStringLiteral("/rest/api/2/project/%1").arg(s.project.trimmed())), [versionName, typeWanted, done](const Response& r) {
        if (!r.ok) { done(false, {}, r.error); return; }
        const QJsonObject p = r.json.object();
        Project project;
        project.id = p[QStringLiteral("id")].toString();
        project.versionId = QString::fromLatin1(kUnscheduled);
        if (project.id.isEmpty()) { done(false, {}, QCoreApplication::translate("infrastructure", "Jira no devolvió el id del proyecto")); return; }
        const QString wanted = versionName.trimmed();
        if (!wanted.isEmpty()) {
            for (const auto& v : p[QStringLiteral("versions")].toArray()) {
                const QJsonObject ver = v.toObject();
                if (ver[QStringLiteral("name")].toString().compare(wanted, Qt::CaseInsensitive) == 0) {
                    project.versionId = ver[QStringLiteral("id")].toString();
                    break;
                }
            }
            if (project.versionId == QString::fromLatin1(kUnscheduled)) {
                done(false, {}, QCoreApplication::translate("infrastructure", "El proyecto no tiene la versión indicada: %1").arg(wanted));
                return;
            }
        }
        // Que el tipo no esté no impide publicar los casos que ya traen su Test: sólo se echa en
        // falta al crear uno, y allí se dice con su nombre.
        for (const auto& t : p[QStringLiteral("issueTypes")].toArray()) {
            const QJsonObject type = t.toObject();
            if (type[QStringLiteral("name")].toString().compare(typeWanted, Qt::CaseInsensitive) == 0) {
                project.testTypeId = type[QStringLiteral("id")].toString();
                break;
            }
        }
        done(true, project, {});
    });
}

void ZephyrClient::testConnection(const TrackerSettings& s, std::function<void(const ConnectionResult&)> done) {
    resolveProject(s, QString(), [this, s, done](bool ok, const Project& project, const QString& error) {
        if (!ok) { done(ConnectionResult{false, {}, error}); return; }
        detect(s, project.id, 0, [this, s, project, done](bool found, const QString& error, bool) {
            if (!found) { done(ConnectionResult{false, {}, error}); return; }
            QString where = QCoreApplication::translate("infrastructure", "API de Zephyr en %1").arg(m_api);
            // Sin ese tipo de incidencia no se pueden crear los Tests que faltan: mejor saberlo aquí
            // que caso por caso al publicar.
            if (project.testTypeId.isEmpty())
                where += QCoreApplication::translate("infrastructure", " · el proyecto no tiene el tipo de incidencia «%1»").arg(testTypeName(s));
            done(ConnectionResult{true, where, {}});
        });
    });
}

// ---- Publicación -----------------------------------------------------------------------------

void ZephyrClient::publish(const TrackerSettings& s, const PublishRequest& request, std::function<void(const PublishResult&)> done) {
    auto job = std::make_shared<Job>();
    job->settings = s;
    job->request = request;
    job->done = std::move(done);
    if (request.cases.isEmpty()) {
        job->result.error = QCoreApplication::translate("infrastructure", "El ciclo no tiene ninguna ejecución que publicar");
        job->finish();
        return;
    }
    resolveProject(s, request.versionName, [this, job](bool ok, const Project& project, const QString& error) {
        if (!ok) { job->result.error = error; job->finish(); return; }
        job->project = project;
        ensureApi(job->settings, project.id, [this, job](bool found, const QString& detectError, bool retryable) {
            if (!found) { job->result.error = detectError; job->result.retryable = retryable; job->finish(); return; }
            resolveLocale(job, [this, job]() { if (job->updating()) checkCycle(job); else createCycle(job); });
        });
    });
}

void ZephyrClient::resolveLocale(const std::shared_ptr<Job>& job, std::function<void()> done) {
    if (m_localeFor == job->settings.baseUrl()) { job->locale = m_locale; done(); return; }
    get(jira(job->settings, QStringLiteral("/rest/api/2/myself")), [this, job, done](const Response& r) {
        // Si Jira no la dice, se formatea en inglés, que es como sale un Jira recién instalado.
        m_locale = r.ok ? r.json.object()[QStringLiteral("locale")].toString() : QString();
        m_localeFor = job->settings.baseUrl();
        job->locale = m_locale;
        done();
    });
}

void ZephyrClient::createCycle(const std::shared_ptr<Job>& job, bool withDates) {
    const QJsonObject body{
        {"name", job->request.cycleName},
        {"projectId", job->project.id},
        {"versionId", job->project.versionId},
        {"description", job->request.description},
        {"startDate", withDates ? cycleDate(job->request.startedAt, job->locale) : QString()},
        {"endDate", withDates ? cycleDate(job->request.finishedAt, job->locale) : QString()},
        {"build", QString()},
        {"environment", QString()},
    };
    postJson(zephyr(job->settings, QStringLiteral("/cycle")), QJsonDocument(body), [this, job, withDates](const Response& r) {
        if (!r.ok) {
            // Zephyr rechaza el ciclo entero con un 406 y un `date` cuando el formato de la fecha no
            // le cuadra, y ese formato depende de cómo esté configurada cada instalación. El ciclo
            // importa más que sus fechas: se repite sin ellas, como se hace con el comentario del paso.
            if (withDates && r.json.object().contains(QStringLiteral("date"))) { createCycle(job, false); return; }
            job->result.error = r.error;
            job->result.retryable = r.retryable;
            job->finish();
            return;
        }
        job->result.cycleId = r.json.object()[QStringLiteral("id")].toString();
        if (job->result.cycleId.isEmpty()) {
            job->result.error = QCoreApplication::translate("infrastructure", "Zephyr no devolvió el id del ciclo creado");
            job->finish();
            return;
        }
        nextCase(job);
    });
}

void ZephyrClient::checkCycle(const std::shared_ptr<Job>& job) {
    const QString cycleId = job->request.cycleId.trimmed();
    get(zephyr(job->settings, QStringLiteral("/cycle/%1").arg(cycleId)), [this, job, cycleId](const Response& r) {
        if (!r.ok) {
            // Borrado en Zephyr desde que se publicó: no hay nada que actualizar, y decirlo evita
            // que las ejecuciones se cuelguen de un ciclo que ya no está.
            job->result.error = r.status == 404 || r.status == 400
                                    ? QCoreApplication::translate("infrastructure", "El ciclo %1 ya no existe en Zephyr: publica los resultados como ciclo nuevo").arg(cycleId)
                                    : r.error;
            job->result.retryable = r.retryable;
            job->finish();
            return;
        }
        job->result.cycleId = cycleId;
        nextCase(job);
    });
}

void ZephyrClient::nextCase(const std::shared_ptr<Job>& job) {
    if (job->index >= job->request.cases.size()) {
        job->result.ok = true;
        job->finish();
        return;
    }
    const PublishCase& c = job->current();
    // La ejecución manda: si aún no tiene Test (este informe no se había publicado), se le crea
    // uno a partir del caso; si lo tiene, es una republicación y se reutiliza.
    if (c.testKey.trimmed().isEmpty()) { createTestForCase(job); return; }
    // Zephyr crea la ejecución con el id numérico del issue, no con su clave.
    get(jira(job->settings, QStringLiteral("/rest/api/2/issue/%1?fields=id").arg(c.testKey.trimmed())), [this, job](const Response& r) {
        const PublishCase& c = job->current();
        if (!r.ok) {
            job->skip(QCoreApplication::translate("infrastructure", "%1: no se encontró el Test %2").arg(c.caseId, c.testKey));
            ++job->index;
            nextCase(job);
            return;
        }
        executeCase(job, r.json.object()[QStringLiteral("id")].toString());
    });
}

void ZephyrClient::postTestIssue(const TrackerSettings& s, const Project& project, const PublishCase& c, const QString& cycleName,
                                 std::function<void(bool, const QString&, const QString&, const QString&, bool)> done) {
    const QJsonObject fields{
        {"project", QJsonObject{{"id", project.id}}},
        {"issuetype", QJsonObject{{"id", project.testTypeId}}},
        {"summary", c.title},
        {"description", testDescription(c, cycleName)},
        // Las mismas etiquetas con las que JiraClient crea los defectos: buscar "qaflow" en Jira
        // saca lo que ha salido de aquí, y el id del caso lo empareja con su original.
        {"labels", QJsonArray{QStringLiteral("qaflow"), c.caseId}},
    };
    postJson(jira(s, QStringLiteral("/rest/api/2/issue")), QJsonDocument(QJsonObject{{"fields", fields}}), [done](const Response& r) {
        const QString issueId = r.json.object()[QStringLiteral("id")].toString();
        if (!r.ok) { done(false, {}, {}, r.error, r.retryable); return; }
        if (issueId.isEmpty()) {
            done(false, {}, {}, QCoreApplication::translate("infrastructure", "Jira no devolvió el issue creado"), false);
            return;
        }
        done(true, issueId, r.json.object()[QStringLiteral("key")].toString(), {}, false);
    });
}

void ZephyrClient::postTestSteps(const TrackerSettings& s, const QString& issueId, const PublishCase& c, int step,
                                 const QStringList& failed, std::function<void(const QStringList&)> done) {
    if (step >= c.design.size()) { done(failed); return; }
    const TestStep& design = c.design[step];
    // El paso de Zephyr son tres campos; QAflow no tiene datos de prueba aparte de la acción.
    const QJsonObject body{
        {"step", design.action},
        {"data", QString()},
        {"result", design.expected},
    };
    postJson(zephyr(s, QStringLiteral("/teststep/%1").arg(issueId)), QJsonDocument(body), [this, s, issueId, c, step, failed, done](const Response& r) {
        // Un paso que no entra no tira el Test: se anota y se sigue con el resto, que valen igual.
        QStringList sofar = failed;
        if (!r.ok)
            sofar << QCoreApplication::translate("infrastructure", "%1: no se pudo crear el paso %2 del Test · %3")
                         .arg(c.caseId, QString::number(step + 1), r.error);
        postTestSteps(s, issueId, c, step + 1, sofar, done);
    });
}

void ZephyrClient::createTestForCase(const std::shared_ptr<Job>& job) {
    const PublishCase& c = job->current();
    if (job->project.testTypeId.isEmpty()) {
        job->skip(QCoreApplication::translate("infrastructure", "%1: el proyecto no tiene el tipo de incidencia «%2» con el que crear el Test")
                      .arg(c.caseId, testTypeName(job->settings)));
        ++job->index;
        nextCase(job);
        return;
    }
    postTestIssue(job->settings, job->project, c, job->request.cycleName, [this, job](bool created, const QString& issueId, const QString& key,
                                                              const QString& error, bool) {
        const PublishCase& c = job->current();
        if (!created) {
            job->skip(QCoreApplication::translate("infrastructure", "%1: no se pudo crear el Test · %2").arg(c.caseId, error));
            ++job->index;
            nextCase(job);
            return;
        }
        ++job->result.testsCreated;
        job->result.createdTests.insert(c.caseId, key);
        // La clave recién creada también vale para los avisos que vienen después de este punto.
        job->request.cases[job->index].testKey = key;
        postTestSteps(job->settings, issueId, c, 0, {}, [this, job, issueId](const QStringList& failed) {
            // El veredicto del paso que no llegó a existir se cuenta luego, al leer los resultados.
            job->result.skipped += failed;
            executeCase(job, issueId);
        });
    });
}

void ZephyrClient::executeCase(const std::shared_ptr<Job>& job, const QString& issueId) {
    if (!job->updating()) { createExecution(job, issueId); return; }
    findExecution(job, issueId, [this, job, issueId](const QString& executionId) {
        // Sin ejecución en el ciclo: se quedó fuera la vez anterior (o su Test se acaba de crear).
        if (executionId.isEmpty()) { createExecution(job, issueId); return; }
        job->executionId = executionId;
        markExecution(job, issueId);
    });
}

void ZephyrClient::findExecution(const std::shared_ptr<Job>& job, const QString& issueId, std::function<void(const QString&)> done) {
    get(zephyr(job->settings, QStringLiteral("/execution?issueId=%1").arg(issueId)), [job, done](const Response& r) {
        if (!r.ok) { done({}); return; }
        // ZAPI contesta {"executions":[…]}; la ruta del plugin, a veces la lista a secas.
        const QJsonArray list = r.json.isArray() ? r.json.array() : r.json.object()[QStringLiteral("executions")].toArray();
        const QString wanted = job->request.cycleId.trimmed();
        auto asString = [](const QJsonValue& v) { return v.isString() ? v.toString() : QString::number(v.toInt()); };
        for (const auto& v : list) {
            const QJsonObject e = v.toObject();
            if (asString(e[QStringLiteral("cycleId")]) != wanted) continue;
            done(asString(e[QStringLiteral("id")]));
            return;
        }
        done({});
    });
}

void ZephyrClient::createExecution(const std::shared_ptr<Job>& job, const QString& issueId) {
    const QJsonObject body{
        {"issueId", issueId},
        {"versionId", job->project.versionId},
        {"cycleId", job->result.cycleId},
        {"projectId", job->project.id},
    };
    postJson(zephyr(job->settings, QStringLiteral("/execution")), QJsonDocument(body), [this, job, issueId](const Response& r) {
        const PublishCase& c = job->current();
        if (!r.ok) {
            job->skip(QCoreApplication::translate("infrastructure", "%1: no se pudo añadir al ciclo · %2").arg(c.caseId, r.error));
            ++job->index;
            nextCase(job);
            return;
        }
        // La respuesta es un objeto indexado por el id de la ejecución creada: {"32": {...}}.
        const QJsonObject created = r.json.object();
        job->executionId = created.keys().isEmpty() ? QString() : created.keys().first();
        if (job->executionId.isEmpty()) {
            job->skip(QCoreApplication::translate("infrastructure", "%1: Zephyr no devolvió el id de la ejecución").arg(c.caseId));
            ++job->index;
            nextCase(job);
            return;
        }
        markExecution(job, issueId);
    });
}

void ZephyrClient::markExecution(const std::shared_ptr<Job>& job, const QString& issueId) {
    const PublishCase& c = job->current();
    const QJsonObject status{{"status", QString::number(zephyrStatus(c.verdict))}};
    putWithComment(zephyr(job->settings, QStringLiteral("/execution/%1/execute").arg(job->executionId)), status,
                   QCoreApplication::translate("infrastructure", "Publicado por QAflow · %1").arg(formatDuration(c.durationSecs)),
                   [this, job, issueId](const Response& exec) {
                       const PublishCase& c = job->current();
                       if (!exec.ok) {
                           job->skip(QCoreApplication::translate("infrastructure", "%1: no se pudo fijar el veredicto · %2").arg(c.caseId, exec.error));
                           ++job->index;
                           nextCase(job);
                           return;
                       }
                       ++job->result.executions;
                       readStepResults(job, issueId);
                   });
}

void ZephyrClient::readStepResults(const std::shared_ptr<Job>& job, const QString& issueId) {
    get(zephyr(job->settings, QStringLiteral("/stepResult?executionId=%1").arg(job->executionId)), [this, job, issueId](const Response& r) {
        const PublishCase& c = job->current();
        job->pendingSteps.clear();
        job->uploads.clear();
        QStringList stepResultIds;
        if (r.ok) {
            // Zephyr devuelve un resultado por paso del Test, en su orden, que es el mismo en el que
            // QAflow los ejecutó. Si el Test tiene menos pasos que la ejecución, sobran los últimos.
            const QJsonArray results = r.json.array();
            for (int i = 0; i < results.size(); ++i) {
                const QString id = QString::number(results[i].toObject()[QStringLiteral("id")].toInt());
                stepResultIds << id;
                if (i < c.steps.size()) job->pendingSteps.append({id, c.steps[i]});
            }
            // El Test enlazado y el caso se editan por separado y se desincronizan; y en el que se
            // acaba de crear puede haberse quedado fuera algún paso. Los veredictos que sobran no
            // tienen dónde ir, y callárselo deja un ciclo a medias sin decirlo.
            if (c.steps.size() > results.size())
                job->skip(QCoreApplication::translate("infrastructure", "%1: el Test %2 tiene %3 pasos y se ejecutaron %4; los %5 últimos veredictos se quedan fuera")
                              .arg(c.caseId, c.testKey).arg(results.size()).arg(c.steps.size()).arg(c.steps.size() - results.size()));
        } else {
            job->skip(QCoreApplication::translate("infrastructure", "%1: no se pudieron leer los pasos de la ejecución").arg(c.caseId));
        }
        // Cada evidencia va al resultado de su paso; las que no tienen paso, a la ejecución entera.
        for (const auto& a : c.attachments) {
            if (a.step > 0 && a.step <= stepResultIds.size())
                job->uploads.append(Upload{a.path, stepResultIds[a.step - 1], "TESTSTEPRESULT"});
            else
                job->uploads.append(Upload{a.path, job->executionId, "EXECUTION"});
        }
        if (!job->updating()) { writeNextStep(job, issueId); return; }
        // Al actualizar, lo ya subido se queda: sólo van las evidencias nuevas de cada destino.
        QList<QPair<QString, QByteArray>> entities;
        for (const auto& u : job->uploads) {
            const auto e = qMakePair(u.entityId, u.entityType);
            if (!entities.contains(e)) entities << e;
        }
        dropUploadedEvidence(job, entities, [this, job, issueId]() { writeNextStep(job, issueId); });
    });
}

void ZephyrClient::dropUploadedEvidence(const std::shared_ptr<Job>& job, QList<QPair<QString, QByteArray>> entities, std::function<void()> done) {
    if (entities.isEmpty()) { done(); return; }
    const auto entity = entities.takeFirst();
    const QString path = QStringLiteral("/attachment/attachmentsByEntity?entityId=%1&entityType=%2").arg(entity.first, QString::fromLatin1(entity.second));
    get(zephyr(job->settings, path), [this, job, entity, entities, done](const Response& r) {
        if (r.ok) {
            QStringList names;
            const QJsonArray data = r.json.isArray() ? r.json.array() : r.json.object()[QStringLiteral("data")].toArray();
            for (const auto& v : data) names << v.toObject()[QStringLiteral("fileName")].toString();
            job->uploads.erase(std::remove_if(job->uploads.begin(), job->uploads.end(), [&](const Upload& u) {
                                   return u.entityId == entity.first && u.entityType == entity.second && names.contains(QFileInfo(u.path).fileName());
                               }), job->uploads.end());
        }
        // Si no se puede leer lo que hay, se sube igual: una evidencia repetida molesta menos que una que falta.
        dropUploadedEvidence(job, entities, done);
    });
}

void ZephyrClient::writeNextStep(const std::shared_ptr<Job>& job, const QString& issueId) {
    if (job->pendingSteps.isEmpty()) { uploadNext(job); return; }
    const auto pending = job->pendingSteps.takeFirst();
    const QJsonObject body{
        {"id", pending.first.toInt()},
        {"issueId", issueId},
        {"executionId", job->executionId.toInt()},
        {"status", QString::number(zephyrStatus(pending.second.result))},
    };
    putWithComment(zephyr(job->settings, QStringLiteral("/stepResult/%1").arg(pending.first)), body, pending.second.note,
                   [this, job, issueId](const Response& r) {
                       if (r.ok) ++job->result.steps;
                       else job->skip(QCoreApplication::translate("infrastructure", "%1: no se pudo fijar el veredicto de un paso · %2").arg(job->current().caseId, r.error));
                       writeNextStep(job, issueId);
                   });
}

void ZephyrClient::uploadNext(const std::shared_ptr<Job>& job) {
    if (job->uploads.isEmpty()) {
        ++job->index;
        nextCase(job);
        return;
    }
    const Upload up = job->uploads.takeFirst();
    QHttpMultiPart* multi = multipartFile(up.path);
    if (!multi) {
        // Borrada o sin permisos desde que se ejecutó el caso: el ciclo sigue, pero se dice.
        job->skip(QCoreApplication::translate("infrastructure", "%1: no se pudo leer %2").arg(job->current().caseId, QFileInfo(up.path).fileName()));
        uploadNext(job);
        return;
    }
    QNetworkRequest req = zephyr(job->settings, QStringLiteral("/attachment?entityId=%1&entityType=%2")
                                                    .arg(up.entityId, QString::fromLatin1(up.entityType)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QVariant());   // lo fija el multipart
    postMultipart(req, multi, [this, job, up](const Response& r) {
        if (r.ok) ++job->result.attachments;
        else job->skip(QCoreApplication::translate("infrastructure", "%1: no se pudo subir %2").arg(job->current().caseId, QFileInfo(up.path).fileName()));
        uploadNext(job);
    });
}

} // namespace qaflow
