#pragma once

#include "core/models/PlanReport.h"
#include "core/models/TestPlan.h"
#include "core/services/ITestCaseRepository.h"

#include <QObject>
#include <memory>
#include <optional>

namespace qaflow {

class TestCaseStore;
class RunHistoryStore;

/// Planes de pruebas. Hay un plan "activo" (el abierto en la pantalla de planes); las
/// mutaciones de contenido (`toggle`, `moveCase`, …) actúan sobre él. Cada ejecución de un
/// plan es un ciclo: el historial guarda un PlanRun por ciclo y de ahí sale el progreso.
class PlanStore : public QObject {
    Q_OBJECT
public:
    PlanStore(std::shared_ptr<ITestCaseRepository> repo, TestCaseStore& cases, RunHistoryStore& history, QObject* parent = nullptr);

    void load();

    // Colección
    const QList<TestPlan>& plans() const { return m_plans; }
    const TestPlan* find(const QString& id) const;
    QString activeId() const { return m_activeId; }
    const TestPlan* active() const { return find(m_activeId); }
    void setActive(const QString& id);

    QString createPlan(const QString& name);
    /// Copia con el mismo contenido, sin ciclos. Devuelve el id nuevo.
    QString duplicatePlan(const QString& id);
    void setArchived(const QString& id, bool archived);
    void removePlan(const QString& id);

    // Contenido del plan activo
    void setName(const QString& name);
    void toggle(const QString& caseId);           // añade al final o quita
    void moveCase(const QString& caseId, int delta);
    void selectAll();
    void selectNone();
    void selectHighPriority();
    void sortByPriority();

    /// Casos del plan activo en su orden (ignora ids inexistentes u obsoletos).
    QStringList orderedCaseIds() const { return orderedCaseIds(m_activeId); }
    QStringList orderedCaseIds(const QString& planId) const;
    int totalSteps() const;
    /// Segundos estimados: la media real de cada caso según el historial; para los casos sin
    /// historial, la media global por paso; si no hay datos, 3 min por paso.
    int estimatedSecs() const;
    QString estimatedTime() const;
    /// En qué se basa la estimación ("según 4 ejecuciones", "3 min por paso · sin historial").
    QString estimateBasis() const;

    /// Último ciclo (ejecución) de un plan, terminado o en curso. nullopt si nunca se ejecutó.
    std::optional<PlanReport> latestCycle(const QString& planId) const;
    int cycleCount(const QString& planId) const;

    /// Escribe la colección en disco. Falso (y `saveFailed`) si no se pudo.
    bool save();

signals:
    /// Cambió el contenido del plan activo (o cuál es el activo).
    void planChanged();
    /// Cambió la colección: alta, baja, archivado, nombre.
    void plansChanged();
    void saveFailed(const QString& what);

private:
    TestPlan* activePlan();
    QString nextId() const;
    void persist();

    std::shared_ptr<ITestCaseRepository> m_repo;
    TestCaseStore& m_cases;
    RunHistoryStore& m_history;
    QList<TestPlan> m_plans;
    QString m_activeId;
};

} // namespace qaflow
