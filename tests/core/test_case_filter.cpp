// CaseFilter (core/models/CaseFilter.h): criterios de la lista de casos.

#include "core/models/CaseFilter.h"

#include <QtTest>

using namespace qaflow;

namespace {
TestCase sampleCase() {
    TestCase c;
    c.id = QStringLiteral("TC-7"); c.title = QStringLiteral("Pago con tarjeta"); c.suite = QStringLiteral("Checkout");
    c.priority = Priority::Alta; c.status = CaseStatus::Listo;
    c.tags = {QStringLiteral("smoke")}; c.component = QStringLiteral("Carrito"); c.jiraKey = QStringLiteral("SHOP-12");
    c.lastRun = LastRun{RunOutcome::Failed, QDateTime::currentDateTime()};
    return c;
}
} // namespace

class CaseFilterTest : public QObject {
    Q_OBJECT
private slots:
    void emptyFilterMatchesEverything() {
        CaseFilter f;
        QVERIFY(f.isEmpty());
        QVERIFY(f.matches(sampleCase()));
        QVERIFY(f.matches(TestCase{}));
    }

    void textSearchIsCaseInsensitiveAcrossFields() {
        const TestCase c = sampleCase();
        CaseFilter f;
        for (const auto& q : {"SMOKE", "carrito", "shop-12", "tc-7", "tarjeta", "checkout"}) {
            f.text = QLatin1String(q);
            QVERIFY2(f.matches(c), q);
        }
        f.text = QStringLiteral("perfil");
        QVERIFY(!f.matches(c));
        QVERIFY(!f.isEmpty());
    }

    void suiteStatusPriorityAndOutcomeFilterIndependently() {
        const TestCase c = sampleCase();
        CaseFilter f;
        f.suite = QStringLiteral("Perfil");     QVERIFY(!f.matches(c));
        f.suite = QStringLiteral("Checkout");   QVERIFY(f.matches(c));
        f.status = CaseStatus::Borrador;        QVERIFY(!f.matches(c));
        f.status = CaseStatus::Listo;           QVERIFY(f.matches(c));
        f.priority = Priority::Baja;            QVERIFY(!f.matches(c));
        f.priority = Priority::Alta;            QVERIFY(f.matches(c));
        f.outcome = RunOutcome::None;           QVERIFY(!f.matches(c));   // "sin ejecutar"
        f.outcome = RunOutcome::Failed;         QVERIFY(f.matches(c));
    }
};

QTEST_APPLESS_MAIN(CaseFilterTest)
#include "test_case_filter.moc"
