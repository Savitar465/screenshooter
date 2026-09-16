#pragma once

#include "core/models/IssueProgress.h"
#include "core/models/PlanReport.h"
#include "core/models/QualityRecordDraft.h"
#include "core/services/IQualityRecordWriter.h"

#include <QObject>
#include <memory>

namespace qaflow {

class BugStore;
class IssueStore;
class PlanStore;
class RunHistoryStore;
class SettingsStore;
class TestCaseStore;
class TestPublishService;

/// El acta de control de calidad de un issue: la propone con lo que hay en el proyecto, la escribe en
/// disco y la guarda en la revisión para poder regenerarla sin volver a teclearla.
///
/// Lo que el acta cuenta son las pruebas del issue, y las pruebas del issue son los ciclos de sus
/// planes: si una revisión tuvo varias ejecuciones, el acta se levanta con la que se elija
/// (`cyclesFor`), y en la revisión queda cuál fue.
///
/// No decide nada por su cuenta: quien cierra la revisión elige el resultado y quién manda el acta a
/// Jira o a GESREQ. Aquí sólo se reúne lo que dice el proyecto.
class QualityRecordService : public QObject {
    Q_OBJECT
public:
    /// `publish` (opcional) sólo se usa para enlazar en el acta el ciclo de Zephyr en el que se
    /// publicaron los resultados.
    QualityRecordService(IssueStore& issues, TestCaseStore& cases, PlanStore& plans, RunHistoryStore& history,
                         BugStore& bugs, SettingsStore& settings, std::shared_ptr<IQualityRecordWriter> writer,
                         TestPublishService* publish = nullptr, QObject* parent = nullptr);

    /// Cómo va el control de calidad del issue: lo ejecutado de la revisión en curso y qué resultado
    /// se propone.
    IssueProgress progressFor(const QString& issueId) const;

    /// Ciclos de plan de la revisión (la abierta, o la última cerrada), el más reciente primero; si la
    /// revisión no tiene ninguno, los del issue. Son las ejecuciones entre las que se elige con cuál
    /// se levanta el acta.
    QList<PlanReport> cyclesFor(const QString& issueId) const;
    /// Ciclo con el que se levanta el acta: el guardado en la revisión si sigue existiendo, o el más
    /// reciente de la revisión. Vacío = el acta habla de todos los ciclos de la revisión.
    QString recordCycleFor(const QString& issueId) const;

    /// El acta propuesta para su revisión: la que ya tuviera guardada, completada con lo que haya
    /// cambiado desde entonces, o una nueva con lo que sabe el proyecto. `planRunId` es el ciclo del
    /// que habla; vacío, todos los de la revisión.
    QualityRecord draftFor(const QString& issueId, const QString& planRunId = QString()) const;

    /// Nombre propuesto del fichero: `ControlCalidad_<GREQ>_<marca de tiempo>.docx`.
    QString suggestedFileName(const QString& issueId) const;

    struct GenerateResult {
        bool ok = false;
        QString path;
        QString error;
    };
    /// Escribe el acta en `path` y la guarda en la revisión del issue (con lo escrito en ella y el
    /// ciclo del que habla, para la próxima vez). El issue queda como estaba si no se pudo escribir.
    GenerateResult generate(const QString& issueId, const QualityRecord& record, const QString& path,
                            const QString& planRunId = QString());

    /// Resumen del resultado para el comentario de Jira y el registro en GESREQ.
    QString summaryFor(const QString& issueId, const QualityRecord& record, QaOutcome outcome,
                       const QString& planRunId = QString()) const;

    /// Casos que prueban el issue: los de sus planes.
    QStringList caseIdsOf(const Issue& issue) const;
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
    /// Número de la ronda de la que se habla: la abierta o, si no hay ninguna, la última cerrada (0 si
    /// el issue todavía no tiene revisiones).
    int revisionNumber(const Issue& issue) const;
    /// Los informes de los ciclos con los que se levanta el acta: el elegido, o todos los de la revisión.
    QList<PlanReport> cyclesForRecord(const Issue& issue, const QString& planRunId) const;
    /// Enlaces a los ciclos de Zephyr en los que se publicaron esos ciclos.
    quality::DraftContext contextFor(const Issue& issue, const QList<PlanReport>& cycles) const;

    IssueStore& m_issues;
    TestCaseStore& m_cases;
    PlanStore& m_plans;
    RunHistoryStore& m_history;
    BugStore& m_bugs;
    SettingsStore& m_settings;
    std::shared_ptr<IQualityRecordWriter> m_writer;
    TestPublishService* m_publish = nullptr;
};

} // namespace qaflow
