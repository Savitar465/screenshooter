#pragma once

#include "core/models/TestCase.h"

#include <QList>
#include <QStringList>

namespace qaflow::seed {

/// Datos de ejemplo para el primer arranque (coinciden con el diseño de referencia).
QList<TestCase> sampleCases();
QStringList defaultSuites();
QStringList defaultPlanIds();
QString defaultSelection(const QList<TestCase>& cases);

} // namespace qaflow::seed
