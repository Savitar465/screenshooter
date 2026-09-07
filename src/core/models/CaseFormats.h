#pragma once

#include "core/models/TestCase.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <optional>

/// Serialización de casos de prueba. Funciones puras: sin disco ni UI.
namespace qaflow::formats {

// ---- JSON (formato nativo; también el del repositorio) ----------------------------------
QJsonObject caseToJson(const TestCase& c, bool includeShots = true);
TestCase caseFromJson(const QJsonObject& o);
QJsonArray casesToJson(const QList<TestCase>& cases, bool includeShots = true);
/// Acepta un array de casos o un objeto {"cases": [...]}. nullopt si el JSON no es válido.
std::optional<QList<TestCase>> casesFromJson(const QByteArray& bytes, QString* error = nullptr);

// ---- CSV (una fila por paso; separador coma, comillas RFC 4180) -------------------------
QString casesToCsv(const QList<TestCase>& cases);
/// Cabecera obligatoria: id,title,suite,priority,status,tags,component,jira,preconditions,step,action,expected.
std::optional<QList<TestCase>> casesFromCsv(const QString& text, QString* error = nullptr);

// ---- Markdown (sólo exportación) ---------------------------------------------------------
QString casesToMarkdown(const QList<TestCase>& cases);

} // namespace qaflow::formats
