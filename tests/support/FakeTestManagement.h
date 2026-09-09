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
    /// Casos a los que se les ha pedido el Test desde el editor, y lo que se contesta.
    QList<PublishCase> testsCreated;
    CreateTestResult createResultToReturn;

    void testConnection(const TrackerSettings&, std::function<void(const ConnectionResult&)> done) override {
        if (reachable) done(ConnectionResult{true, QStringLiteral("API de Zephyr en /rest/zephyr/latest"), {}});
        else done(ConnectionResult{false, {}, QStringLiteral("Host not found")});
    }

    void publish(const TrackerSettings&, const PublishRequest& request, std::function<void(const PublishResult&)> done) override {
        published << request;
        PublishResult r = resultToReturn;
        if (!r.ok && r.error.isEmpty()) {
            r.ok = true;
            r.cycleId = QStringLiteral("77");
            r.executions = request.cases.size();
        }
        done(r);
    }

    void createTest(const TrackerSettings&, const PublishCase& c, std::function<void(const CreateTestResult&)> done) override {
        testsCreated << c;
        CreateTestResult r = createResultToReturn;
        if (!r.ok && r.error.isEmpty()) {
            r.ok = true;
            r.key = QStringLiteral("SHOP-77");
        }
        done(r);
    }
};

} // namespace qaflow::testing
