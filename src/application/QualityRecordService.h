#pragma once

#include "core/models/IssueProgress.h"
#include "core/models/QualityRecordDraft.h"
#include "core/services/IQualityRecordWriter.h"

#include <QObject>
#include <memory>

namespace qaflow {

class BugStore;
class IssueStore;
class RunHistoryStore;
class SettingsStore;
class TestCaseStore;

/// El acta de control de calidad de un issue: la propone con lo que hay en el proyecto, la escribe en
/// disco y la guarda en la revisión para poder regenerarla sin volver a teclearla.
///
/// No decide nada por su cuenta: quien cierra la revisión elige el resultado y quién manda el acta a
/// Jira o a GESREQ. Aquí sólo se reúne lo que dice el proyecto.
class QualityRecordService : public QObject {
    Q_OBJECT
public:
    QualityRecordService(IssueStore& issues, TestCaseStore& cases, RunHistoryStore& history, BugStore& bugs,
                         SettingsStore& settings, std::shared_ptr<IQualityRecordWriter> writer, QObject* parent = nullptr);

    /// Cómo va el control de calidad del issue: lo ejecutado de la revisión en curso y qué resultado
    /// se propone.
    IssueProgress progressFor(const QString& issueId) const;

    /// El acta propuesta para su revisión: la que ya tuviera guardada, completada con lo que haya
    /// cambiado desde entonces, o una nueva con lo que sabe el proyecto.
    QualityRecord draftFor(const QString& issueId) const;

    /// Nombre propuesto del fichero: `ControlCalidad_<GREQ>_<marca de tiempo>.docx`.
    QString suggestedFileName(const QString& issueId) const;

    struct GenerateResult {
        bool ok = false;
        QString path;
        QString error;
    };
    /// Escribe el acta en `path` y la guarda en la revisión del issue (con lo escrito en ella, para
    /// la próxima vez). El issue queda como estaba si no se pudo escribir.
    GenerateResult generate(const QString& issueId, const QualityRecord& record, const QString& path);

    /// Resumen del resultado para el comentario de Jira y el registro en GESREQ.
    QString summaryFor(const QString& issueId, const QualityRecord& record, QaOutcome outcome) const;

    /// Ejecuciones de la revisión en curso (o de la última cerrada), la más reciente primero.
    QList<RunRecord> revisionRuns(const Issue& issue) const;
    /// Bugs reportados desde los casos del issue en esa revisión.
    QList<IssueLink> revisionBugs(const Issue& issue) const;

private:
    /// La última acta escrita en el proyecto (de cualquier issue): de ahí se heredan los datos del
    /// entorno que no cambian entre requerimientos.
    QualityRecord previousRecord(const QString& issueId) const;
    /// Cuándo empezó la revisión que se está mirando; inválida si el issue no tiene ninguna.
    QDateTime revisionStart(const Issue& issue) const;

    IssueStore& m_issues;
    TestCaseStore& m_cases;
    RunHistoryStore& m_history;
    BugStore& m_bugs;
    SettingsStore& m_settings;
    std::shared_ptr<IQualityRecordWriter> m_writer;
};

} // namespace qaflow
