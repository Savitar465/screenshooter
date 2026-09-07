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

#include "MemoryRepositories.h"

#include "application/PlanStore.h"
#include "application/RunController.h"
#include "application/RunHistoryStore.h"
#include "application/TestCaseStore.h"

#include <memory>

namespace qaflow::testing {

struct AppFixture {
    std::shared_ptr<MemoryTestCaseRepository> repo = std::make_shared<MemoryTestCaseRepository>();
    std::shared_ptr<MemoryRunHistoryRepository> historyRepo = std::make_shared<MemoryRunHistoryRepository>();
    std::shared_ptr<MemoryRunSessionRepository> sessionRepo = std::make_shared<MemoryRunSessionRepository>();

    TestCaseStore store{repo};
    RunHistoryStore history{historyRepo, store};
    RunController run{store, history, sessionRepo};
    PlanStore plans{repo, store, history};

    AppFixture() {
        store.load();
        history.load();
        plans.load();
    }
};

} // namespace qaflow::testing
