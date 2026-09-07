#pragma once

#include "core/models/TestRun.h"

#include <QObject>
#include <QStringList>

namespace qaflow {

class TestCaseStore;

/// Ejecución manual paso a paso de un caso (o de una cola de casos: plan de pruebas).
/// No conoce la UI; sólo emite cambios de estado.
class RunController : public QObject {
    Q_OBJECT
public:
    explicit RunController(TestCaseStore& store, QObject* parent = nullptr);

    const RunState& state() const { return m_run; }
    bool isRunning() const { return m_run.isActive(); }
    int totalSteps() const;
    int queuedCount() const { return m_queue.size(); }

    void start(const QString& caseId);
    /// Ejecuta los casos en orden; `finish()` pasa al siguiente automáticamente.
    void startSequence(const QStringList& caseIds);
    void restart();
    void setNote(const QString& note);
    void mark(StepResult result);
    /// Cierra la ejecución actual registrando el veredicto en el caso.
    /// Devuelve true si arrancó el siguiente caso de la cola.
    bool finish();
    void abandon();

signals:
    void runChanged();

private:
    void recordVerdict();

    TestCaseStore& m_store;
    RunState m_run;
    QStringList m_queue;
};

} // namespace qaflow
