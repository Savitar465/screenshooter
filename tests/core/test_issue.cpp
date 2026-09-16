// Issue (core/models/Issue.h): estados, revisiones y su resultado, prioridad a partir del
// requerimiento, qué cambió entre dos lecturas del mismo requerimiento y filtro de la lista.
// También IssueProgress (core/models/IssueProgress.h): cómo va el control de calidad de un issue.

#include "core/models/Issue.h"
#include "core/models/IssueProgress.h"

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

    void outcomesHaveCanonicalValuesAndLabels() {
        for (const auto o : {QaOutcome::Pendiente, QaOutcome::Conforme, QaOutcome::Observado})
            QVERIFY(qaOutcomeFromString(toString(o)) == o);
        QCOMPARE(toString(QaOutcome::Observado), QStringLiteral("Observado"));
        QVERIFY(qaOutcomeFromString(QStringLiteral("conforme")) == QaOutcome::Conforme);
        QVERIFY(qaOutcomeFromString(QStringLiteral("lo que sea")) == QaOutcome::Pendiente);
    }

    void revisionsGoFromTheOpenOneToTheLastResult() {
        Issue issue;
        QVERIFY(!issue.currentRevision());
        QVERIFY(!issue.lastClosedRevision());
        QVERIFY(issue.lastOutcome() == QaOutcome::Pendiente);

        IssueRevision first;
        first.number = 1;
        first.startedAt = QDateTime::currentDateTime();
        issue.revisions << first;
        QVERIFY(issue.currentRevision());
        QCOMPARE(issue.currentRevision()->number, 1);
        QVERIFY(!issue.lastClosedRevision());

        issue.revisions.last().closedAt = QDateTime::currentDateTime();
        issue.revisions.last().outcome = QaOutcome::Observado;
        QVERIFY(!issue.currentRevision());                          // cerrada: ya no hay ninguna en curso
        QVERIFY(issue.lastOutcome() == QaOutcome::Observado);

        IssueRevision second;
        second.number = 2;
        second.startedAt = QDateTime::currentDateTime();
        issue.revisions << second;
        QVERIFY(issue.currentRevision() && issue.currentRevision()->number == 2);
        QVERIFY(issue.lastClosedRevision()->number == 1);           // la anterior sigue siendo la última cerrada
        QVERIFY(issue.lastOutcome() == QaOutcome::Observado);
    }

    void progressCountsTheLastRunOfEachCaseAndProposesTheOutcome() {
        // Los casos del issue son los de sus planes; aquí llegan ya resueltos.
        const QStringList caseIds{QStringLiteral("TC-1"), QStringLiteral("TC-2"), QStringLiteral("TC-3"), QStringLiteral("TC-9")};
        QList<TestCase> cases;
        for (const auto& id : {QStringLiteral("TC-1"), QStringLiteral("TC-2"), QStringLiteral("TC-3")}) {
            TestCase c;
            c.id = id;
            cases << c;
        }
        const QDateTime now = QDateTime::currentDateTime();
        auto run = [&now](const QString& id, const QString& caseId, Verdict v, int minutesAgo) {
            RunRecord r;
            r.id = id;
            r.caseId = caseId;
            r.verdict = v;
            r.startedAt = now.addSecs(-60 * minutesAgo);
            r.finishedAt = r.startedAt.addSecs(120);
            return r;
        };

        QList<RunRecord> runs{run(QStringLiteral("R-1"), QStringLiteral("TC-1"), Verdict::Fallido, 30),
                              run(QStringLiteral("R-2"), QStringLiteral("TC-1"), Verdict::Superado, 5),   // repetido: manda el último
                              run(QStringLiteral("R-3"), QStringLiteral("TC-2"), Verdict::Superado, 20)};
        IssueProgress p = issueProgress(caseIds, cases, runs, {});
        QCOMPARE(p.cases, 3);
        QCOMPARE(p.missingCases, 1);          // TC-9 ya no existe
        QCOMPARE(p.executed, 2);
        QCOMPARE(p.passed, 2);
        QCOMPARE(p.notRun(), 1);
        QVERIFY(p.suggested == QaOutcome::Pendiente);   // falta ejecutar TC-3
        QVERIFY(p.blockers.contains(QStringLiteral("1 caso sin ejecutar")));

        runs << run(QStringLiteral("R-4"), QStringLiteral("TC-3"), Verdict::Superado, 2);
        p = issueProgress(caseIds, cases, runs, {});
        QCOMPARE(p.executed, 3);
        QVERIFY(p.suggested == QaOutcome::Conforme);
        QVERIFY(p.blockers.isEmpty());
        QVERIFY(p.canClose());

        // Un bug sin resolver de uno de sus casos deja el requerimiento observado.
        IssueLink bug;
        bug.key = QStringLiteral("SHOP-99");
        bug.caseId = QStringLiteral("TC-2");
        p = issueProgress(caseIds, cases, runs, {bug});
        QCOMPARE(p.bugs, 1);
        QCOMPARE(p.openBugs, 1);
        QVERIFY(p.suggested == QaOutcome::Observado);
        QVERIFY(p.blockers.contains(QStringLiteral("1 bug abierto")));

        bug.resolved = true;
        QVERIFY(issueProgress(caseIds, cases, runs, {bug}).suggested == QaOutcome::Conforme);
    }

    void progressOnlyCountsTheRunsOfTheOpenRevision() {
        const QStringList caseIds{QStringLiteral("TC-1")};
        TestCase c;
        c.id = QStringLiteral("TC-1");
        const QDateTime revisionStart = QDateTime::currentDateTime().addSecs(-3600);
        RunRecord old;
        old.id = QStringLiteral("R-1");
        old.caseId = QStringLiteral("TC-1");
        old.verdict = Verdict::Fallido;
        old.startedAt = revisionStart.addSecs(-7200);
        old.finishedAt = old.startedAt.addSecs(60);

        // La ejecución de la ronda anterior no cuenta: esta revisión todavía no ha probado nada.
        IssueProgress p = issueProgress(caseIds, {c}, {old}, {}, revisionStart);
        QCOMPARE(p.executed, 0);
        QVERIFY(!p.canClose());

        RunRecord fresh = old;
        fresh.id = QStringLiteral("R-2");
        fresh.verdict = Verdict::Superado;
        fresh.startedAt = revisionStart.addSecs(600);
        fresh.finishedAt = fresh.startedAt.addSecs(60);
        p = issueProgress(caseIds, {c}, {old, fresh}, {}, revisionStart);
        QCOMPARE(p.executed, 1);
        QCOMPARE(p.passed, 1);
        QVERIFY(p.suggested == QaOutcome::Conforme);
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
