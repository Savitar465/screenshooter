#include "JsonRunHistoryRepository.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace qaflow {

namespace {

QString isoOrEmpty(const QDateTime& dt) { return dt.isValid() ? dt.toString(Qt::ISODate) : QString(); }

QJsonObject toJson(const RunRecord& r) {
    QJsonArray steps;
    for (const auto& s : r.steps)
        steps.append(QJsonObject{{"action", s.action}, {"expected", s.expected}, {"result", toString(s.result)}, {"note", s.note}, {"durationSecs", s.durationSecs}});
    return QJsonObject{
        {"id", r.id}, {"caseId", r.caseId}, {"caseTitle", r.caseTitle}, {"suite", r.suite},
        {"planRunId", r.planRunId}, {"startedAt", isoOrEmpty(r.startedAt)}, {"finishedAt", isoOrEmpty(r.finishedAt)},
        {"verdict", toString(r.verdict)}, {"plannedSteps", r.plannedSteps}, {"durationSecs", r.durationSecs}, {"steps", steps},
    };
}

RunRecord runFromJson(const QJsonObject& o) {
    RunRecord r;
    r.id = o["id"].toString();
    r.caseId = o["caseId"].toString();
    r.caseTitle = o["caseTitle"].toString();
    r.suite = o["suite"].toString();
    r.planRunId = o["planRunId"].toString();
    r.startedAt = QDateTime::fromString(o["startedAt"].toString(), Qt::ISODate);
    r.finishedAt = QDateTime::fromString(o["finishedAt"].toString(), Qt::ISODate);
    r.verdict = verdictFromString(o["verdict"].toString());
    r.plannedSteps = o["plannedSteps"].toInt();
    for (const auto& v : o["steps"].toArray()) {
        const auto s = v.toObject();
        r.steps.append(RunRecordStep{s["action"].toString(), s["expected"].toString(), stepResultFromString(s["result"].toString()), s["note"].toString(), s["durationSecs"].toInt()});
    }
    // Registros anteriores a la medición por pasos: usar inicio → fin.
    r.durationSecs = o.contains("durationSecs") ? static_cast<qint64>(o["durationSecs"].toDouble())
                     : (r.startedAt.isValid() && r.finishedAt.isValid() ? r.startedAt.secsTo(r.finishedAt) : 0);
    return r;
}

QJsonObject toJson(const PlanRun& p) {
    return QJsonObject{
        {"id", p.id}, {"planId", p.planId}, {"name", p.name}, {"caseIds", QJsonArray::fromStringList(p.caseIds)},
        {"startedAt", isoOrEmpty(p.startedAt)}, {"finishedAt", isoOrEmpty(p.finishedAt)},
        {"zephyrCycleId", p.zephyrCycleId}, {"publishedAt", isoOrEmpty(p.publishedAt)},
    };
}

PlanRun planFromJson(const QJsonObject& o) {
    PlanRun p;
    p.id = o["id"].toString();
    p.planId = o["planId"].toString();
    p.name = o["name"].toString();
    for (const auto& v : o["caseIds"].toArray()) p.caseIds << v.toString();
    p.startedAt = QDateTime::fromString(o["startedAt"].toString(), Qt::ISODate);
    p.finishedAt = QDateTime::fromString(o["finishedAt"].toString(), Qt::ISODate);
    p.zephyrCycleId = o["zephyrCycleId"].toString();
    p.publishedAt = QDateTime::fromString(o["publishedAt"].toString(), Qt::ISODate);
    return p;
}

} // namespace

JsonRunHistoryRepository::JsonRunHistoryRepository(const QString& dataDir)
    : m_path(QDir(dataDir).filePath(QStringLiteral("history.json"))) {}

std::optional<RunHistory> JsonRunHistoryRepository::loadHistory() {
    QFile f(m_path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return std::nullopt;
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return std::nullopt;
    RunHistory h;
    const auto o = doc.object();
    for (const auto& v : o["runs"].toArray()) h.runs.append(runFromJson(v.toObject()));
    for (const auto& v : o["plans"].toArray()) h.plans.append(planFromJson(v.toObject()));
    return h;
}

bool JsonRunHistoryRepository::saveHistory(const RunHistory& history) {
    QJsonArray runs, plans;
    for (const auto& r : history.runs) runs.append(toJson(r));
    for (const auto& p : history.plans) plans.append(toJson(p));
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(QJsonObject{{"runs", runs}, {"plans", plans}}).toJson(QJsonDocument::Indented));
    return f.commit();
}

} // namespace qaflow
