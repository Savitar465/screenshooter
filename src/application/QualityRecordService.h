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

    /// Todo lo que sigue habla de **una ronda**: `revision` es su número y 0 (lo habitual) significa la
    /// que está en curso —la abierta o, si ninguna lo está, la última—. Pasando el número se levanta el
    /// acta o se publica el resultado de una ronda anterior que se quedó a medias.

    /// Ciclos de plan de la ronda, el más reciente primero; si la ronda en curso no tiene ninguno, los
    /// del issue (una ronda cerrada se queda con los suyos: sus ciclos son los que se probaron
    /// entonces). Son las ejecuciones entre las que se elige con cuál se levanta el acta.
    QList<PlanReport> cyclesFor(const QString& issueId, int revision = 0) const;
    /// Ciclo con el que se levanta el acta: el guardado en la revisión si sigue existiendo, o el más
    /// reciente de la revisión. Vacío = el acta habla de todos los ciclos de la revisión.
    QString recordCycleFor(const QString& issueId, int revision = 0) const;

    /// El acta propuesta para su revisión: la que ya tuviera guardada, completada con lo que haya
    /// cambiado desde entonces, o una nueva con lo que sabe el proyecto. `planRunId` es el ciclo del
    /// que habla; vacío, todos los de la revisión.
    QualityRecord draftFor(const QString& issueId, const QString& planRunId = QString(), int revision = 0) const;

    /// Nombre propuesto del fichero: `ControlCalidad_<GREQ>_rev<N>_<marca de tiempo>.docx`. La ronda va
    /// en el nombre porque un requerimiento observado levanta un acta por ronda.
    QString suggestedFileName(const QString& issueId, int revision = 0) const;

    struct GenerateResult {
        bool ok = false;
        QString path;
        QString error;
    };
    /// Escribe el acta en `path` y la guarda en la revisión del issue (con lo escrito en ella y el
    /// ciclo del que habla, para la próxima vez). El issue queda como estaba si no se pudo escribir.
    GenerateResult generate(const QString& issueId, const QualityRecord& record, const QString& path,
                            const QString& planRunId = QString(), int revision = 0);

    /// Resumen del resultado para el comentario de Jira y el registro en GESREQ.
    QString summaryFor(const QString& issueId, const QualityRecord& record, QaOutcome outcome,
                       const QString& planRunId = QString(), int revision = 0) const;

    /// Casos que prueban el issue: los de sus planes.
    QStringList caseIdsOf(const Issue& issue) const;
    /// Ejecuciones de la ronda, la más reciente primero.
    QList<RunRecord> revisionRuns(const Issue& issue, int revision = 0) const;
    /// Bugs reportados desde los casos del issue durante esa ronda (de cuándo se abrió a cuándo se
    /// cerró; los de la ronda en curso, hasta ahora).
    QList<IssueLink> revisionBugs(const Issue& issue, int revision = 0) const;

private:
    /// La última acta escrita en el proyecto (de cualquier issue): de ahí se heredan los datos del
    /// entorno que no cambian entre requerimientos.
    QualityRecord previousRecord(const QString& issueId) const;
    /// Cuándo empezó la ronda; inválida si el issue no la tiene.
    QDateTime revisionStart(const Issue& issue, int revision = 0) const;
    /// Cuándo se cerró la ronda; inválida si sigue abierta o si el issue no la tiene.
    QDateTime revisionEnd(const Issue& issue, int revision = 0) const;
    /// Número de la ronda de la que se habla: el que se pida o, con 0, la abierta o la última cerrada
    /// (0 si el issue todavía no tiene revisiones).
    int revisionNumber(const Issue& issue, int revision = 0) const;
    /// Los informes de los ciclos con los que se levanta el acta: el elegido, o todos los de la revisión.
    QList<PlanReport> cyclesForRecord(const Issue& issue, const QString& planRunId, int revision = 0) const;
    /// Enlaces a los ciclos de Zephyr en los que se publicaron esos ciclos.
    quality::DraftContext contextFor(const Issue& issue, const QList<PlanReport>& cycles, int revision = 0) const;

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
