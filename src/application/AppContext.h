#pragma once

#include "application/BugReportService.h"
#include "application/BugStore.h"
#include "application/CaseTransferService.h"
#include "application/EvidenceService.h"
#include "application/PlanStore.h"
#include "application/ProjectStore.h"
#include "application/RunController.h"
#include "application/RunHistoryStore.h"
#include "application/SettingsStore.h"
#include "application/TestCaseStore.h"
#include "application/TestPublishService.h"
#include "core/services/IGlobalHotkey.h"

#include <QString>

namespace qaflow {

/// Servicios de la capa de aplicación ya construidos (raíz de composición en main.cpp).
/// La presentación sólo depende de estos tipos; nunca de la infraestructura.
struct AppContext {
    ProjectStore* projects = nullptr;
    QString projectId;
    TestCaseStore* cases = nullptr;
    PlanStore* plan = nullptr;
    RunController* run = nullptr;
    RunHistoryStore* history = nullptr;
    SettingsStore* settings = nullptr;
    EvidenceService* evidence = nullptr;
    BugReportService* bugs = nullptr;
    BugStore* bugLedger = nullptr;
    CaseTransferService* transfer = nullptr;
    /// Publicación de ciclos en Zephyr (puede ser nullptr en tests).
    TestPublishService* publish = nullptr;
    /// Atajo global del sistema (puede ser nullptr en tests). Ajustes muestra su `status()`.
    IGlobalHotkey* hotkey = nullptr;
    /// Directorio de datos (cases.json, history.json…), para mostrarlo o abrirlo desde la UI.
    QString dataDir;
    /// Cómo se capturan las pantallas ("QScreen::grabWindow", "xdg-desktop-portal (Wayland)"), para Ajustes.
    QString captureBackend;
};

} // namespace qaflow
