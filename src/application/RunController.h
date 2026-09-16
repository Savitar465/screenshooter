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
/// No conoce la UI; sólo emite cambios de estado. Los pasos se pueden recorrer en cualquier orden:
/// marcar uno no cierra el camino a los demás y un fallo o un bloqueo no cortan la ejecución.
/// Cada ejecución terminada se archiva en el historial y actualiza la "última ejecución" del caso.
/// La ejecución en curso se guarda en disco para sobrevivir al cierre de la aplicación.
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
    bool canGoBack() const { return !m_run.caseId.isEmpty() && (m_run.finished || m_run.idx > 0); }
    bool canGoNext() const { return !m_run.caseId.isEmpty() && m_run.idx + 1 < m_run.results.size(); }

    void start(const QString& caseId);
    /// Ejecuta los casos en orden; `finish()` pasa al siguiente automáticamente. `environment` es el
    /// ambiente en el que se prueba el ciclo, que queda anotado en él.
    void startSequence(const QStringList& caseIds, const QString& planName = QString(), const QString& planId = QString(),
                       const QString& environment = QString());
    void restart();
    /// La nota se guarda en disco con retardo; `persistSessionNow()` fuerza la escritura.
    void setNote(const QString& note);
    /// Fuerza la escritura de la sesión. Falso (y `saveFailed`) si no se pudo.
    bool persistSessionNow() { return persistSession(); }
    /// Da veredicto al paso en pantalla (lo cambia si ya lo tenía) y pasa al siguiente pendiente.
    void mark(StepResult result);
    /// Pone en pantalla otro paso del caso, marcado o no. Reabre una ejecución ya terminada.
    void goTo(int index);
    /// Paso anterior; sobre una ejecución terminada, reabre el paso en pantalla.
    void back();
    void next();
    /// Corrige (o pone) el veredicto de un paso desde la lista, sin moverse de sitio.
    void setResult(int index, StepResult result);
    /// Cierra la ejecución archivándola —con lo marcado hasta ahora si quedan pasos pendientes—.
    /// Devuelve true si arrancó el siguiente caso de la cola.
    bool finish();
    void abandon();

signals:
    void runChanged();
    /// Arrancó un ciclo de un plan: `planId` es el plan del catálogo (vacío en un ciclo suelto).
    void planStarted(const QString& planRunId, const QString& planId);
    /// Se terminó (o se abandonó) la ejecución de un plan; el informe ya está en el historial.
    void planCompleted(const QString& planRunId);
    void saveFailed(const QString& what);

private:
    void begin(const QString& caseId);
    /// Vuelca al registro del paso en pantalla su nota y lo que lleva corriendo su cronómetro.
    void holdStep();
    /// Pone en pantalla el paso `index`: guarda lo del anterior y retoma la nota y el reloj del nuevo.
    void enterStep(int index);
    void recomputeFinished();
    /// Archiva la ejecución en el historial y registra el veredicto en el caso. `evenIfPending`
    /// archiva también una ejecución a medias (los pasos sin marcar quedan como N/A).
    void commitRun(bool evenIfPending);
    void closePlan();
    /// Emite runChanged() y programa el guardado de la sesión.
    void changed();
    bool persistSession();

    TestCaseStore& m_store;
    RunHistoryStore& m_history;
    std::shared_ptr<IRunSessionRepository> m_session;
    RunState m_run;
    QStringList m_queue;
    QString m_planRunId;
    QTimer m_saveTimer;
};

} // namespace qaflow
