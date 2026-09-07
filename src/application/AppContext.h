#pragma once

#include "application/BugReportService.h"
#include "application/EvidenceService.h"
#include "application/PlanStore.h"
#include "application/RunController.h"
#include "application/SettingsStore.h"
#include "application/TestCaseStore.h"

namespace qaflow {

/// Servicios de la capa de aplicación ya construidos (raíz de composición en main.cpp).
/// La presentación sólo depende de estos tipos; nunca de la infraestructura.
struct AppContext {
    TestCaseStore* cases = nullptr;
    PlanStore* plan = nullptr;
    RunController* run = nullptr;
    SettingsStore* settings = nullptr;
    EvidenceService* evidence = nullptr;
    BugReportService* bugs = nullptr;
};

} // namespace qaflow
