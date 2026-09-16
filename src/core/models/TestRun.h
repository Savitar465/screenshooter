#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

#include <algorithm>

namespace qaflow {

enum class StepResult { Pass, Fail, Block, Skip };

struct StepRecord {
    StepResult result = StepResult::Pass;
    QString note;
    int durationSecs = 0;   // tiempo que el paso estuvo en pantalla (acumulado si se vuelve a él)
    bool marked = false;    // false: el paso todavía no tiene veredicto
};

enum class Verdict { Superado, Fallido, Bloqueado };

struct RunState {
    QString caseId;          // vacío = no hay ejecución
    int idx = 0;             // paso en pantalla (0-based); se mueve libremente por el caso
    /// Un registro por paso del caso. Los que no están `marked` siguen pendientes: se puede saltar
    /// de uno a otro en cualquier orden y dejar huecos sin marcar.
    QList<StepRecord> results;
    QString note;            // observación del paso en pantalla (se vuelca a su registro al salir)
    QDateTime startedAt;
    bool finished = false;   // todos los pasos marcados y sin reabrir

    // Cronómetro del paso actual. `stepElapsedSecs` acumula lo transcurrido en visitas anteriores
    // (la ejecución sobrevive al cierre de la aplicación y se puede volver a un paso ya visto);
    // `stepStartedAt` marca desde cuándo corre en esta visita.
    QDateTime stepStartedAt;
    int stepElapsedSecs = 0;

    bool isActive() const { return !caseId.isEmpty() && !finished; }
    bool isMarked(int i) const { return i >= 0 && i < results.size() && results[i].marked; }
    int markedCount() const {
        int n = 0;
        for (const auto& x : results) if (x.marked) ++n;
        return n;
    }
    bool allMarked() const {
        return !results.isEmpty() && std::all_of(results.cbegin(), results.cend(), [](const StepRecord& r) { return r.marked; });
    }
    int lastMarkedIndex() const {
        for (int i = results.size() - 1; i >= 0; --i) if (results[i].marked) return i;
        return -1;
    }
    /// Primer paso sin marcar a partir de `from`; si no queda ninguno por delante, vuelve a buscar
    /// desde el principio (se pudo dejar un hueco atrás). -1 si están todos marcados.
    int nextPending(int from) const {
        for (int i = std::max(0, from); i < results.size(); ++i) if (!results[i].marked) return i;
        for (int i = 0; i < std::min(from, static_cast<int>(results.size())); ++i) if (!results[i].marked) return i;
        return -1;
    }
    int count(StepResult r) const {
        int n = 0;
        for (const auto& x : results) if (x.marked && x.result == r) ++n;
        return n;
    }
    /// Los pasos saltados (N/A) y los que siguen pendientes no influyen en el veredicto.
    Verdict verdict() const {
        if (count(StepResult::Block) > 0) return Verdict::Bloqueado;
        if (count(StepResult::Fail) > 0) return Verdict::Fallido;
        return Verdict::Superado;
    }
    int firstFailIndex() const {
        for (int i = 0; i < results.size(); ++i) if (results[i].marked && results[i].result == StepResult::Fail) return i;
        return -1;
    }
    int firstBlockIndex() const {
        for (int i = 0; i < results.size(); ++i) if (results[i].marked && results[i].result == StepResult::Block) return i;
        return -1;
    }
    /// Paso del que colgar un bug: el que está en pantalla si falló o quedó bloqueado, si no el
    /// fallo o bloqueo más cercano por detrás y, en último término, el que haya por delante.
    /// -1 si la ejecución no ha visto ningún problema todavía.
    int reportableStepIndex() const {
        const auto broken = [this](int i) {
            return isMarked(i) && (results[i].result == StepResult::Fail || results[i].result == StepResult::Block);
        };
        for (int i = std::min(idx, static_cast<int>(results.size()) - 1); i >= 0; --i) if (broken(i)) return i;
        for (int i = idx + 1; i < results.size(); ++i) if (broken(i)) return i;
        return -1;
    }
    /// Segundos del paso actual (acumulados + los de esta visita).
    int currentStepSecs(const QDateTime& now = QDateTime::currentDateTime()) const {
        return stepElapsedSecs + (stepStartedAt.isValid() && !finished ? static_cast<int>(stepStartedAt.secsTo(now)) : 0);
    }
    /// Segundos de toda la ejecución: lo que se lleva en cada paso visitado + el paso actual.
    int elapsedSecs(const QDateTime& now = QDateTime::currentDateTime()) const {
        int n = currentStepSecs(now);
        for (int i = 0; i < results.size(); ++i) if (i != idx) n += results[i].durationSecs;
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
