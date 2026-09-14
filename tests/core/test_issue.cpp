// Issue (core/models/Issue.h): estados, prioridad a partir del requerimiento, qué cambió entre dos
// lecturas del mismo requerimiento y filtro de la lista.

#include "core/models/Issue.h"

#include <QtTest>

using namespace qaflow;

namespace {
ExternalRequirement requirement() {
    ExternalRequirement r;
    r.id = QStringLiteral("2025175");
    r.system = QStringLiteral("SUMA TRANSITO-TRANSITOS");
    r.systemCode = QStringLiteral("SUMA TRANSITO");
    r.systemName = QStringLiteral("TRANSITOS");
    r.summary = QStringLiteral("Desarrollo complementario del laboratorio");
    r.priority = QStringLiteral("ALTA");
    r.states = {QStringLiteral("CONTROL CALIDAD ASIGNADO")};
    r.assignedFrom = QDate(2025, 8, 11);
    r.assignedUntil = QDate(2026, 1, 27);
    r.requester = QStringLiteral("PÉREZ GÓMEZ ANA");
    r.requestingUnit = QStringLiteral("GNN");
    return r;
}
} // namespace

class IssueTest : public QObject {
    Q_OBJECT
private slots:
    void statesHaveCanonicalValuesAndLabels() {
        for (const auto s : {IssueState::Pending, IssueState::Preparing, IssueState::Testing, IssueState::Done})
            QVERIFY(issueStateFromString(toString(s)) == s);
        QCOMPARE(toString(IssueState::Preparing), QStringLiteral("En preparación"));
        QVERIFY(issueStateFromString(QStringLiteral("en pruebas")) == IssueState::Testing);
        QVERIFY(issueStateFromString(QStringLiteral("desconocido")) == IssueState::Pending);
        QCOMPARE(label(IssueState::Done), QStringLiteral("Finalizado"));
    }

    void priorityComesFromWhatTheSystemWrites() {
        QVERIFY(priorityFromRequirement(QStringLiteral("ALTA")) == Priority::Alta);
        QVERIFY(priorityFromRequirement(QStringLiteral(" alta ")) == Priority::Alta);
        QVERIFY(priorityFromRequirement(QStringLiteral("BAJA")) == Priority::Baja);
        QVERIFY(priorityFromRequirement(QStringLiteral("MEDIA")) == Priority::Media);
        QVERIFY(priorityFromRequirement(QString()) == Priority::Media);
    }

    void aNewReadOfTheSameRequirementSaysWhatChanged() {
        const ExternalRequirement before = requirement();
        ExternalRequirement after = before;
        QVERIFY(diffRequirement(before, after).isEmpty());
        after.states = {QStringLiteral("CONTROL DE CALIDAD OBSERVADO"), QStringLiteral("CONTROL FUNCIONAL")};
        after.assignedUntil = QDate(2026, 2, 15);
        after.summary = before.summary + QStringLiteral("   ");   // sólo espacios: no es un cambio
        const QList<RequirementChange> changes = diffRequirement(before, after);
        QCOMPARE(changes.size(), 2);
        QCOMPARE(changes[0].field, QStringLiteral("states"));
        QCOMPARE(changes[0].before, QStringLiteral("CONTROL CALIDAD ASIGNADO"));
        QCOMPARE(changes[0].after, QStringLiteral("CONTROL DE CALIDAD OBSERVADO + CONTROL FUNCIONAL"));
        QCOMPARE(changes[1].field, QStringLiteral("assignedUntil"));
        QCOMPARE(changes[1].before, QStringLiteral("27/01/2026"));
        QCOMPARE(changes[1].after, QStringLiteral("15/02/2026"));
        QCOMPARE(requirementFieldLabel(QStringLiteral("states")), QStringLiteral("Estado"));
        QCOMPARE(requirementFieldLabel(QStringLiteral("assignedUntil")), QStringLiteral("Asignado hasta"));
    }

    // De cada campo cuenta lo que se revisó por última vez y lo último que se leyó; si vuelve a como estaba, no hay cambio.
    void pendingChangesKeepTheOldestBeforeAndVanishWhenReverted() {
        const QList<RequirementChange> first{{QStringLiteral("states"), QStringLiteral("A"), QStringLiteral("B")}};
        QList<RequirementChange> merged = mergeChanges(first, {{QStringLiteral("states"), QStringLiteral("B"), QStringLiteral("C")},
                                                              {QStringLiteral("priority"), QStringLiteral("ALTA"), QStringLiteral("MEDIA")}});
        QCOMPARE(merged.size(), 2);
        QCOMPARE(merged[0].before, QStringLiteral("A"));
        QCOMPARE(merged[0].after, QStringLiteral("C"));
        merged = mergeChanges(merged, {{QStringLiteral("states"), QStringLiteral("C"), QStringLiteral("A")}});
        QCOMPARE(merged.size(), 1);
        QCOMPARE(merged[0].field, QStringLiteral("priority"));
    }

    void theFilterAlsoSearchesWhatWasImported() {
        Issue issue;
        issue.id = QStringLiteral("IS-0001");
        issue.title = QStringLiteral("Pruebas del laboratorio");
        issue.requirement.data = requirement();
        issue.state = IssueState::Testing;
        issue.priority = Priority::Alta;

        IssueFilter filter;
        QVERIFY(filter.isEmpty());
        QVERIFY(filter.matches(issue));
        filter.text = QStringLiteral("2025175 transito");
        QVERIFY(filter.matches(issue));
        filter.text = QStringLiteral("pérez");
        QVERIFY(filter.matches(issue));
        filter.text = QStringLiteral("observado");
        QVERIFY(!filter.matches(issue));

        filter.text.clear();
        filter.state = IssueState::Testing;
        QVERIFY(filter.matches(issue));
        filter.state = IssueState::Done;
        QVERIFY(!filter.matches(issue));

        filter.state.reset();
        filter.published = false;
        QVERIFY(filter.matches(issue));
        issue.publication.key = QStringLiteral("SHOP-12");
        QVERIFY(!filter.matches(issue));
        filter.published = true;
        QVERIFY(filter.matches(issue));
        filter.priority = Priority::Baja;
        QVERIFY(!filter.matches(issue));
    }
};

QTEST_APPLESS_MAIN(IssueTest)
#include "test_issue.moc"
