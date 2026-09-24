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

QJsonObject recordToJson(const QualityRecord& r) {
    QJsonArray observations, characteristics;
    for (const auto& o : r.observations)
        observations.append(QJsonObject{{"type", o.type}, {"observations", o.observations}, {"corrections", o.corrections}});
    for (const auto& c : r.characteristics)
        characteristics.append(QJsonObject{{"text", c.text}, {"satisfied", c.satisfied}, {"note", c.note}});
    return QJsonObject{
        {"greq", r.greq}, {"process", r.process}, {"system", r.system}, {"moduleLink", r.moduleLink}, {"server", r.server},
        {"dbAccess", r.dbAccess}, {"dbSchema", r.dbSchema}, {"dbUser", r.dbUser}, {"appUser", r.appUser},
        {"tables", r.tables}, {"functions", r.functions}, {"description", r.description}, {"developedBy", r.developedBy},
        {"qaResource", r.qaResource}, {"department", r.department}, {"revisionNumber", r.revisionNumber},
        {"from", isoDate(r.from)}, {"to", isoDate(r.to)}, {"observations", observations},
        {"caseDesign", r.caseDesign}, {"execution", r.execution}, {"bugs", r.bugs},
        {"executionImages", QJsonArray::fromStringList(r.executionImages)},
        {"characteristics", characteristics}, {"generalNotes", r.generalNotes}, {"logoPath", r.logoPath},
    };
}

QualityRecord recordFromJson(const QJsonObject& o) {
    QualityRecord r;
    r.greq = o["greq"].toString();
    r.process = o["process"].toString(r.process);
    r.system = o["system"].toString();
    r.moduleLink = o["moduleLink"].toString();
    r.server = o["server"].toString(r.server);
    r.dbAccess = o["dbAccess"].toString(r.dbAccess);
    r.dbSchema = o["dbSchema"].toString(r.dbSchema);
    r.dbUser = o["dbUser"].toString(r.dbUser);
    r.appUser = o["appUser"].toString(r.appUser);
    r.tables = o["tables"].toString(r.tables);
    r.functions = o["functions"].toString(r.functions);
    r.description = o["description"].toString();
    r.developedBy = o["developedBy"].toString();
    r.qaResource = o["qaResource"].toString();
    r.department = o["department"].toString(r.department);
    r.revisionNumber = o["revisionNumber"].toInt(r.revisionNumber);
    r.from = date(o["from"]);
    r.to = date(o["to"]);
    if (o["observations"].isArray()) {
        QList<ObservationCount> observations;
        for (const auto& v : o["observations"].toArray()) {
            const QJsonObject c = v.toObject();
            observations << ObservationCount{c["type"].toString(), c["observations"].toInt(), c["corrections"].toInt()};
        }
        if (!observations.isEmpty()) r.observations = observations;
    }
    r.caseDesign = o["caseDesign"].toString();
    r.execution = o["execution"].toString();
    r.bugs = o["bugs"].toString();
    r.executionImages = strings(o["executionImages"]);
    if (o["characteristics"].isArray()) {
        QList<QualityCharacteristic> characteristics;
        for (const auto& v : o["characteristics"].toArray()) {
            const QJsonObject c = v.toObject();
            characteristics << QualityCharacteristic{c["text"].toString(), c["satisfied"].toBool(true), c["note"].toString()};
        }
        if (!characteristics.isEmpty()) r.characteristics = characteristics;
    }
    r.generalNotes = o["generalNotes"].toString();
    r.logoPath = o["logoPath"].toString();
    return r;
}

QJsonObject revisionToJson(const IssueRevision& r) {
    QJsonObject o{
        {"number", r.number}, {"startedAt", iso(r.startedAt)}, {"closedAt", iso(r.closedAt)},
        {"outcome", toString(r.outcome)}, {"planRunId", r.planRunId}, {"record", recordToJson(r.record)},
        {"documentPath", r.documentPath}, {"documentAt", iso(r.documentAt)},
    };
    if (!r.phase.isEmpty()) o.insert(QStringLiteral("phase"), r.phase);
    if (!r.jira.isEmpty())
        o.insert(QStringLiteral("jira"), QJsonObject{
            {"key", r.jira.key}, {"publishedAt", iso(r.jira.publishedAt)}, {"attachedDocument", r.jira.attachedDocument},
            {"uncertain", r.jira.uncertain}, {"lastError", r.jira.lastError},
        });
    if (!r.gesreq.isEmpty())
        o.insert(QStringLiteral("gesreq"), QJsonObject{
            {"registeredAt", iso(r.gesreq.registeredAt)}, {"result", toString(r.gesreq.result)}, {"comment", r.gesreq.comment},
            {"requirementState", r.gesreq.requirementState},
            {"attachedDocument", r.gesreq.attachedDocument}, {"uncertain", r.gesreq.uncertain}, {"lastError", r.gesreq.lastError},
        });
    return o;
}

IssueRevision revisionFromJson(const QJsonObject& o) {
    IssueRevision r;
    r.number = o["number"].toInt(1);
    r.phase = o["phase"].toString();
    r.startedAt = dateTime(o["startedAt"]);
    r.closedAt = dateTime(o["closedAt"]);
    r.outcome = qaOutcomeFromString(o["outcome"].toString());
    r.planRunId = o["planRunId"].toString();
    r.record = recordFromJson(o["record"].toObject());
    r.documentPath = o["documentPath"].toString();
    r.documentAt = dateTime(o["documentAt"]);
    const QJsonObject jira = o["jira"].toObject();
    r.jira.key = jira["key"].toString();
    r.jira.publishedAt = dateTime(jira["publishedAt"]);
    r.jira.attachedDocument = jira["attachedDocument"].toBool();
    r.jira.uncertain = jira["uncertain"].toBool();
    r.jira.lastError = jira["lastError"].toString();
    const QJsonObject gesreq = o["gesreq"].toObject();
    r.gesreq.registeredAt = dateTime(gesreq["registeredAt"]);
    r.gesreq.result = qaOutcomeFromString(gesreq["result"].toString());
    r.gesreq.comment = gesreq["comment"].toString();
    r.gesreq.requirementState = gesreq["requirementState"].toString();
    r.gesreq.attachedDocument = gesreq["attachedDocument"].toBool();
    r.gesreq.uncertain = gesreq["uncertain"].toBool();
    r.gesreq.lastError = gesreq["lastError"].toString();
    return r;
}

QJsonObject mapToJson(const QMap<QString, QString>& map) {
    QJsonObject o;
    for (auto it = map.cbegin(); it != map.cend(); ++it) o.insert(it.key(), it.value());
    return o;
}

QMap<QString, QString> mapFromJson(const QJsonValue& value) {
    QMap<QString, QString> map;
    const QJsonObject o = value.toObject();
    for (auto it = o.constBegin(); it != o.constEnd(); ++it)
        if (!it.value().toString().trimmed().isEmpty()) map.insert(it.key(), it.value().toString());
    return map;
}

QJsonObject issueToJson(const Issue& i) {
    QJsonObject o{
        {"id", i.id}, {"title", i.title}, {"notes", i.notes}, {"priority", toString(i.priority)}, {"state", toString(i.state)},
        {"planIds", QJsonArray::fromStringList(i.planIds)},
        {"createdAt", iso(i.createdAt)}, {"updatedAt", iso(i.updatedAt)},
    };
    if (i.isImported()) o.insert(QStringLiteral("requirement"), requirementToJson(i.requirement));
    if (i.isPublished()) o.insert(QStringLiteral("publication"), publicationToJson(i.publication));
    if (!i.revisions.isEmpty()) {
        QJsonArray revisions;
        for (const auto& r : i.revisions) revisions.append(revisionToJson(r));
        o.insert(QStringLiteral("revisions"), revisions);
    }
    if (!i.phases.isEmpty()) o.insert(QStringLiteral("phases"), QJsonArray::fromStringList(i.phases));
    if (!i.zephyr.isEmpty())
        o.insert(QStringLiteral("zephyr"), QJsonObject{
            {"tests", mapToJson(i.zephyr.tests)}, {"cycles", mapToJson(i.zephyr.cycles)}, {"cycleNames", mapToJson(i.zephyr.cycleNames)},
        });
    return o;
}

Issue issueFromJson(const QJsonObject& o) {
    Issue i;
    i.id = o["id"].toString();
    i.title = o["title"].toString();
    i.notes = o["notes"].toString();
    i.priority = priorityFromString(o["priority"].toString());
    i.state = issueStateFromString(o["state"].toString());
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
    for (const auto& v : o["revisions"].toArray()) i.revisions << revisionFromJson(v.toObject());
    i.phases = strings(o["phases"]);
    const QJsonObject zephyr = o["zephyr"].toObject();
    i.zephyr.tests = mapFromJson(zephyr["tests"]);
    i.zephyr.cycles = mapFromJson(zephyr["cycles"]);
    i.zephyr.cycleNames = mapFromJson(zephyr["cycleNames"]);
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
