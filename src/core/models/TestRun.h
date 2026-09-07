#pragma once

#include <QList>
#include <QString>

namespace qaflow {

enum class StepResult { Pass, Fail, Block };

struct StepRecord {
    StepResult result = StepResult::Pass;
    QString note;
};

enum class Verdict { Superado, Fallido, Bloqueado };

struct RunState {
    QString caseId;          // vacío = no hay ejecución
    int idx = 0;             // paso actual (0-based)
    QList<StepRecord> results;
    QString note;            // observación del paso actual
    bool finished = false;

    bool isActive() const { return !caseId.isEmpty() && !finished; }
    int count(StepResult r) const {
        int n = 0;
        for (const auto& x : results) if (x.result == r) ++n;
        return n;
    }
    Verdict verdict() const {
        if (count(StepResult::Block) > 0) return Verdict::Bloqueado;
        if (count(StepResult::Fail) > 0) return Verdict::Fallido;
        return Verdict::Superado;
    }
    int firstFailIndex() const {
        for (int i = 0; i < results.size(); ++i) if (results[i].result == StepResult::Fail) return i;
        return -1;
    }
};

} // namespace qaflow
