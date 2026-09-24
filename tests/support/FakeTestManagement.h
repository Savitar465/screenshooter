#pragma once

// Herramienta de gestión de pruebas falsa: guarda lo que se le pidió publicar, para probar
// TestPublishService sin red.

#include "core/services/ITestManagement.h"

namespace qaflow::testing {

class FakeTestManagement : public ITestManagement {
public:
    bool reachable = true;
    QList<PublishRequest> published;
    PublishResult resultToReturn;
    /// Ciclos que se borraron en Zephyr: actualizarlos devuelve `cycleMissing`.
    QStringList missingCycles;
    /// Ciclo nuevo que devuelve cada publicación que crea uno (el 77 si no se dice otro).
    QString nextCycleId = QStringLiteral("77");
    QList<PublishRequest> testsRequested;
    /// Tests que «crea» `createTests` para cada caso sin clave: TC-101 → SHOP-101 si no se dice otro.
    QHash<QString, QString> testsToCreate;

    void testConnection(const TrackerSettings&, std::function<void(const ConnectionResult&)> done) override {
        if (reachable) done(ConnectionResult{true, QStringLiteral("API de Zephyr en /rest/zephyr/latest"), {}});
        else done(ConnectionResult{false, {}, QStringLiteral("Host not found")});
    }

    void publish(const TrackerSettings&, const PublishRequest& request, std::function<void(const PublishResult&)> done) override {
        published << request;
        if (!request.cycleId.isEmpty() && missingCycles.contains(request.cycleId)) {
            PublishResult gone;
            gone.cycleMissing = true;
            gone.error = QStringLiteral("El ciclo %1 ya no existe en Zephyr").arg(request.cycleId);
            done(gone);
            return;
        }
        PublishResult r = resultToReturn;
        if (!r.ok && r.error.isEmpty()) {
            r.ok = true;
            r.cycleId = request.cycleId.isEmpty() ? nextCycleId : request.cycleId;
            r.executions = request.cases.size();
        }
        done(r);
    }

    void createTests(const TrackerSettings&, const PublishRequest& request, std::function<void(const PublishResult&)> done) override {
        testsRequested << request;
        PublishResult r;
        r.ok = true;
        for (const auto& c : request.cases) {
            if (!c.testKey.isEmpty()) continue;
            const QString key = testsToCreate.value(c.caseId, QStringLiteral("SHOP-%1").arg(c.caseId.section(QLatin1Char('-'), -1)));
            r.createdTests.insert(c.caseId, key);
            ++r.testsCreated;
        }
        done(r);
    }
};

} // namespace qaflow::testing
