#include "CaseTransferService.h"

#include "application/TestCaseStore.h"
#include "core/models/CaseFormats.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

namespace qaflow {

CaseTransferService::CaseTransferService(TestCaseStore& cases, QObject* parent) : QObject(parent), m_cases(cases) {}

CaseTransferService::Format CaseTransferService::formatForPath(const QString& path) {
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == QStringLiteral("csv")) return Format::Csv;
    if (ext == QStringLiteral("md") || ext == QStringLiteral("markdown")) return Format::Markdown;
    return Format::Json;
}

QString CaseTransferService::extension(Format f) {
    switch (f) {
        case Format::Json: return QStringLiteral("json");
        case Format::Csv: return QStringLiteral("csv");
        case Format::Markdown: return QStringLiteral("md");
    }
    return {};
}

CaseTransferService::Result CaseTransferService::exportTo(const QString& path, Format format, const QStringList& ids) const {
    QList<TestCase> selected;
    for (const auto& c : m_cases.cases()) if (ids.isEmpty() || ids.contains(c.id)) selected.append(c);

    QByteArray bytes;
    switch (format) {
        case Format::Json: bytes = QJsonDocument(formats::casesToJson(selected, /*includeShots=*/false)).toJson(QJsonDocument::Indented); break;
        case Format::Csv: bytes = formats::casesToCsv(selected).toUtf8(); break;
        case Format::Markdown: bytes = formats::casesToMarkdown(selected).toUtf8(); break;
    }

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return {false, tr("No se pudo escribir %1").arg(path)};
    f.write(bytes);
    if (!f.commit()) return {false, tr("No se pudo escribir %1").arg(path)};
    return {true, tr("%1 casos exportados a %2").arg(selected.size()).arg(QFileInfo(path).fileName())};
}

CaseTransferService::Result CaseTransferService::importFrom(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {false, tr("No se pudo leer %1").arg(path)};
    const QByteArray bytes = f.readAll();

    QString error;
    std::optional<QList<TestCase>> parsed;
    switch (formatForPath(path)) {
        case Format::Csv: parsed = formats::casesFromCsv(QString::fromUtf8(bytes), &error); break;
        case Format::Json: parsed = formats::casesFromJson(bytes, &error); break;
        case Format::Markdown: error = tr("Markdown es sólo de exportación; importa JSON o CSV"); break;
    }
    if (!parsed) return {false, error.isEmpty() ? tr("Formato no reconocido") : error};
    if (parsed->isEmpty()) return {false, tr("El archivo no contiene casos")};

    const auto [added, updated] = m_cases.mergeCases(*parsed);
    Result r;
    r.ok = true;
    r.added = added;
    r.updated = updated;
    r.message = tr("Importado: %1 añadidos · %2 actualizados").arg(added).arg(updated);
    return r;
}

} // namespace qaflow
