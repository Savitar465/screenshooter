// RequirementSourceService (application/RequirementSourceService.h): prueba la conexión con GESREQ con
// los ajustes del SettingsStore, deja en ellos si entró y ofrece los sistemas de la bandeja para vincular
// uno al proyecto.

#include "support/FakeRequirementSource.h"
#include "support/MemoryRepositories.h"

#include "application/RequirementSourceService.h"
#include "application/SettingsStore.h"

#include <QtTest>

using namespace qaflow;
using qaflow::testing::FakeRequirementSource;
using qaflow::testing::MemorySecretStore;
using qaflow::testing::MemorySettingsRepository;

namespace {
ExternalRequirement requirementOf(const QString& id, const QString& systemCode) {
    ExternalRequirement r;
    r.id = id;
    r.systemCode = systemCode;
    return r;
}

/// Ajustes de GESREQ ya rellenos, con la contraseña en un llavero en memoria.
struct Fixture {
    std::shared_ptr<FakeRequirementSource> source = std::make_shared<FakeRequirementSource>();
    SettingsStore settings{std::make_shared<MemorySettingsRepository>(), std::make_shared<MemorySecretStore>()};
    RequirementSourceService service{source, settings};

    Fixture() {
        settings.load();
        settings.updateRequirementSource([](RequirementSourceSettings& r) {
            r.url = QStringLiteral("http://gesreq.test:7401/greq");
            r.user = QStringLiteral("QAUSR0101");
            r.password = QStringLiteral("s3creta");
        });
    }
};
} // namespace

class RequirementSourceServiceTest : public QObject {
    Q_OBJECT
private slots:
    void testsTheConnectionWithTheStoredSettingsAndMarksItConnected() {
        Fixture f;
        ConnectionResult out;
        bool done = false;
        f.service.testConnection([&](const ConnectionResult& r) { out = r; done = true; });
        QVERIFY(done);
        QVERIFY2(out.ok, qPrintable(out.error));
        QCOMPARE(f.source->tested.size(), 1);
        QCOMPARE(f.source->tested.first().url, QStringLiteral("http://gesreq.test:7401/greq"));
        QCOMPARE(f.source->tested.first().password, QStringLiteral("s3creta"));   // la del llavero
        QVERIFY(f.settings.requirementSource().connected);
    }

    // Los sistemas de la bandeja son los candidatos a vincular a un proyecto: ordenados y sin repetir.
    void offersTheSystemsOfTheInbox() {
        Fixture f;
        f.source->inbox = {requirementOf(QStringLiteral("2025723"), QStringLiteral("SUMAOCE")),
                           requirementOf(QStringLiteral("2025719"), QStringLiteral("SEGRAN")),
                           requirementOf(QStringLiteral("2025724"), QStringLiteral("SUMAOCE")),
                           requirementOf(QStringLiteral("2026999"), QString())};
        QSignalSpy changed(&f.service, &RequirementSourceService::systemsChanged);
        f.service.testConnection([](const ConnectionResult&) {});
        QCOMPARE(f.service.systems(), QStringList({QStringLiteral("SEGRAN"), QStringLiteral("SUMAOCE")}));
        QCOMPARE(changed.count(), 1);
        f.service.testConnection([](const ConnectionResult&) {});
        QCOMPARE(changed.count(), 1);   // la misma bandeja no vuelve a avisar
    }

    void fetchesTheCatalogOfSystemsWithTheStoredSettings() {
        Fixture f;
        f.source->catalog = {RequirementSystem{QStringLiteral("SEGRAN"), QStringLiteral("GESTIÓN DE RIESGOS")},
                             RequirementSystem{QStringLiteral("SUMA TRANSITO"), QStringLiteral("TRANSITOS")}};
        RequirementSystemsResult out;
        f.service.fetchSystems([&](const RequirementSystemsResult& r) { out = r; });
        QVERIFY2(out.ok, qPrintable(out.error));
        QCOMPARE(out.systems.size(), 2);
        QCOMPARE(f.source->catalogRequests.size(), 1);
        QCOMPARE(f.source->catalogRequests.first().user, QStringLiteral("QAUSR0101"));
        QCOMPARE(f.source->catalogRequests.first().password, QStringLiteral("s3creta"));
    }

    void aRejectedConnectionIsMarkedAsDisconnected() {
        Fixture f;
        f.settings.updateRequirementSource([](RequirementSourceSettings& r) { r.connected = true; });
        f.source->reachable = false;
        ConnectionResult out;
        f.service.testConnection([&](const ConnectionResult& r) { out = r; });
        QVERIFY(!out.ok);
        QVERIFY(!out.error.isEmpty());
        QVERIFY(!f.settings.requirementSource().connected);
        QVERIFY(f.service.systems().isEmpty());
    }

    // ---- Registro del resultado ------------------------------------------------------------------
    void theResultIsRegisteredWithTheStoredSettings() {
        Fixture f;
        QVERIFY(f.service.canRegisterResult());
        RequirementRegistration registration;
        registration.requirementId = QStringLiteral("2026997");
        registration.result = QStringLiteral("Observado");
        registration.comment = QStringLiteral("3 observaciones de funcionamiento");
        registration.attachmentPath = QStringLiteral("/tmp/acta.docx");

        RequirementRegistrationResult out;
        f.service.registerResult(registration, [&out](const RequirementRegistrationResult& r) { out = r; });
        QVERIFY(out.ok);
        QCOMPARE(f.source->registrations.size(), 1);
        QCOMPARE(f.source->registrations.first().requirementId, QStringLiteral("2026997"));
        QCOMPARE(f.source->registrations.first().result, QStringLiteral("Observado"));
        QCOMPARE(f.source->registrations.first().attachmentPath, QStringLiteral("/tmp/acta.docx"));
        QCOMPARE(f.source->tested.last().user, QStringLiteral("QAUSR0101"));
    }

    void aRegistrationThatIsCutOffIsUnconfirmedAndOneWithoutConnectorIsNotEvenSent() {
        Fixture f;
        f.source->registrationCutOff = true;
        RequirementRegistrationResult out;
        f.service.registerResult(RequirementRegistration{QStringLiteral("2026997"), QStringLiteral("Conforme"), {}, {}},
                                 [&out](const RequirementRegistrationResult& r) { out = r; });
        QVERIFY(!out.ok);
        QVERIFY(out.uncertain);
        QVERIFY(out.retryable());

        // Un conector que no sabe registrar (o sin conexión configurada) ni lo intenta.
        f.source->registersResults = false;
        QVERIFY(!f.service.canRegisterResult());
        f.service.registerResult(RequirementRegistration{QStringLiteral("2026997"), QStringLiteral("Conforme"), {}, {}},
                                 [&out](const RequirementRegistrationResult& r) { out = r; });
        QVERIFY(!out.ok);
        QVERIFY(!out.uncertain);
        QCOMPARE(f.source->registrations.size(), 1);   // sólo el primero llegó al conector
    }
};

QTEST_APPLESS_MAIN(RequirementSourceServiceTest)
#include "test_requirement_source_service.moc"
