#pragma once

#include "core/models/TestRun.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace qaflow {

/// Valores canónicos (se persisten en history.json): "Pasa", "Superado"… No traducir.
QString toString(StepResult r);
QString toString(Verdict v);
StepResult stepResultFromString(const QString& s);
Verdict verdictFromString(const QString& s);
/// Texto para mostrar en el idioma de la interfaz.
QString label(StepResult r);
QString label(Verdict v);

/// "45 s", "4 min 12 s", "1 h 05 min".
QString formatDuration(qint64 secs);

/// Un paso tal y como se ejecutó: el texto de ese momento, su resultado y la nota del tester.
struct RunRecordStep {
    QString action;
    QString expected;
    StepResult result = StepResult::Pass;
    QString note;
    int durationSecs = 0;
};

/// Ejecución terminada de un caso. Es una instantánea: no cambia aunque el caso se edite después.
struct RunRecord {
    QString id;               // R-0001
    QString caseId;           // TC-104
    QString caseTitle;
    QString suite;
    QString planRunId;        // vacío si el caso se ejecutó suelto
    QDateTime startedAt;
    QDateTime finishedAt;
    Verdict verdict = Verdict::Superado;
    QList<RunRecordStep> steps;   // sólo los pasos que llegaron a ejecutarse
    int plannedSteps = 0;         // pasos que tenía el caso ("3 de 5" cuando se bloquea)
    qint64 durationSecs = 0;      // tiempo real de ejecución (suma de los pasos; excluye el tiempo con la app cerrada)
    /// Test de Zephyr creado para esta ejecución al publicar su informe (SHOP-77). Cada ejecución
    /// tiene el suyo: dos ciclos del mismo caso son dos Tests distintos. Vacío hasta publicarla.
    QString testKey;

    int count(StepResult r) const;
    bool hasNotes() const;
};

/// Ejecución de un plan de pruebas: agrupa los RunRecord con el mismo planRunId.
struct PlanRun {
    QString id;               // PR-0001
    QString planId;           // TestPlan del que es ciclo (vacío en registros antiguos)
    QString name;
    QStringList caseIds;      // composición del plan al arrancar, en orden de ejecución
    QDateTime startedAt;
    QDateTime finishedAt;     // inválida mientras el plan sigue en curso
    /// Dónde quedaron estos resultados en la herramienta de gestión de pruebas: el ciclo de Zephyr
    /// que se creó al publicarlos y cuándo se hizo. Vacío mientras no se haya publicado.
    QString zephyrCycleId;
    QDateTime publishedAt;

    bool isFinished() const { return finishedAt.isValid(); }
    bool isPublished() const { return !zephyrCycleId.isEmpty(); }
};

/// Todo el historial. Un único agregado para que la persistencia sea trivial.
struct RunHistory {
    QList<RunRecord> runs;
    QList<PlanRun> plans;
};

} // namespace qaflow
