#pragma once

#include "core/models/RunHistory.h"
#include "core/models/TestCase.h"
#include "core/models/TestRun.h"
#include "core/services/IRunSessionRepository.h"

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <memory>
#include <optional>

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
    /// Ejecución fallada o bloqueada que retoma la que está en curso; vacío si el caso se ejecuta entero.
    QString continuesRunId() const { return m_continuesRunId; }
    /// Casos de la continuación que todavía no se han retomado (los que siguen en la cola).
    int pendingResumes() const { return static_cast<int>(m_resume.size()); }
    /// Casos del ciclo en curso, en el orden del plan; vacío si el caso se ejecuta suelto.
    QStringList planCases() const;
    /// El caso está en la cola del ciclo (sin empezar o aparcado): se puede ir a él con `goToCase`.
    bool isQueued(const QString& caseId) const { return m_queue.contains(caseId); }
    /// Última ejecución que el ciclo archivó de ese caso: la que cuenta en su informe. nullptr si el
    /// caso todavía no tiene ninguna en el ciclo (o no hay ciclo).
    std::optional<RunRecord> archivedRun(const QString& caseId) const;
    /// La ejecución en pantalla es la revisión de una ya archivada (`reviewCase`).
    bool isReviewing() const { return !m_run.reviewOf.isEmpty(); }
    /// Revisión en la que todavía no se ha cambiado nada de lo archivado: ningún veredicto dado de
    /// nuevo, ninguna nota distinta y ninguna evidencia nueva. Dejarla no archiva nada.
    bool reviewUntouched() const;
    /// Estado con el que se dejó un caso del ciclo para ir a otro; nullptr si no se ha empezado.
    const RunState* parkedRun(const QString& caseId) const;
    bool canGoBack() const { return !m_run.caseId.isEmpty() && !m_run.paused && (m_run.finished || m_run.idx > 0); }
    bool canGoNext() const { return !m_run.caseId.isEmpty() && !m_run.paused && m_run.idx + 1 < m_run.results.size(); }
    /// La ejecución en curso está en pausa: sus cronómetros no corren y no admite veredictos ni moverse de paso.
    bool isPaused() const { return isRunning() && m_run.paused; }

    void start(const QString& caseId);
    /// Ejecuta los casos en orden; `finish()` pasa al siguiente automáticamente. `environment` es el
    /// ambiente en el que se prueba el ciclo, que queda anotado en él.
    void startSequence(const QStringList& caseIds, const QString& planName = QString(), const QString& planId = QString(),
                       const QString& environment = QString());
    /// Continúa un ciclo terminado: vuelve a ejecutar **sólo sus casos fallados y bloqueados**, cada uno
    /// retomado en el paso que se rompió (los anteriores conservan su veredicto y su nota).
    /// **Se reabre el mismo ciclo**: lo ya probado, sus evidencias, sus bugs y su ciclo de Zephyr siguen
    /// ahí, y lo repetido sustituye a lo roto. El ambiente es el suyo (`environment` sólo se anota si no
    /// tenía). Si la ronda del issue ya se cerró, `planStarted` lo lleva a la siguiente.
    /// Falso si el ciclo no existe, no terminó, no dejó nada roto o ninguno de sus casos sigue estando.
    /// Con `planCases` (lo que tiene hoy su plan) continúa además lo que el ciclo dejó sin ejecutar y lo
    /// que se añadió al plan después de arrancarlo (`PlanReport::toContinue`).
    bool continueCycle(const QString& planRunId, const QString& environment = QString(), const QStringList& planCases = {});
    /// Lo que cambió en el ciclo en curso al seguir a su plan.
    struct PlanSync {
        QStringList added;     // casos nuevos, al final de la cola
        QStringList removed;   // casos quitados del plan que todavía no se habían empezado
        bool isEmpty() const { return added.isEmpty() && removed.isEmpty(); }
    };
    /// El plan del ciclo en curso cambió: `planCases` es lo que tiene ahora. Los casos nuevos entran al
    /// ciclo y a su cola; los quitados salen si todavía no se empezaron —el que está en pantalla, uno
    /// aparcado o uno ya ejecutado se quedan, para no perder lo probado—. Emite `planCasesChanged`.
    PlanSync syncPlanCases(const QStringList& planCases);
    void restart();
    /// Pone en pausa la ejecución en curso: el tiempo que pase hasta reanudarla no cuenta para el paso ni
    /// para la ejecución. Sólo una ejecución activa (no terminada) se pausa. Sobrevive al cierre.
    void pause();
    void resume();
    void togglePause() { if (isPaused()) resume(); else pause(); }
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
    /// Pone en pantalla otro caso pendiente del ciclo. El que estaba se aparca tal cual —veredictos,
    /// notas, cronómetros— sin archivarse, y vuelve a la cola: se retoma donde se dejó al volver a él.
    /// Falso si no hay ciclo o el caso no está en su cola (ya archivado, o no es del plan).
    bool goToCase(const QString& caseId);
    /// Pone en pantalla, para revisarlo, un caso que el ciclo ya archivó (un superado mientras se
    /// continúa lo roto, por ejemplo). Se abre en su primer paso con todo lo que se archivó heredado
    /// —veredictos, notas y evidencias— y se recorre como cualquier ejecución. El caso que estaba en
    /// pantalla se aparca, como con `goToCase`. Corregir algo (un veredicto, una nota, una evidencia)
    /// la convierte en una ejecución nueva del ciclo, que al cerrarla sustituye a la archivada en el
    /// informe; sin tocar nada, al dejarla el caso sigue como estaba.
    /// Falso si no hay ciclo, el caso es el de pantalla, sigue en la cola o no tiene nada archivado.
    bool reviewCase(const QString& caseId);
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
    /// El ciclo en curso siguió un cambio de su plan (`syncPlanCases`).
    void planCasesChanged(const QString& planRunId, const QStringList& added, const QStringList& removed);
    void saveFailed(const QString& what);

private:
    void begin(const QString& caseId);
    /// Id para una ejecución que arranca: libre también entre las que siguen vivas (las aparcadas).
    QString reserveRunId() const;
    /// Deja el caso de pantalla para poner otro: se aparca en la cola tal cual, salvo una revisión sin
    /// cambios, que no tiene nada que guardar y devuelve el caso a lo archivado.
    void setAside();
    /// Pone en pantalla un caso de la cola: el aparcado tal como se dejó, o uno nuevo con `begin`.
    void enterCase(const QString& caseId);
    /// Deja el estado como lo dejó la ejecución que se retoma: lo anterior al paso roto se conserva
    /// marcado y la ejecución empieza en ese paso. Si el caso se editó desde entonces, sólo se hereda
    /// el tramo cuyos pasos siguen siendo los mismos.
    void resumeFrom(const RunRecord& previous, const TestCase& c);
    /// Vuelca al registro del paso en pantalla su nota y lo que lleva corriendo su cronómetro.
    void holdStep();
    /// Arranca el cronómetro del paso en pantalla, salvo en pausa (entonces se queda parado).
    void startStepClock();
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
    /// Ejecuciones que retoman los casos de una continuación, por caso. Se consumen al arrancar cada uno.
    QHash<QString, RunRecord> m_resume;
    /// Ejecución que retoma la que está en curso; se archiva con ella.
    QString m_continuesRunId;
    /// Casos del ciclo que se empezaron y se dejaron para ir a otro, por caso. Siguen en la cola.
    QHash<QString, ParkedRun> m_parked;
    QTimer m_saveTimer;
};

} // namespace qaflow
