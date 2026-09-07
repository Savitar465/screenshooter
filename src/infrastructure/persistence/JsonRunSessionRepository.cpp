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
    const auto run = o["run"].toObject();
    s.run.caseId = run["caseId"].toString();
    s.run.idx = run["idx"].toInt();
    s.run.note = run["note"].toString();
    s.run.startedAt = QDateTime::fromString(run["startedAt"].toString(), Qt::ISODate);
    s.run.finished = run["finished"].toBool();
    s.run.stepElapsedSecs = run["stepElapsedSecs"].toInt();
    for (const auto& v : run["results"].toArray()) {
        const auto r = v.toObject();
        s.run.results.append(StepRecord{stepResultFromString(r["result"].toString()), r["note"].toString(), r["durationSecs"].toInt()});
    }
    for (const auto& v : o["queue"].toArray()) s.queue << v.toString();
    s.planRunId = o["planRunId"].toString();
    return s;
}

bool JsonRunSessionRepository::saveSession(const RunSession& s) {
    QJsonArray results;
    for (const auto& r : s.run.results)
        results.append(QJsonObject{{"result", toString(r.result)}, {"note", r.note}, {"durationSecs", r.durationSecs}});
    const QJsonObject run{
        {"caseId", s.run.caseId}, {"idx", s.run.idx}, {"note", s.run.note},
        {"startedAt", s.run.startedAt.isValid() ? s.run.startedAt.toString(Qt::ISODate) : QString()},
        {"finished", s.run.finished}, {"stepElapsedSecs", s.run.stepElapsedSecs}, {"results", results},
    };
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(QJsonObject{{"run", run}, {"queue", QJsonArray::fromStringList(s.queue)}, {"planRunId", s.planRunId}}).toJson(QJsonDocument::Indented));
    return f.commit();
}

void JsonRunSessionRepository::clearSession() { QFile::remove(m_path); }

} // namespace qaflow
