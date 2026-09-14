#include "JsonIssueRepository.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace qaflow {

namespace {

constexpr int kVersion = 1;

QString iso(const QDateTime& dt) { return dt.isValid() ? dt.toString(Qt::ISODate) : QString(); }
QString isoDate(const QDate& d) { return d.isValid() ? d.toString(Qt::ISODate) : QString(); }
QDateTime dateTime(const QJsonValue& v) { return QDateTime::fromString(v.toString(), Qt::ISODate); }
QDate date(const QJsonValue& v) { return QDate::fromString(v.toString(), Qt::ISODate); }
QStringList strings(const QJsonValue& v) {
    QStringList out;
    for (const auto& x : v.toArray()) out << x.toString();
    return out;
}

QJsonArray fieldsToJson(const QList<RequirementField>& fields) {
    QJsonArray out;
    for (const auto& f : fields) out.append(QJsonObject{{"label", f.label}, {"value", f.value}});
    return out;
}

QList<RequirementField> fieldsFromJson(const QJsonValue& v) {
    QList<RequirementField> out;
    for (const auto& x : v.toArray()) {
        const QJsonObject o = x.toObject();
        out << RequirementField{o["label"].toString(), o["value"].toString()};
    }
    return out;
}

QJsonObject rowToJson(const ExternalRequirement& r) {
    return QJsonObject{
        {"id", r.id}, {"system", r.system}, {"systemCode", r.systemCode}, {"systemName", r.systemName}, {"summary", r.summary},
        {"requestedOn", isoDate(r.requestedOn)}, {"assignedFrom", isoDate(r.assignedFrom)}, {"assignedUntil", isoDate(r.assignedUntil)},
        {"requestingUnit", r.requestingUnit}, {"requester", r.requester}, {"user", r.user}, {"priority", r.priority},
        {"states", QJsonArray::fromStringList(r.states)}, {"detailUrl", r.detailUrl},
    };
}

ExternalRequirement rowFromJson(const QJsonObject& o) {
    ExternalRequirement r;
    r.id = o["id"].toString();
    r.system = o["system"].toString();
    r.systemCode = o["systemCode"].toString();
    r.systemName = o["systemName"].toString();
    r.summary = o["summary"].toString();
    r.requestedOn = date(o["requestedOn"]);
    r.assignedFrom = date(o["assignedFrom"]);
    r.assignedUntil = date(o["assignedUntil"]);
    r.requestingUnit = o["requestingUnit"].toString();
    r.requester = o["requester"].toString();
    r.user = o["user"].toString();
    r.priority = o["priority"].toString();
    r.states = strings(o["states"]);
    r.detailUrl = o["detailUrl"].toString();
    return r;
}

QJsonObject detailToJson(const RequirementDetail& d) {
    QJsonArray sections, attachments;
    for (const auto& s : d.sections) sections.append(QJsonObject{{"title", s.title}, {"fields", fieldsToJson(s.fields)}});
    for (const auto& a : d.attachments) attachments.append(QJsonObject{{"label", a.label}, {"fileName", a.fileName}, {"url", a.url}});
    return QJsonObject{
        {"id", d.id}, {"requestType", d.requestType}, {"reference", d.reference}, {"requestingUnit", d.requestingUnit},
        {"requester", d.requester}, {"priority", d.priority}, {"state", d.state}, {"systemCode", d.systemCode},
        {"description", d.description}, {"url", d.url}, {"fields", fieldsToJson(d.fields)}, {"sections", sections}, {"attachments", attachments},
    };
}

RequirementDetail detailFromJson(const QJsonObject& o) {
    RequirementDetail d;
    d.id = o["id"].toString();
    d.requestType = o["requestType"].toString();
    d.reference = o["reference"].toString();
    d.requestingUnit = o["requestingUnit"].toString();
    d.requester = o["requester"].toString();
    d.priority = o["priority"].toString();
    d.state = o["state"].toString();
    d.systemCode = o["systemCode"].toString();
    d.description = o["description"].toString();
    d.url = o["url"].toString();
    d.fields = fieldsFromJson(o["fields"]);
    for (const auto& v : o["sections"].toArray()) {
        const QJsonObject s = v.toObject();
        d.sections << RequirementSection{s["title"].toString(), fieldsFromJson(s["fields"])};
    }
    for (const auto& v : o["attachments"].toArray()) {
        const QJsonObject a = v.toObject();
        d.attachments << RequirementAttachment{a["label"].toString(), a["fileName"].toString(), a["url"].toString()};
    }
    return d;
}

QJsonObject requirementToJson(const RequirementLink& r) {
    QJsonArray changes;
    for (const auto& c : r.changes) changes.append(QJsonObject{{"field", c.field}, {"before", c.before}, {"after", c.after}});
    QJsonObject o{
        {"connection", r.connection}, {"data", rowToJson(r.data)}, {"importedAt", iso(r.importedAt)},
        {"fetchedAt", iso(r.fetchedAt)}, {"missing", r.missing}, {"changes", changes},
    };
    // La ficha sólo está si se llegó a consultar.
    if (!r.detail.id.isEmpty()) {
        o.insert(QStringLiteral("detail"), detailToJson(r.detail));
        o.insert(QStringLiteral("detailFetchedAt"), iso(r.detailFetchedAt));
    }
    return o;
}

RequirementLink requirementFromJson(const QJsonObject& o) {
    RequirementLink r;
    r.connection = o["connection"].toString();
    r.data = rowFromJson(o["data"].toObject());
    r.importedAt = dateTime(o["importedAt"]);
    r.fetchedAt = dateTime(o["fetchedAt"]);
    r.missing = o["missing"].toBool();
    for (const auto& v : o["changes"].toArray()) {
        const QJsonObject c = v.toObject();
        r.changes << RequirementChange{c["field"].toString(), c["before"].toString(), c["after"].toString()};
    }
    if (o.contains(QStringLiteral("detail"))) {
        r.detail = detailFromJson(o["detail"].toObject());
        r.detailFetchedAt = dateTime(o["detailFetchedAt"]);
    }
    return r;
}

QJsonObject publicationToJson(const IssuePublication& p) {
    return QJsonObject{
        {"tracker", p.tracker}, {"baseUrl", p.baseUrl}, {"project", p.project}, {"key", p.key}, {"url", p.url},
        {"issueType", p.issueType}, {"publishedAt", iso(p.publishedAt)}, {"publishedTitle", p.publishedTitle},
        {"publishedDescription", p.publishedDescription}, {"status", p.status}, {"resolved", p.resolved},
        {"statusCheckedAt", iso(p.statusCheckedAt)}, {"linked", p.linked}, {"uncertain", p.uncertain}, {"lastError", p.lastError},
    };
}

IssuePublication publicationFromJson(const QJsonObject& o) {
    IssuePublication p;
    p.tracker = o["tracker"].toString();
    p.baseUrl = o["baseUrl"].toString();
    p.project = o["project"].toString();
    p.key = o["key"].toString();
    p.url = o["url"].toString();
    p.issueType = o["issueType"].toString();
    p.publishedAt = dateTime(o["publishedAt"]);
    p.publishedTitle = o["publishedTitle"].toString();
    p.publishedDescription = o["publishedDescription"].toString();
    p.status = o["status"].toString();
    p.resolved = o["resolved"].toBool();
    p.statusCheckedAt = dateTime(o["statusCheckedAt"]);
    p.linked = o["linked"].toBool();
    p.uncertain = o["uncertain"].toBool();
    p.lastError = o["lastError"].toString();
    return p;
}

QJsonObject issueToJson(const Issue& i) {
    QJsonObject o{
        {"id", i.id}, {"title", i.title}, {"notes", i.notes}, {"priority", toString(i.priority)}, {"state", toString(i.state)},
        {"caseIds", QJsonArray::fromStringList(i.caseIds)}, {"planIds", QJsonArray::fromStringList(i.planIds)},
        {"createdAt", iso(i.createdAt)}, {"updatedAt", iso(i.updatedAt)},
    };
    if (i.isImported()) o.insert(QStringLiteral("requirement"), requirementToJson(i.requirement));
    if (i.isPublished()) o.insert(QStringLiteral("publication"), publicationToJson(i.publication));
    return o;
}

Issue issueFromJson(const QJsonObject& o) {
    Issue i;
    i.id = o["id"].toString();
    i.title = o["title"].toString();
    i.notes = o["notes"].toString();
    i.priority = priorityFromString(o["priority"].toString());
    i.state = issueStateFromString(o["state"].toString());
    i.caseIds = strings(o["caseIds"]);
    i.planIds = strings(o["planIds"]);
    if (o.contains(QStringLiteral("publication"))) {
        i.publication = publicationFromJson(o["publication"].toObject());
    } else if (!o["jiraKey"].toString().isEmpty()) {
        // Ficheros de la primera versión, que sólo guardaban la clave y la URL.
        i.publication.key = o["jiraKey"].toString();
        i.publication.url = o["jiraUrl"].toString();
    }
    i.createdAt = dateTime(o["createdAt"]);
    i.updatedAt = dateTime(o["updatedAt"]);
    if (o.contains(QStringLiteral("requirement"))) i.requirement = requirementFromJson(o["requirement"].toObject());
    return i;
}

} // namespace

JsonIssueRepository::JsonIssueRepository(const QString& dataDir) : m_path(QDir(dataDir).filePath(QStringLiteral("issues.json"))) {}

std::optional<QList<Issue>> JsonIssueRepository::loadIssues() {
    QFile f(m_path);
    if (!f.exists()) return QList<Issue>{};
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return std::nullopt;
    const QJsonObject root = doc.object();
    // Un fichero de otra versión se deja como está en vez de leerlo a medias y reescribirlo sin lo que no se entendió.
    if (root["version"].toInt() != kVersion || !root["issues"].isArray()) return std::nullopt;
    QList<Issue> issues;
    for (const auto& v : root["issues"].toArray()) issues << issueFromJson(v.toObject());
    return issues;
}

bool JsonIssueRepository::saveIssues(const QList<Issue>& issues) {
    QJsonArray list;
    for (const auto& i : issues) list.append(issueToJson(i));
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(QJsonObject{{"version", kVersion}, {"issues", list}}).toJson(QJsonDocument::Indented));
    return f.commit();
}

} // namespace qaflow
