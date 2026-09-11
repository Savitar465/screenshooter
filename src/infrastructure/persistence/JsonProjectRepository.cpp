#include "JsonProjectRepository.h"
#include "JsonTestCaseRepository.h"
#include "JsonRunHistoryRepository.h"
#include "JsonBugRepository.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <algorithm>

namespace qaflow {
namespace {
bool validId(const QString& id) { return id == QStringLiteral("default") || (!QUuid(id).isNull() && QUuid(id).toString(QUuid::WithoutBraces) == id); }
}
JsonProjectRepository::JsonProjectRepository(QString root) : m_root(std::move(root)) {}
QString JsonProjectRepository::dataDir(const QString& id) const {
    if (!validId(id)) return {};
    // El proyecto principal conserva los ficheros existentes y sus rutas de evidencias.
    return id == QStringLiteral("default") ? m_root : QDir(m_root).filePath(QStringLiteral("projects/%1").arg(id));
}
std::optional<ProjectCollection> JsonProjectRepository::load() {
    QFile file(QDir(m_root).filePath(QStringLiteral("projects.json")));
    if (!file.exists()) {
        ProjectCollection initial{QStringLiteral("default"), {{QStringLiteral("default"), QStringLiteral("Proyecto principal")}}, {}};
        JsonTestCaseRepository cases(m_root);
        if (const auto existing = cases.loadCases())
            for (const auto& c : *existing)
                if (!c.suite.trimmed().isEmpty() && !initial.suites.contains(c.suite)) initial.suites.append(c.suite);
        return save(initial) ? std::optional(initial) : std::nullopt;
    }
    if (!file.open(QIODevice::ReadOnly)) return std::nullopt;
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) return std::nullopt;
    const auto object = doc.object();
    if (object["version"].toInt() != 1 || !object["projects"].isArray()) return std::nullopt;
    ProjectCollection collection;
    QSet<QString> ids;
    for (const auto& value : object["projects"].toArray()) {
        const auto p = value.toObject();
        const QString id = p["id"].toString(), name = p["name"].toString().trimmed();
        if (!validId(id) || name.isEmpty() || ids.contains(id)) return std::nullopt;
        ids.insert(id);
        collection.projects.append({id, name});
    }
    collection.activeId = object["activeId"].toString();
    if (collection.projects.isEmpty() || !ids.contains(collection.activeId)) return std::nullopt;
    if (object.contains("suites") && !object["suites"].isArray()) return std::nullopt;
    for (const auto& value : object["suites"].toArray()) {
        if (!value.isString()) return std::nullopt;
        const QString name = value.toString();
        if (!name.trimmed().isEmpty() && !collection.suites.contains(name)) collection.suites.append(name);
    }
    const QStringList persisted = collection.suites;
    // Incorporar también las suites de proyectos que aún no se han abierto en esta sesión.
    for (const auto& project : collection.projects) {
        JsonTestCaseRepository cases(dataDir(project.id));
        if (const auto existing = cases.loadCases())
            for (const auto& c : *existing)
                if (!c.suite.trimmed().isEmpty() && !collection.suites.contains(c.suite)) collection.suites.append(c.suite);
    }
    std::sort(collection.suites.begin(), collection.suites.end(), [](const QString& a, const QString& b) { return a.localeAwareCompare(b) < 0; });
    if ((collection.suites != persisted || !object.contains("suites")) && !save(collection)) return std::nullopt;
    return collection;
}
bool JsonProjectRepository::save(const ProjectCollection& collection) {
    if (!QDir().mkpath(m_root)) return false;
    QJsonArray projects;
    for (const auto& p : collection.projects) projects.append(QJsonObject{{"id", p.id}, {"name", p.name}});
    const QByteArray bytes = QJsonDocument(QJsonObject{{"version", 1}, {"activeId", collection.activeId}, {"projects", projects}, {"suites", QJsonArray::fromStringList(collection.suites)}}).toJson();
    QSaveFile file(QDir(m_root).filePath(QStringLiteral("projects.json")));
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
bool JsonProjectRepository::initialize(const QString& id) {
    const QString dir = dataDir(id);
    if (dir.isEmpty() || id == QStringLiteral("default") || QDir(dir).exists()) return false;
    if (!QDir().mkpath(dir)) return false;
    JsonTestCaseRepository cases(dir);
    JsonRunHistoryRepository history(dir);
    JsonBugRepository bugs(dir);
    return cases.saveCases({}) && cases.savePlans({}) && history.saveHistory({}) && bugs.saveLedger({});
}
} // namespace qaflow
