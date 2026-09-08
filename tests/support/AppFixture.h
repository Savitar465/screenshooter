#pragma once

// Capa de aplicación completa montada sobre repositorios en memoria y cargada con los datos
// de ejemplo (7 casos, un plan). Es el punto de partida de los tests de application/.
//
// Datos de ejemplo útiles (ver SeedData.cpp):
//   TC-101 Autenticación · Alta  · 3 pasos · Pasó       TC-105 Checkout · Media · 2 pasos · Pasó
//   TC-102 Autenticación · Alta  · 2 pasos · Falló      TC-106 Perfil · Baja · 2 pasos
//   TC-103 Autenticación · Media · 1 paso  · Borrador   TC-107 Notificaciones · Baja · 1 paso · Borrador
//   TC-104 Checkout · Alta · 4 pasos · Pasó (seleccionado al cargar)
//   Plan PL-0001 "Regresión Sprint 14": TC-101, TC-102, TC-104, TC-105 (11 pasos)
//   Gestor: Jira falso (FakeIssueTracker) en https://acme.atlassian.net, proyecto SHOP, conectado.

#include "FakeIssueTracker.h"
#include "MemoryRepositories.h"

#include "application/BugReportService.h"
#include "application/BugStore.h"
#include "application/PlanStore.h"
#include "application/RunController.h"
#include "application/RunHistoryStore.h"
#include "application/SettingsStore.h"
#include "application/TestCaseStore.h"

#include <memory>

namespace qaflow::testing {

struct AppFixture {
    std::shared_ptr<MemoryTestCaseRepository> repo = std::make_shared<MemoryTestCaseRepository>();
    std::shared_ptr<MemoryRunHistoryRepository> historyRepo = std::make_shared<MemoryRunHistoryRepository>();
    std::shared_ptr<MemoryRunSessionRepository> sessionRepo = std::make_shared<MemoryRunSessionRepository>();
    std::shared_ptr<MemorySettingsRepository> settingsRepo = std::make_shared<MemorySettingsRepository>();
    std::shared_ptr<MemorySecretStore> secrets = std::make_shared<MemorySecretStore>();
    std::shared_ptr<MemoryBugRepository> bugRepo = std::make_shared<MemoryBugRepository>();
    std::shared_ptr<FakeIssueTracker> tracker = std::make_shared<FakeIssueTracker>();

    TestCaseStore store{repo};
    RunHistoryStore history{historyRepo, store};
    RunController run{store, history, sessionRepo};
    PlanStore plans{repo, store, history};
    SettingsStore settings{settingsRepo, secrets};
    BugStore bugLedger{bugRepo};
    BugReportService bugs{tracker, store, run, settings, bugLedger};

    AppFixture() {
        store.load();
        history.load();
        plans.load();
        settings.load();
        bugLedger.load();
        settings.updateTracker([](TrackerSettings& s) { s.token = QStringLiteral("test-token"); s.connected = true; });
    }
};

} // namespace qaflow::testing
