#pragma once

#include "core/models/IssueLink.h"
#include "core/models/PlanReport.h"
#include "core/models/RunHistory.h"
#include "core/models/TestCase.h"
#include "core/services/IRunHistoryRepository.h"

#include <QObject>
#include <memory>

namespace qaflow {

class TestCaseStore;
class BugStore;

/// Fuente de verdad del historial de ejecuciones. Sólo crece: cada ejecución terminada
/// añade un RunRecord; cada plan arrancado añade un PlanRun que se cierra al acabar.
class RunHistoryStore : public QObject {
    Q_OBJECT
public:
    RunHistoryStore(std::shared_ptr<IRunHistoryRepository> repo, TestCaseStore& cases, QObject* parent = nullptr);

    void load();
    /// Libro de bugs del proyecto: con él, el informe de cada ciclo trae los bugs que se reportaron
    /// mientras corría. Se pone aparte porque el libro se crea después que el historial; sin él los
    /// informes salen sin bugs, que es lo que quieren los tests que no los miran.
    void setBugs(const BugStore* bugs) { m_bugs = bugs; }

    const QList<RunRecord>& runs() const { return m_history.runs; }
    const QList<PlanRun>& plans() const { return m_history.plans; }
    const RunRecord* findRun(const QString& id) const;
    const PlanRun* findPlan(const QString& id) const;

    /// Ejecuciones de un caso, la más reciente primero.
    QList<RunRecord> runsForCase(const QString& caseId) const;
    QList<RunRecord> runsForPlan(const QString& planRunId) const;
    /// Informe de un plan. Los títulos de los casos pendientes se resuelven contra los casos actuales
    /// y los bugs, contra el libro de `setBugs()`.
    PlanReport report(const QString& planRunId) const;
    /// Evidencias de una ejecución archivada: las suyas y, si retoma otra, las de los pasos que heredó
    /// de ella (con el `runId` de la ejecución en que se tomaron). Son las que prueban sus veredictos:
    /// un paso heredado no se volvió a probar, y su prueba sigue siendo la de entonces.
    QList<Screenshot> evidenceOf(const RunRecord& run) const;
    /// Evidencias de la ejecución `runId` asignadas a los pasos `steps` (1..N), incluidas las que ésta
    /// heredó a su vez. Es lo que trae una continuación de los pasos que no vuelve a probar.
    QList<Screenshot> evidenceOfSteps(const QString& runId, const QList<int>& steps) const;

    /// Abre una ejecución de plan y devuelve su id (vacío si no hay casos). `environment` es el
    /// ambiente en el que se va a probar ("QA", "Staging"…), que acompaña al ciclo hasta Zephyr.
    /// `continuesCycleId` lo marca como continuación de ese ciclo: hereda su issue y su revisión,
    /// porque continuar es seguir con la misma ronda de pruebas, no empezar otra.
    QString startPlan(const QString& name, const QStringList& caseIds, const QString& planId = QString(),
                      const QString& environment = QString(), const QString& continuesCycleId = QString());
    void finishPlan(const QString& planRunId);
    /// Anota de qué control de calidad es el ciclo: el issue del requerimiento y la revisión que
    /// estaba abierta al arrancarlo. Lo llama quien coordina el arranque, en cuanto el issue abre su
    /// ronda; sin issue (ciclo suelto) no hay nada que anotar.
    void noteCycleRevision(const QString& planRunId, const QString& issueId, int revision);
    /// Último ambiente en el que se probó en este proyecto (el del ciclo más reciente que lo indique);
    /// vacío si todavía no se anotó ninguno. Es lo que se propone al arrancar el ciclo siguiente.
    QString lastEnvironment() const;
    /// Anota en el ciclo de plan el ciclo de Zephyr en el que se publicaron sus resultados.
    void markPublished(const QString& planRunId, const QString& zephyrCycleId);
    /// Guarda en cada ejecución (R-0007 → SHOP-77) el Test de Zephyr que se creó para ella al
    /// publicarla: volver a publicar ese informe reutiliza esos Tests en vez de estrenar otros.
    void assignTestKeys(const QHash<QString, QString>& testKeyByRunId);
    /// Migración de datos anteriores a que la evidencia fuera de la ejecución: las evidencias
    /// sueltas de cada caso pasan a su última ejecución, y las de un caso que nunca se ejecutó se
    /// descartan (los ficheros no se tocan). `runningCaseId` es el caso que se está ejecutando
    /// ahora, cuyas evidencias son de esa ejecución y todavía no pueden sellarse.
    /// Devuelve cuántas evidencias se movieron o descartaron.
    int adoptLooseEvidence(const QString& runningCaseId = QString());
    /// Id que le tocará a la próxima ejecución que se archive. Se pide al arrancarla, no al
    /// cerrarla, para que lo que se reporte mientras corre (los bugs) pueda enlazarse con ella.
    /// Mientras no se archive nada, dos llamadas devuelven el mismo id.
    QString reserveRunId() const;
    /// Añade una ejecución terminada. Respeta el id reservado si trae uno libre; si no, le asigna el
    /// siguiente. Devuelve el registro guardado.
    RunRecord addRun(RunRecord record);
    /// Bugs que se encontraron en esa ejecución, del más reciente al primero. Salen del libro de
    /// `setBugs()`; sin él, ninguno.
    QList<IssueLink> bugsOfRun(const RunRecord& run) const;
    /// Elimina un ciclo de plan con sus ejecuciones y las evidencias de éstas (los ficheros se
    /// liberan a través de TestCaseStore). Si a algún caso se le borró su última ejecución, su
    /// «última ejecución» vuelve a ser la más reciente que quede. Definitivo: no se puede deshacer.
    /// Falso si el ciclo no existe. No comprueba si está en curso: eso lo decide quien llama.
    bool removePlanRun(const QString& planRunId);

    /// Escribe el historial en disco. Falso (y `saveFailed`) si no se pudo.
    bool save();

signals:
    void historyChanged();
    void saveFailed(const QString& what);

private:
    void persist();

    std::shared_ptr<IRunHistoryRepository> m_repo;
    TestCaseStore& m_cases;
    const BugStore* m_bugs = nullptr;
    RunHistory m_history;
};

} // namespace qaflow
