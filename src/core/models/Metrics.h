#pragma once

#include "core/models/RunHistory.h"
#include "core/models/TestCase.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <optional>

namespace qaflow {

/// Estado actual de una suite según la última ejecución de cada caso.
struct SuiteMetrics {
    QString suite;
    int cases = 0;
    int passed = 0;
    int failed = 0;
    int blocked = 0;

    int executed() const { return passed + failed + blocked; }
    int notRun() const { return cases - executed(); }
    /// Porcentaje de superados sobre ejecutados (0 si no se ejecutó nada).
    int successRate() const { return executed() ? passed * 100 / executed() : 0; }
};

/// Resultado de un ciclo terminado de un plan, para ver la evolución entre ciclos.
struct CycleMetrics {
    QString planRunId;
    QString planId;
    QString name;
    QDateTime startedAt;
    QDateTime finishedAt;
    int total = 0;
    int executed = 0;
    int passed = 0;
    int failed = 0;
    int blocked = 0;
    qint64 durationSecs = 0;

    int successRate() const { return executed ? passed * 100 / executed : 0; }
    /// Porcentaje del plan que llegó a ejecutarse.
    int coverage() const { return total ? executed * 100 / total : 0; }
};

/// Totales de todos los casos según su última ejecución.
struct MetricsSummary {
    int cases = 0;
    int passed = 0;
    int failed = 0;
    int blocked = 0;

    int executed() const { return passed + failed + blocked; }
    int successRate() const { return executed() ? passed * 100 / executed() : 0; }
};

/// Funciones puras de cálculo de métricas. No persisten nada: se calculan al vuelo.
namespace metrics {

MetricsSummary summary(const QList<TestCase>& cases);
/// Una entrada por suite (los casos sin suite van en una suite vacía), ordenadas por nombre.
QList<SuiteMetrics> bySuite(const QList<TestCase>& cases);
/// Ciclos terminados en orden cronológico (los de `planId`, o todos si está vacío).
QList<CycleMetrics> cycles(const RunHistory& history, const QString& planId = QString());
/// Puntos de diferencia en la tasa de éxito entre los dos últimos ciclos; nullopt con menos de dos.
std::optional<int> trend(const QList<CycleMetrics>& cycles);

} // namespace metrics

} // namespace qaflow
