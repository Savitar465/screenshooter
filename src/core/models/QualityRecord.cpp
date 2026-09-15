#include "QualityRecord.h"

#include "core/models/BugReport.h"

namespace qaflow {

namespace {
QString formatDate(const QDate& d) { return d.isValid() ? d.toString(QStringLiteral("dd/MM/yyyy")) : QString(); }
} // namespace

QList<ObservationCount> QualityRecord::emptyObservations() {
    QList<ObservationCount> out;
    for (const auto& type : BugReport::classifications()) out << ObservationCount{type, 0, 0};
    return out;
}

QList<QualityCharacteristic> QualityRecord::defaultCharacteristics() {
    return {
        {QStringLiteral("Existen las ayudas que necesita en las opciones que utilizó"), true, {}},
        {QStringLiteral("Procesos de validación"), true, {}},
        {QStringLiteral("El proceso de ejecución del proceso final fue exitoso"), true, {}},
        {QStringLiteral("El sistema se adecúa al proceso que se está revisando"), true, {}},
    };
}

int QualityRecord::totalObservations() const {
    int total = 0;
    for (const auto& o : observations) total += o.observations;
    return total;
}

int QualityRecord::totalCorrections() const {
    int total = 0;
    for (const auto& o : observations) total += o.corrections;
    return total;
}

QString QualityRecord::reviewDates() const {
    const QString start = formatDate(from);
    const QString end = formatDate(to);
    if (start.isEmpty()) return end;
    if (end.isEmpty() || end == start) return start;
    return QStringLiteral("%1 a %2").arg(start, end);
}

} // namespace qaflow
