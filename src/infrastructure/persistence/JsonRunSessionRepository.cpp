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
        {"finished", run.finished}, {"paused", run.paused}, {"stepElapsedSecs", run.stepElapsedSecs}, {"results", results},
    };
}

} // namespace

JsonRunSessionRepository::JsonRunSessionRepository(const QString& dataDir)
    : m_path(QDir(dataDir).filePath(QStringLiteral("session.json"))) {}

std::optional<RunSession> JsonRunSessionRepository::loadSession() {
    QFile f(m_path);
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
    return s;
}

bool JsonRunSessionRepository::saveSession(const RunSession& s) {
    QJsonArray parked;
    for (const auto& p : s.parked) parked.append(QJsonObject{{"run", runToJson(p.run)}, {"continuesRunId", p.continuesRunId}});
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(QJsonObject{{"run", runToJson(s.run)}, {"queue", QJsonArray::fromStringList(s.queue)}, {"planRunId", s.planRunId},
                                      {"continuesRunId", s.continuesRunId}, {"parked", parked}})
                .toJson(QJsonDocument::Indented));
    return f.commit();
}

void JsonRunSessionRepository::clearSession() { QFile::remove(m_path); }

} // namespace qaflow
