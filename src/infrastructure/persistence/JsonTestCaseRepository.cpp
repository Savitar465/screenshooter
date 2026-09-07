#include "JsonTestCaseRepository.h"

#include "core/models/CaseFormats.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace qaflow {

namespace {

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
      m_plansPath(QDir(dataDir).filePath(QStringLiteral("plans.json"))),
      m_legacyPlanPath(QDir(dataDir).filePath(QStringLiteral("plan.json"))) {}

std::optional<QList<TestCase>> JsonTestCaseRepository::loadCases() {
    const auto doc = readJson(m_casesPath);
    if (!doc || !doc->isArray()) return std::nullopt;
    QList<TestCase> out;
    for (const auto& v : doc->array()) out.append(formats::caseFromJson(v.toObject()));
    return out;
}

bool JsonTestCaseRepository::saveCases(const QList<TestCase>& cases) {
    return writeJson(m_casesPath, QJsonDocument(formats::casesToJson(cases)));
}

namespace {
QJsonObject planToJson(const TestPlan& p) {
    return QJsonObject{{"id", p.id}, {"name", p.name}, {"caseIds", QJsonArray::fromStringList(p.caseIds)},
                       {"archived", p.archived}, {"createdAt", p.createdAt.isValid() ? p.createdAt.toString(Qt::ISODate) : QString()}};
}
TestPlan planFromJson(const QJsonObject& o) {
    TestPlan p;
    p.id = o["id"].toString();
    p.name = o["name"].toString();
    for (const auto& v : o["caseIds"].toArray()) p.caseIds << v.toString();
    p.archived = o["archived"].toBool();
    p.createdAt = QDateTime::fromString(o["createdAt"].toString(), Qt::ISODate);
    return p;
}
} // namespace

std::optional<PlanCollection> JsonTestCaseRepository::loadPlans() {
    if (const auto doc = readJson(m_plansPath); doc && doc->isObject()) {
        PlanCollection col;
        const auto o = doc->object();
        col.activeId = o["activeId"].toString();
        for (const auto& v : o["plans"].toArray()) col.plans.append(planFromJson(v.toObject()));
        return col;
    }
    // Migración: versiones anteriores guardaban un único plan en plan.json.
    if (const auto legacy = readJson(m_legacyPlanPath); legacy && legacy->isObject()) {
        TestPlan p = planFromJson(legacy->object());
        p.id = QStringLiteral("PL-0001");
        if (p.name.isEmpty()) p.name = TestPlan{}.name;
        return PlanCollection{p.id, {p}};
    }
    return std::nullopt;
}

bool JsonTestCaseRepository::savePlans(const PlanCollection& col) {
    QJsonArray plans;
    for (const auto& p : col.plans) plans.append(planToJson(p));
    return writeJson(m_plansPath, QJsonDocument(QJsonObject{{"activeId", col.activeId}, {"plans", plans}}));
}

} // namespace qaflow
