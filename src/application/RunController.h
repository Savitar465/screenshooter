#pragma once

#include "core/models/TestRun.h"
#include "core/services/IRunSessionRepository.h"

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <memory>

namespace qaflow {

class TestCaseStore;
class RunHistoryStore;

/// Ejecución manual paso a paso de un caso (o de una cola de casos: plan de pruebas).
/// No conoce la UI; sólo emite cambios de estado. Cada ejecución terminada se archiva
/// en el historial y actualiza la "última ejecución" del caso. La ejecución en curso se
/// guarda en disco para sobrevivir al cierre de la aplicación.
class RunController : public QObject {
    Q_OBJECT
public:
    RunController(TestCaseStore& store, RunHistoryStore& history,
                  std::shared_ptr<IRunSessionRepository> session = nullptr, QObject* parent = nullptr);
    ~RunController() override;

    /// Restaura la ejecución guardada, si la hay y su caso sigue existiendo.
    void load();

    const RunState& state() const { return m_run; }
    bool isRunning() const { return m_run.isActive(); }
    int totalSteps() const;
    int queuedCount() const { return m_queue.size(); }
    /// Id de la ejecución de plan en curso (vacío si el caso se ejecuta suelto).
    QString planRunId() const { return m_planRunId; }

    void start(const QString& caseId);
    /// Ejecuta los casos en orden; `finish()` pasa al siguiente automáticamente.
    void startSequence(const QStringList& caseIds, const QString& planName = QString(), const QString& planId = QString());
    void restart();
    /// La nota se guarda en disco con retardo; `persistSessionNow()` fuerza la escritura.
    void setNote(const QString& note);
    void persistSessionNow() { persistSession(); }
    void mark(StepResult result);
    /// Deshace el último veredicto y vuelve a ese paso (también reabre una ejecución terminada).
    void back();
    /// Corrige el veredicto de un paso ya marcado. Si se quita un bloqueo, la ejecución continúa.
    void setResult(int index, StepResult result);
    /// Cierra la ejecución actual archivándola. Devuelve true si arrancó el siguiente caso de la cola.
    bool finish();
    void abandon();

signals:
    void runChanged();
    /// Se terminó (o se abandonó) la ejecución de un plan; el informe ya está en el historial.
    void planCompleted(const QString& planRunId);

private:
    void begin(const QString& caseId);
    void startStepClock();
    void recomputeFinished();
    /// Si la ejecución actual ha terminado, la archiva en el historial y registra el veredicto en el caso.
    void commitIfFinished();
    void closePlan();
    /// Emite runChanged() y programa el guardado de la sesión.
    void changed();
    void persistSession();

    TestCaseStore& m_store;
    RunHistoryStore& m_history;
    std::shared_ptr<IRunSessionRepository> m_session;
    RunState m_run;
    QStringList m_queue;
    QString m_planRunId;
    QTimer m_saveTimer;
};

} // namespace qaflow
