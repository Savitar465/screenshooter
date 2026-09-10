#pragma once

#include "core/models/PlanReport.h"
#include "core/models/RunHistory.h"
#include "core/services/IRunHistoryRepository.h"

#include <QObject>
#include <memory>

namespace qaflow {

class TestCaseStore;

/// Fuente de verdad del historial de ejecuciones. Sólo crece: cada ejecución terminada
/// añade un RunRecord; cada plan arrancado añade un PlanRun que se cierra al acabar.
class RunHistoryStore : public QObject {
    Q_OBJECT
public:
    RunHistoryStore(std::shared_ptr<IRunHistoryRepository> repo, TestCaseStore& cases, QObject* parent = nullptr);

    void load();

    const QList<RunRecord>& runs() const { return m_history.runs; }
    const QList<PlanRun>& plans() const { return m_history.plans; }
    const RunRecord* findRun(const QString& id) const;
    const PlanRun* findPlan(const QString& id) const;

    /// Ejecuciones de un caso, la más reciente primero.
    QList<RunRecord> runsForCase(const QString& caseId) const;
    QList<RunRecord> runsForPlan(const QString& planRunId) const;
    /// Informe de un plan. Los títulos de los casos pendientes se resuelven contra los casos actuales.
    PlanReport report(const QString& planRunId) const;

    /// Abre una ejecución de plan y devuelve su id (vacío si no hay casos).
    QString startPlan(const QString& name, const QStringList& caseIds, const QString& planId = QString());
    void finishPlan(const QString& planRunId);
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
    /// Añade una ejecución terminada. Asigna el id y devuelve el registro guardado.
    RunRecord addRun(RunRecord record);
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
    RunHistory m_history;
};

} // namespace qaflow
