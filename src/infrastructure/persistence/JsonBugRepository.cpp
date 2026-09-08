#include "JsonBugRepository.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace qaflow {

namespace {

QString iso(const QDateTime& dt) { return dt.isValid() ? dt.toString(Qt::ISODate) : QString(); }
QStringList strings(const QJsonValue& v) { QStringList out; for (const auto& x : v.toArray()) out << x.toString(); return out; }

QJsonObject reportToJson(const BugReport& b) {
    return QJsonObject{
        {"title", b.title}, {"severity", b.severity}, {"environment", b.environment},
        {"linkedCaseId", b.linkedCaseId}, {"linkedStoryKey", b.linkedStoryKey},
        {"stepsToReproduce", b.stepsToReproduce}, {"expected", b.expected}, {"actual", b.actual},
        {"attachmentPaths", QJsonArray::fromStringList(b.attachmentPaths)},
        {"issueType", b.issueType}, {"priority", b.priority}, {"assigneeId", b.assigneeId}, {"assigneeName", b.assigneeName},
        {"components", QJsonArray::fromStringList(b.components)}, {"affectsVersions", QJsonArray::fromStringList(b.affectsVersions)},
        {"labels", QJsonArray::fromStringList(b.labels)},
    };
}

BugReport reportFromJson(const QJsonObject& o) {
    BugReport b;
    b.title = o["title"].toString();
    b.severity = o["severity"].toString(b.severity);
    b.environment = o["environment"].toString(b.environment);
    b.linkedCaseId = o["linkedCaseId"].toString();
    b.linkedStoryKey = o["linkedStoryKey"].toString();
    b.stepsToReproduce = o["stepsToReproduce"].toString();
    b.expected = o["expected"].toString();
    b.actual = o["actual"].toString();
    b.attachmentPaths = strings(o["attachmentPaths"]);
    b.issueType = o["issueType"].toString(b.issueType);
    b.priority = o["priority"].toString();
    b.assigneeId = o["assigneeId"].toString();
    b.assigneeName = o["assigneeName"].toString();
    b.components = strings(o["components"]);
    b.affectsVersions = strings(o["affectsVersions"]);
    b.labels = strings(o["labels"]);
    return b;
}

QJsonObject issueToJson(const IssueLink& i) {
    return QJsonObject{
        {"key", i.key}, {"url", i.url}, {"title", i.title}, {"caseId", i.caseId}, {"tracker", i.tracker}, {"severity", i.severity},
        {"status", i.status}, {"resolved", i.resolved}, {"createdAt", iso(i.createdAt)}, {"statusCheckedAt", iso(i.statusCheckedAt)},
    };
}

IssueLink issueFromJson(const QJsonObject& o) {
    IssueLink i;
    i.key = o["key"].toString();
    i.url = o["url"].toString();
    i.title = o["title"].toString();
    i.caseId = o["caseId"].toString();
    i.tracker = o["tracker"].toString();
    i.severity = o["severity"].toString();
    i.status = o["status"].toString();
    i.resolved = o["resolved"].toBool();
    i.createdAt = QDateTime::fromString(o["createdAt"].toString(), Qt::ISODate);
    i.statusCheckedAt = QDateTime::fromString(o["statusCheckedAt"].toString(), Qt::ISODate);
    return i;
}

QJsonObject pendingToJson(const PendingBug& p) {
    return QJsonObject{{"id", p.id}, {"report", reportToJson(p.report)}, {"createdAt", iso(p.createdAt)}, {"lastError", p.lastError}, {"attempts", p.attempts}};
}

PendingBug pendingFromJson(const QJsonObject& o) {
    PendingBug p;
    p.id = o["id"].toString();
    p.report = reportFromJson(o["report"].toObject());
    p.createdAt = QDateTime::fromString(o["createdAt"].toString(), Qt::ISODate);
    p.lastError = o["lastError"].toString();
    p.attempts = o["attempts"].toInt();
    return p;
}

} // namespace

JsonBugRepository::JsonBugRepository(const QString& dataDir) : m_path(QDir(dataDir).filePath(QStringLiteral("bugs.json"))) {}

std::optional<BugLedger> JsonBugRepository::loadLedger() {
    QFile f(m_path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly)) return std::nullopt;
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return std::nullopt;
    BugLedger l;
    const auto o = doc.object();
    for (const auto& v : o["issues"].toArray()) l.issues.append(issueFromJson(v.toObject()));
    for (const auto& v : o["pending"].toArray()) l.pending.append(pendingFromJson(v.toObject()));
    return l;
}

bool JsonBugRepository::saveLedger(const BugLedger& ledger) {
    QJsonArray issues, pending;
    for (const auto& i : ledger.issues) issues.append(issueToJson(i));
    for (const auto& p : ledger.pending) pending.append(pendingToJson(p));
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(QJsonObject{{"issues", issues}, {"pending", pending}}).toJson(QJsonDocument::Indented));
    return f.commit();
}

} // namespace qaflow
