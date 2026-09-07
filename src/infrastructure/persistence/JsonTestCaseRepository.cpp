#include "JsonTestCaseRepository.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace qaflow {

namespace {

QJsonObject toJson(const TestCase& c) {
    QJsonArray steps;
    for (const auto& s : c.steps) steps.append(QJsonObject{{"action", s.action}, {"expected", s.expected}});
    QJsonArray shots;
    for (const auto& s : c.shots) shots.append(QJsonObject{{"id", s.id}, {"step", s.step}, {"fileName", s.fileName}, {"path", s.path}});
    QJsonObject o{
        {"id", c.id}, {"title", c.title}, {"suite", c.suite},
        {"priority", toString(c.priority)}, {"status", toString(c.status)},
        {"preconditions", c.preconditions}, {"steps", steps}, {"shots", shots},
    };
    if (c.lastRun.outcome != RunOutcome::None) {
        o["lastRunOutcome"] = c.lastRun.outcome == RunOutcome::Passed ? "passed" : c.lastRun.outcome == RunOutcome::Blocked ? "blocked" : "failed";
        o["lastRunAt"] = c.lastRun.at.toString(Qt::ISODate);
    }
    return o;
}

TestCase fromJson(const QJsonObject& o) {
    TestCase c;
    c.id = o["id"].toString();
    c.title = o["title"].toString();
    c.suite = o["suite"].toString();
    c.priority = priorityFromString(o["priority"].toString());
    c.status = statusFromString(o["status"].toString());
    c.preconditions = o["preconditions"].toString();
    for (const auto& v : o["steps"].toArray()) {
        const auto s = v.toObject();
        c.steps.append(TestStep{s["action"].toString(), s["expected"].toString()});
    }
    for (const auto& v : o["shots"].toArray()) {
        const auto s = v.toObject();
        c.shots.append(Screenshot{s["id"].toInt(), s["step"].toInt(), s["fileName"].toString(), s["path"].toString()});
    }
    const QString outcome = o["lastRunOutcome"].toString();
    if (!outcome.isEmpty()) {
        c.lastRun.outcome = outcome == "passed" ? RunOutcome::Passed : outcome == "blocked" ? RunOutcome::Blocked : RunOutcome::Failed;
        c.lastRun.at = QDateTime::fromString(o["lastRunAt"].toString(), Qt::ISODate);
    }
    return c;
}

bool writeJson(const QString& path, const QJsonDocument& doc) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(doc.toJson(QJsonDocument::Indented));
    return f.commit();
}

std::optional<QJsonDocument> readJson(const QString& path) {
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return std::nullopt;
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) return std::nullopt;
    return doc;
}

} // namespace

JsonTestCaseRepository::JsonTestCaseRepository(const QString& dataDir)
    : m_casesPath(QDir(dataDir).filePath(QStringLiteral("cases.json"))),
      m_planPath(QDir(dataDir).filePath(QStringLiteral("plan.json"))) {}

std::optional<QList<TestCase>> JsonTestCaseRepository::loadCases() {
    const auto doc = readJson(m_casesPath);
    if (!doc || !doc->isArray()) return std::nullopt;
    QList<TestCase> out;
    for (const auto& v : doc->array()) out.append(fromJson(v.toObject()));
    return out;
}

bool JsonTestCaseRepository::saveCases(const QList<TestCase>& cases) {
    QJsonArray arr;
    for (const auto& c : cases) arr.append(toJson(c));
    return writeJson(m_casesPath, QJsonDocument(arr));
}

std::optional<TestPlan> JsonTestCaseRepository::loadPlan() {
    const auto doc = readJson(m_planPath);
    if (!doc || !doc->isObject()) return std::nullopt;
    TestPlan p;
    const auto o = doc->object();
    p.name = o["name"].toString();
    for (const auto& v : o["caseIds"].toArray()) p.caseIds << v.toString();
    return p;
}

bool JsonTestCaseRepository::savePlan(const TestPlan& plan) {
    return writeJson(m_planPath, QJsonDocument(QJsonObject{{"name", plan.name}, {"caseIds", QJsonArray::fromStringList(plan.caseIds)}}));
}

} // namespace qaflow
