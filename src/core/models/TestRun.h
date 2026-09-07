#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace qaflow {

enum class StepResult { Pass, Fail, Block, Skip };

struct StepRecord {
    StepResult result = StepResult::Pass;
    QString note;
    int durationSecs = 0;   // tiempo que el paso estuvo en pantalla hasta marcarlo
};

enum class Verdict { Superado, Fallido, Bloqueado };

struct RunState {
    QString caseId;          // vacío = no hay ejecución
    int idx = 0;             // paso actual (0-based)
    QList<StepRecord> results;
    QString note;            // observación del paso actual
    QDateTime startedAt;
    bool finished = false;

    // Cronómetro del paso actual. `stepElapsedSecs` acumula lo transcurrido en sesiones
    // anteriores (la ejecución sobrevive al cierre de la aplicación); `stepStartedAt`
    // marca desde cuándo corre en esta sesión.
    QDateTime stepStartedAt;
    int stepElapsedSecs = 0;

    bool isActive() const { return !caseId.isEmpty() && !finished; }
    int count(StepResult r) const {
        int n = 0;
        for (const auto& x : results) if (x.result == r) ++n;
        return n;
    }
    /// Los pasos saltados (N/A) no influyen en el veredicto.
    Verdict verdict() const {
        if (count(StepResult::Block) > 0) return Verdict::Bloqueado;
        if (count(StepResult::Fail) > 0) return Verdict::Fallido;
        return Verdict::Superado;
    }
    int firstFailIndex() const {
        for (int i = 0; i < results.size(); ++i) if (results[i].result == StepResult::Fail) return i;
        return -1;
    }
    /// Segundos del paso actual (acumulados + los de esta sesión).
    int currentStepSecs(const QDateTime& now = QDateTime::currentDateTime()) const {
        return stepElapsedSecs + (stepStartedAt.isValid() && !finished ? static_cast<int>(stepStartedAt.secsTo(now)) : 0);
    }
    /// Segundos de toda la ejecución: pasos marcados + paso actual.
    int elapsedSecs(const QDateTime& now = QDateTime::currentDateTime()) const {
        int n = currentStepSecs(now);
        for (const auto& x : results) n += x.durationSecs;
        return n;
    }
};

/// Ejecución en curso tal y como se guarda en disco para sobrevivir al cierre.
struct RunSession {
    RunState run;
    QStringList queue;     // casos del plan pendientes
    QString planRunId;     // ejecución de plan abierta en el historial (vacío si es suelta)
};

} // namespace qaflow
