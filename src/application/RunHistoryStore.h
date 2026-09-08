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
    /// Añade una ejecución terminada. Asigna el id y devuelve el registro guardado.
    RunRecord addRun(RunRecord record);

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
