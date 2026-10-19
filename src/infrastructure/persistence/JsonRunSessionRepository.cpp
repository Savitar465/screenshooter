#include "JsonRunSessionRepository.h"

#include "core/models/RunHistory.h"   // toString(StepResult) / stepResultFromString

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace qaflow {

namespace {

RunState runFromJson(const QJsonObject& run) {
    RunState r;
    r.caseId = run["caseId"].toString();
    r.runId = run["runId"].toString();
    r.idx = run["idx"].toInt();
    r.note = run["note"].toString();
    r.startedAt = QDateTime::fromString(run["startedAt"].toString(), Qt::ISODate);
    r.finished = run["finished"].toBool();
    r.paused = run["paused"].toBool();
    r.reviewOf = run["reviewOf"].toString();
    r.stepElapsedSecs = run["stepElapsedSecs"].toInt();
    for (const auto& v : run["results"].toArray()) {
        const auto x = v.toObject();
        // Las sesiones anteriores sólo guardaban los pasos ya ejecutados: sin "marked", todos lo están.
        r.results.append(StepRecord{stepResultFromString(x["result"].toString()), x["note"].toString(),
                                    x["durationSecs"].toInt(), x["marked"].toBool(true), x["inherited"].toBool()});
    }
    return r;
}

QJsonObject runToJson(const RunState& run) {
    QJsonArray results;
    for (const auto& r : run.results)
        results.append(QJsonObject{{"result", toString(r.result)}, {"note", r.note}, {"durationSecs", r.durationSecs},
                                   {"marked", r.marked}, {"inherited", r.inherited}});
    return QJsonObject{
        {"caseId", run.caseId}, {"runId", run.runId}, {"idx", run.idx}, {"note", run.note},
        {"startedAt", run.startedAt.isValid() ? run.startedAt.toString(Qt::ISODate) : QString()},
        {"finished", run.finished}, {"paused", run.paused}, {"reviewOf", run.reviewOf}, {"stepElapsedSecs", run.stepElapsedSecs},
        {"results", results},
    };
}

QJsonObject idsToJson(const QHash<QString, QString>& ids) {
    QJsonObject o;
    for (auto it = ids.cbegin(); it != ids.cend(); ++it) o.insert(it.key(), it.value());
    return o;
}

QHash<QString, QString> idsFromJson(const QJsonObject& o) {
    QHash<QString, QString> ids;
    for (auto it = o.constBegin(); it != o.constEnd(); ++it) ids.insert(it.key(), it.value().toString());
    return ids;
}

} // namespace

JsonRunSessionRepository::JsonRunSessionRepository(const QString& dataDir)
    : m_path(QDir(dataDir).filePath(QStringLiteral("session.json"))), m_dir(dataDir) {}

std::optional<RunSession> JsonRunSessionRepository::loadSession() { return read(m_path); }

bool JsonRunSessionRepository::saveSession(const RunSession& s) { return write(m_path, s); }

void JsonRunSessionRepository::clearSession() { QFile::remove(m_path); }

QString JsonRunSessionRepository::suspendedPath(const QString& planRunId) const {
    // El id es nuestro (PR-0001), pero va a una ruta: nada de separadores.
    QString name = planRunId;
    name.replace(QLatin1Char('/'), QLatin1Char('_')).replace(QLatin1Char('\\'), QLatin1Char('_'));
    return QDir(m_dir).filePath(QStringLiteral("sessions/%1.json").arg(name));
}

std::optional<RunSession> JsonRunSessionRepository::loadSuspended(const QString& planRunId) {
    return planRunId.isEmpty() ? std::nullopt : read(suspendedPath(planRunId));
}

bool JsonRunSessionRepository::saveSuspended(const QString& planRunId, const RunSession& s) {
    return !planRunId.isEmpty() && write(suspendedPath(planRunId), s);
}

void JsonRunSessionRepository::clearSuspended(const QString& planRunId) {
    if (!planRunId.isEmpty()) QFile::remove(suspendedPath(planRunId));
}

std::optional<RunSession> JsonRunSessionRepository::read(const QString& path) {
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return std::nullopt;
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return std::nullopt;
    const auto o = doc.object();

    RunSession s;
    s.run = runFromJson(o["run"].toObject());
    for (const auto& v : o["queue"].toArray()) s.queue << v.toString();
    s.planRunId = o["planRunId"].toString();
    s.continuesRunId = o["continuesRunId"].toString();
    for (const auto& v : o["parked"].toArray()) {
        const auto p = v.toObject();
        s.parked.append(ParkedRun{runFromJson(p["run"].toObject()), p["continuesRunId"].toString()});
    }
    s.resume = idsFromJson(o["resume"].toObject());
    s.review = idsFromJson(o["review"].toObject());
    for (const auto& v : o["order"].toArray()) s.order << v.toString();
    return s;
}

bool JsonRunSessionRepository::write(const QString& path, const RunSession& s) {
    QJsonArray parked;
    for (const auto& p : s.parked) parked.append(QJsonObject{{"run", runToJson(p.run)}, {"continuesRunId", p.continuesRunId}});
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(QJsonObject{{"run", runToJson(s.run)}, {"queue", QJsonArray::fromStringList(s.queue)}, {"planRunId", s.planRunId},
                                      {"continuesRunId", s.continuesRunId}, {"parked", parked},
                                      {"resume", idsToJson(s.resume)}, {"review", idsToJson(s.review)},
                                      {"order", QJsonArray::fromStringList(s.order)}})
                .toJson(QJsonDocument::Indented));
    return f.commit();
}

} // namespace qaflow
