#pragma once

#include "core/models/TestRun.h"

#include <QObject>
#include <QStringList>

namespace qaflow {

class TestCaseStore;
class RunHistoryStore;

/// Ejecución manual paso a paso de un caso (o de una cola de casos: plan de pruebas).
/// No conoce la UI; sólo emite cambios de estado. Cada ejecución terminada se archiva
/// en el historial y actualiza la "última ejecución" del caso.
class RunController : public QObject {
    Q_OBJECT
public:
    RunController(TestCaseStore& store, RunHistoryStore& history, QObject* parent = nullptr);

    const RunState& state() const { return m_run; }
    bool isRunning() const { return m_run.isActive(); }
    int totalSteps() const;
    int queuedCount() const { return m_queue.size(); }
    /// Id de la ejecución de plan en curso (vacío si el caso se ejecuta suelto).
    QString planRunId() const { return m_planRunId; }

    void start(const QString& caseId);
    /// Ejecuta los casos en orden; `finish()` pasa al siguiente automáticamente.
    void startSequence(const QStringList& caseIds, const QString& planName = QString());
    void restart();
    void setNote(const QString& note);
    void mark(StepResult result);
    /// Cierra la ejecución actual archivándola. Devuelve true si arrancó el siguiente caso de la cola.
    bool finish();
    void abandon();

signals:
    void runChanged();
    /// Se terminó (o se abandonó) la ejecución de un plan; el informe ya está en el historial.
    void planCompleted(const QString& planRunId);

private:
    void begin(const QString& caseId);
    /// Si la ejecución actual ha terminado, la archiva en el historial y registra el veredicto en el caso.
    void commitIfFinished();
    void closePlan();

    TestCaseStore& m_store;
    RunHistoryStore& m_history;
    RunState m_run;
    QStringList m_queue;
    QString m_planRunId;
};

} // namespace qaflow
