#pragma once

// Sistema de requerimientos falso: responde con la bandeja, las fichas y el catálogo que se le den y
// guarda con qué ajustes se le preguntó, para probar RequirementSourceService, los ajustes de GESREQ y la
// importación de issues sin red.

#include "core/services/IRequirementSource.h"

#include <QMap>

namespace qaflow::testing {

class FakeRequirementSource : public IRequirementSource {
public:
    bool reachable = true;
    QList<ExternalRequirement> inbox;
    QMap<QString, RequirementDetail> details;           // fichas que tiene, por número de requerimiento
    QList<RequirementSystem> catalog;
    QList<RequirementSourceSettings> tested;            // ajustes de cada prueba de conexión
    QList<RequirementSourceSettings> catalogRequests;   // ajustes de cada consulta del catálogo
    int inboxReads = 0;

    void testConnection(const RequirementSourceSettings& s, std::function<void(const ConnectionResult&)> done) override {
        tested << s;
        if (reachable) done(ConnectionResult{true, QStringLiteral("Tester Uno, Ana · %1 requerimientos en la bandeja de control de calidad").arg(inbox.size()), {}});
        else done(ConnectionResult{false, {}, QStringLiteral("GESREQ rechazó el usuario o la contraseña")});
    }

    void fetchInbox(const RequirementSourceSettings&, std::function<void(const RequirementInboxResult&)> done) override {
        ++inboxReads;
        RequirementInboxResult r;
        r.ok = reachable;
        r.fetchedAt = QDateTime::currentDateTime();
        if (reachable) {
            r.requirements = inbox;
        } else {
            r.failure = RequirementSourceFailure::Credentials;
            r.error = QStringLiteral("GESREQ rechazó el usuario o la contraseña");
        }
        done(r);
    }

    void fetchSystems(const RequirementSourceSettings& s, std::function<void(const RequirementSystemsResult&)> done) override {
        catalogRequests << s;
        RequirementSystemsResult r;
        r.ok = reachable;
        if (reachable) {
            r.systems = catalog;
        } else {
            r.failure = RequirementSourceFailure::Credentials;
            r.error = QStringLiteral("GESREQ rechazó el usuario o la contraseña");
        }
        done(r);
    }

    void fetchDetail(const RequirementSourceSettings&, const QString& id, std::function<void(const RequirementDetailResult&)> done) override {
        RequirementDetailResult r;
        r.fetchedAt = QDateTime::currentDateTime();
        if (reachable && details.contains(id)) {
            r.ok = true;
            r.detail = details.value(id);
        } else {
            r.failure = RequirementSourceFailure::NotFound;
            r.error = QStringLiteral("El GESREQ falso no tiene la ficha %1").arg(id);
        }
        done(r);
    }
};

} // namespace qaflow::testing
