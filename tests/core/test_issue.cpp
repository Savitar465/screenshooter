// Issue (core/models/Issue.h): estados, revisiones y su resultado, prioridad a partir del
// requerimiento, qué cambió entre dos lecturas del mismo requerimiento y filtro de la lista.
// También IssueProgress (core/models/IssueProgress.h): cómo va el control de calidad de un issue.

#include "core/models/Issue.h"
#include "core/models/IssueProgress.h"

#include <QtTest>

#include <algorithm>

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

    // ---- Bugs verificados en un reintento --------------------------------------------------------
    void aBugIsVerifiedWhenALaterRunPassesItsStep() {
        const QDateTime found = QDateTime::currentDateTime().addDays(-1);
        IssueLink bug;
        bug.key = QStringLiteral("SHOP-1");
        bug.caseId = QStringLiteral("TC-1");
        bug.runId = QStringLiteral("R-1");
        bug.step = 2;
        bug.createdAt = found;

        auto run = [](const QString& id, const QString& caseId, const QDateTime& at, QList<StepResult> results, bool inheritedFirst = false) {
            RunRecord r;
            r.id = id;
            r.caseId = caseId;
            r.startedAt = at;
            r.finishedAt = at.addSecs(60);
            for (const auto result : results) {
                RunRecordStep step;
                step.result = result;
                r.steps << step;
            }
            if (inheritedFirst && !r.steps.isEmpty()) r.steps.first().inherited = true;
            r.verdict = std::all_of(results.cbegin(), results.cend(), [](StepResult x) { return x == StepResult::Pass; })
                            ? Verdict::Superado : Verdict::Fallido;
            return r;
        };
        const RunRecord failing = run(QStringLiteral("R-1"), QStringLiteral("TC-1"), found.addSecs(-120), {StepResult::Pass, StepResult::Fail});
        QVERIFY(!retestPassed(bug, {failing}));   // la ejecución en la que se encontró no cuenta

        const RunRecord retest = run(QStringLiteral("R-2"), QStringLiteral("TC-1"), found.addSecs(3600), {StepResult::Pass, StepResult::Pass});
        QVERIFY(retestPassed(bug, {failing, retest}));

        // Otro caso, o un reintento que no llegó a su paso, no lo verifican.
        QVERIFY(!retestPassed(bug, {run(QStringLiteral("R-3"), QStringLiteral("TC-2"), found.addSecs(3600), {StepResult::Pass, StepResult::Pass})}));
        QVERIFY(!retestPassed(bug, {run(QStringLiteral("R-4"), QStringLiteral("TC-1"), found.addSecs(3600), {StepResult::Pass})}));

        // Manda el último reintento: si volvió a fallar, no está corregido.
        const RunRecord again = run(QStringLiteral("R-5"), QStringLiteral("TC-1"), found.addSecs(7200), {StepResult::Pass, StepResult::Fail});
        QVERIFY(!retestPassed(bug, {failing, retest, again}));

        // Un bug del caso entero pide el caso superado.
        IssueLink wholeCase = bug;
        wholeCase.step = 0;
        QVERIFY(retestPassed(wholeCase, {retest}));
        QVERIFY(!retestPassed(wholeCase, {again}));
    }

    void anInheritedStepDoesNotVerifyABug() {
        IssueLink bug;
        bug.caseId = QStringLiteral("TC-1");
        bug.step = 1;
        bug.createdAt = QDateTime::currentDateTime().addDays(-1);
        RunRecord continued;
        continued.id = QStringLiteral("R-2");
        continued.caseId = QStringLiteral("TC-1");
        continued.startedAt = QDateTime::currentDateTime();
        RunRecordStep step;
        step.result = StepResult::Pass;
        step.inherited = true;   // viene de la ejecución que se continuaba: no se volvió a probar
        continued.steps << step;
        QVERIFY(!retestPassed(bug, {continued}));
    }

    // ---- Fases del control de calidad ------------------------------------------------------------
    void phasesAreCleanedAndDefaultToQaThenPre() {
        QCOMPARE(defaultQaPhases(), (QStringList{QStringLiteral("QA"), QStringLiteral("PRE")}));
        QCOMPARE(normalizedQaPhases({}), defaultQaPhases());
        QCOMPARE(normalizedQaPhases({QStringLiteral("  "), QString()}), defaultQaPhases());
        QCOMPARE(normalizedQaPhases({QStringLiteral(" QA "), QStringLiteral("qa"), QStringLiteral("UAT"), QStringLiteral("PRE")}),
                 (QStringList{QStringLiteral("QA"), QStringLiteral("UAT"), QStringLiteral("PRE")}));
        const QStringList phases = defaultQaPhases();
        QVERIFY(!isFinalPhase(QStringLiteral("QA"), phases));
        QVERIFY(isFinalPhase(QStringLiteral("pre"), phases));
        QVERIFY(isFinalPhase(QStringLiteral("Staging"), phases));   // una que ya no está: no hay a dónde avanzar
    }

    void theNextRoundStaysInItsPhaseUntilItIsApproved() {
        const QStringList phases = defaultQaPhases();
        Issue issue;
        QCOMPARE(nextPhase(issue, phases), QStringLiteral("QA"));   // sin rondas: la primera

        IssueRevision qa;
        qa.number = 1;
        qa.phase = QStringLiteral("QA");
        qa.startedAt = QDateTime::currentDateTime();
        issue.revisions << qa;
        QCOMPARE(nextPhase(issue, phases), QStringLiteral("QA"));   // abierta: la misma

        issue.revisions.last().closedAt = QDateTime::currentDateTime();
        issue.revisions.last().outcome = QaOutcome::Observado;
        QCOMPARE(nextPhase(issue, phases), QStringLiteral("QA"));   // observada: se vuelve a probar en QA
        QVERIFY(!closesRequirement(issue.revisions.last(), phases));

        issue.revisions.last().outcome = QaOutcome::Conforme;
        QCOMPARE(nextPhase(issue, phases), QStringLiteral("PRE"));  // QA aprobada: toca PRE
        QVERIFY(!closesRequirement(issue.revisions.last(), phases));
        QCOMPARE(outcomeLabel(QaOutcome::Conforme, QStringLiteral("QA"), phases), QStringLiteral("Aprobada en QA"));

        IssueRevision pre = qa;
        pre.number = 2;
        pre.phase = QStringLiteral("PRE");
        pre.outcome = QaOutcome::Conforme;
        pre.closedAt = QDateTime::currentDateTime();
        issue.revisions << pre;
        QVERIFY(closesRequirement(issue.revisions.last(), phases));  // conforme en la última: el OK final
        QCOMPARE(nextPhase(issue, phases), QStringLiteral("PRE"));
        QCOMPARE(outcomeLabel(QaOutcome::Conforme, QStringLiteral("PRE"), phases), QStringLiteral("Conforme"));
    }

    void roundsFromBeforeThePhasesKeepWhatTheyMeant() {
        const QStringList phases = defaultQaPhases();
        IssueRevision old;
        old.startedAt = QDateTime::currentDateTime();
        QCOMPARE(phaseOf(old, phases), QStringLiteral("QA"));        // abierta: la primera
        old.closedAt = QDateTime::currentDateTime();
        old.outcome = QaOutcome::Observado;
        QCOMPARE(phaseOf(old, phases), QStringLiteral("QA"));
        // Antes, Conforme cerraba el requerimiento: sigue cerrándolo.
        old.outcome = QaOutcome::Conforme;
        QCOMPARE(phaseOf(old, phases), QStringLiteral("PRE"));
        QVERIFY(closesRequirement(old, phases));
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

    // Un requerimiento buscado por su número llega como ficha: se lleva a lo que sería su fila de la
    // bandeja, con el principio del alcance como descripción corta si la ficha no la trae.
    void aRequirementDetailBecomesAnInboxRow() {
        RequirementDetail d;
        d.id = QStringLiteral("2025800");
        d.systemCode = QStringLiteral("SUMA  TRANSITO");
        d.description = QStringLiteral("Alta de cupones de descuento\nCon su vigencia y su tope.");
        d.priority = QStringLiteral("ALTA");
        d.state = QStringLiteral("CONTROL FUNCIONAL");
        d.requester = QStringLiteral("Ana Pérez");
        d.url = QStringLiteral("http://gesreq.test/greq/publico.do?id=2025800&bandera=1");
        const ExternalRequirement r = requirementFromDetail(d);
        QCOMPARE(r.id, d.id);
        QCOMPARE(r.systemCode, QStringLiteral("SUMA TRANSITO"));
        QCOMPARE(r.summary, QStringLiteral("Alta de cupones de descuento"));
        QCOMPARE(r.states, QStringList{QStringLiteral("CONTROL FUNCIONAL")});
        QCOMPARE(r.priority, d.priority);
        QCOMPARE(r.requester, d.requester);
        QCOMPARE(r.detailUrl, d.url);

        d.fields << RequirementField{QStringLiteral("Descripción Corta"), QStringLiteral("Cupones")};
        QCOMPARE(requirementFromDetail(d).summary, QStringLiteral("Cupones"));
    }

    // El issue de un requerimiento es el de su número en su conexión, con o sin barra final.
    void anIssueTestsTheRequirementOfItsConnection() {
        Issue issue;
        QVERIFY(!issue.testsRequirement(QStringLiteral("http://gesreq.test/greq"), QString()));
        issue.requirement.connection = QStringLiteral("http://gesreq.test/greq");
        issue.requirement.data.id = QStringLiteral("2025175");
        QVERIFY(issue.testsRequirement(QStringLiteral("HTTP://gesreq.test/greq/"), QStringLiteral("2025175")));
        QVERIFY(!issue.testsRequirement(QStringLiteral("http://gesreq.test/greq"), QStringLiteral("2025176")));
        QVERIFY(!issue.testsRequirement(QStringLiteral("http://otro/greq"), QStringLiteral("2025175")));
    }

    // Un issue finalizado lo está desde que se cerró su última revisión; a mano, desde su último cambio.
    void anIssueIsFinishedWhenItsLastRevisionClosed() {
        Issue issue;
        issue.updatedAt = QDateTime(QDate(2026, 9, 20), QTime(10, 0));
        QVERIFY(!issue.finishedAt().isValid());
        issue.state = IssueState::Done;
        QCOMPARE(issue.finishedAt(), issue.updatedAt);
        IssueRevision r;
        r.closedAt = QDateTime(QDate(2026, 9, 1), QTime(9, 0));
        issue.revisions << r;
        QCOMPARE(issue.finishedAt(), r.closedAt);
    }
};

QTEST_APPLESS_MAIN(IssueTest)
#include "test_issue.moc"
