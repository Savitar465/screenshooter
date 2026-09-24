#include "TestPublishService.h"

#include "application/BugStore.h"
#include "application/IssueStore.h"
#include "application/RunHistoryStore.h"
#include "application/SettingsStore.h"
#include "application/TestCaseStore.h"
#include "core/Text.h"

#include <QFileInfo>

#include <algorithm>

namespace qaflow {

TestPublishService::TestPublishService(std::shared_ptr<ITestManagement> zephyr, TestCaseStore& cases, RunHistoryStore& history,
                                       SettingsStore& settings, BugStore& bugs, QObject* parent)
    : QObject(parent), m_zephyr(std::move(zephyr)), m_cases(cases), m_history(history), m_settings(settings), m_bugs(bugs) {}

QList<PublishDefect> TestPublishService::defectsOf(const PlanReport& report, const QString& caseId, const QString& runId) const {
    // Los bugs de ese caso que salieron de este ciclo, con la misma regla con la que el informe los
    // enseña (`PlanReport::foundIn`): lo que se ve en la pantalla es lo que se sube.
    QList<PublishDefect> defects;
    for (const auto& bug : m_bugs.issues()) {
        if (bug.caseId != caseId || bug.key.trimmed().isEmpty()) continue;
        if (!PlanReport::foundIn(report.plan, bug)) continue;
        // El paso es el de la ejecución de la que salió el bug. Si fue otra (el caso se repitió en el
        // ciclo y se publica la última), el bug va sólo a la ejecución. Los que no anotaron su
        // ejecución conservan su paso, como antes.
        const bool otherRun = !bug.runId.trimmed().isEmpty() && bug.runId != runId;
        defects << PublishDefect{bug.key.trimmed(), otherRun ? 0 : bug.step};
    }
    return defects;
}

bool TestPublishService::enabled() const {
    const TrackerSettings& t = m_settings.tracker();
    return m_zephyr && t.zephyr && t.kind == TrackerKind::Jira;
}

QStringList TestPublishService::casesNeedingTest(const PlanReport& report) const {
    const Issue* issue = issueOf(report);
    QStringList out;
    for (const auto& row : report.rows) {
        if (!row.executed) continue;
        const QString key = issue ? issue->zephyr.tests.value(row.caseId, row.run.testKey) : row.run.testKey;
        if (key.trimmed().isEmpty()) out << row.caseId;
    }
    return out;
}

const Issue* TestPublishService::issueOf(const PlanReport& report) const {
    return m_issues && !report.plan.issueId.isEmpty() ? m_issues->find(report.plan.issueId) : nullptr;
}

QString TestPublishService::phaseOf(const PlanReport& report) const {
    if (!report.plan.environment.trimmed().isEmpty()) return report.plan.environment.trimmed();
    const Issue* issue = issueOf(report);
    if (!issue) return {};
    const QStringList phases = m_issues->phasesOf(*issue);
    if (const IssueRevision* round = report.plan.revision > 0 ? issue->revision(report.plan.revision) : nullptr)
        return qaflow::phaseOf(*round, phases);
    return phases.first();
}

QString TestPublishService::phaseCycleName(const Issue& issue, const QString& phase) const {
    // El requerimiento y la fase: es lo que se busca en Zephyr, y lo único que distingue un ciclo de
    // fase de otro. Un issue creado a mano no tiene número de GESREQ: va con su id y su título.
    const QString name = issue.isImported() ? tr("GREQ %1 · %2").arg(issue.requirement.data.id, phase.trimmed())
                                            : tr("%1 · %2 · %3").arg(issue.id, elideTitle(issue.title, 120), phase.trimmed());
    return elideTitle(name, PublishRequest::kMaxCycleField);
}

QString TestPublishService::testContextOf(const Issue& issue) const {
    return issue.isImported() ? tr("GREQ %1 · %2").arg(issue.requirement.data.id, elideTitle(issue.title, 80))
                              : tr("%1 · %2").arg(issue.id, elideTitle(issue.title, 80));
}

QStringList TestPublishService::casesWithoutTest(const QString& issueId, const QStringList& caseIds) const {
    const Issue* issue = m_issues ? m_issues->find(issueId) : nullptr;
    QStringList out;
    for (const auto& caseId : caseIds)
        if (!issue || issue->zephyr.tests.value(caseId).trimmed().isEmpty()) out << caseId;
    return out;
}

void TestPublishService::createTests(const QString& issueId, const QStringList& caseIds, std::function<void(const PublishResult&)> done) {
    const Issue* issue = m_issues ? m_issues->find(issueId) : nullptr;
    PublishResult refused;
    if (!enabled()) refused.error = tr("Activa Zephyr en Ajustes para crear los Tests");
    else if (!issue) refused.error = tr("El issue ya no existe");
    if (!refused.error.isEmpty()) { done(refused); return; }
    PublishRequest req;
    req.versionName = m_settings.tracker().zephyrVersion;
    req.testContext = testContextOf(*issue);
    for (const auto& caseId : casesWithoutTest(issueId, caseIds)) {
        const TestCase* c = m_cases.find(caseId);
        if (!c) continue;
        PublishCase pc;
        pc.caseId = caseId;
        pc.title = c->title;
        pc.preconditions = c->preconditions;
        pc.design = c->steps;
        req.cases.append(pc);
    }
    if (req.cases.isEmpty()) {
        PublishResult nothing;
        nothing.ok = true;
        done(nothing);
        return;
    }
    m_zephyr->createTests(m_settings.tracker(), req, [this, issueId, done = std::move(done)](const PublishResult& r) {
        // Lo creado se guarda aunque otros no salieran: ya existe en Jira y no hay que duplicarlo.
        QHash<QString, QString> created;
        for (auto it = r.createdTests.constBegin(); it != r.createdTests.constEnd(); ++it) created.insert(it.key(), it.value());
        if (m_issues) m_issues->noteZephyrTests(issueId, created);
        done(r);
    });
}

int TestPublishService::continuationDepth(const PlanRun& plan) const {
    int depth = 0;
    QString id = plan.continuesCycleId;
    // La cadena es finita (cada ciclo continúa a uno anterior), pero se acota por si un history.json
    // editado a mano la cerrara en círculo.
    while (!id.isEmpty() && depth < 100) {
        ++depth;
        const PlanRun* previous = m_history.findPlan(id);
        id = previous ? previous->continuesCycleId : QString();
    }
    return depth;
}

QString TestPublishService::cycleName(const PlanReport& report) const {
    if (const Issue* issue = issueOf(report)) {
        const QString phase = phaseOf(report);
        const QString stored = issue->zephyr.cycleNameOf(phase);
        return stored.isEmpty() ? phaseCycleName(*issue, phase) : stored;
    }
    return ownCycleName(report);
}

QString TestPublishService::ownCycleName(const PlanReport& report) const {
    const PlanRun& plan = report.plan;
    QStringList parts;
    int planPart = -1;
    // El requerimiento primero: en Zephyr los ciclos de un mismo control de calidad se buscan por él.
    if (const Issue* issue = m_issues && !plan.issueId.isEmpty() ? m_issues->find(plan.issueId) : nullptr)
        if (issue->isImported()) parts << tr("GREQ %1").arg(issue->requirement.data.id);
    if (plan.revision > 0) parts << tr("Rev. %1").arg(plan.revision);
    // Y el plan, que es lo que distingue los ciclos de una misma ronda entre sí.
    if (!plan.name.trimmed().isEmpty()) { planPart = parts.size(); parts << plan.name.trimmed(); }
    // Una continuación repite plan, revisión y, casi siempre, día y ambiente: sin decir por dónde va la
    // cadena, su ciclo se llamaría igual que aquel al que continúa.
    if (const int depth = continuationDepth(plan); depth > 0) parts << tr("Cont. %1").arg(depth);
    if (plan.startedAt.isValid()) parts << plan.startedAt.toString(QStringLiteral("dd/MM/yyyy"));
    if (!plan.environment.trimmed().isEmpty()) parts << plan.environment.trimmed();
    if (parts.isEmpty()) return elideTitle(plan.name, PublishRequest::kMaxCycleField);
    // El nombre del plan suele llevar el título del issue, que puede ser larguísimo: se acorta él
    // para que el ciclo quepa en Zephyr sin perder requerimiento, revisión, fecha ni ambiente.
    const QString sep = QStringLiteral(" · ");
    if (const int overflow = parts.join(sep).size() - PublishRequest::kMaxCycleField; overflow > 0 && planPart >= 0)
        parts[planPart] = elideTitle(parts[planPart], std::max(1, int(parts[planPart].size()) - overflow));
    return elideTitle(parts.join(sep), PublishRequest::kMaxCycleField);
}

PublishRequest TestPublishService::requestFor(const PlanReport& report, bool update) const {
    PublishRequest req;
    const Issue* owner = issueOf(report);
    // El ciclo de un issue va siempre al de su fase (si ya existe, se actualiza); uno suelto, al suyo
    // sólo cuando se actualiza.
    if (owner) req.cycleId = owner->zephyr.cycleOf(phaseOf(report));
    else if (update) req.cycleId = report.plan.zephyrCycleId.trimmed();
    req.cycleName = cycleName(report);
    if (owner) req.testContext = testContextOf(*owner);
    req.versionName = m_settings.tracker().zephyrVersion;
    req.environment = report.plan.environment.trimmed();
    req.startedAt = report.plan.startedAt;
    req.finishedAt = report.plan.finishedAt;
    req.description = QObject::tr("Publicado desde QAflow · %1 de %2 casos ejecutados · %3 % de éxito")
                          .arg(report.executed).arg(report.total()).arg(report.successRate());
    // De qué control de calidad son estos resultados y dónde se obtuvieron: quien abra el ciclo en
    // Zephyr lo ve sin tener que volver a QAflow.
    if (const Issue* issue = m_issues && !report.plan.issueId.isEmpty() ? m_issues->find(report.plan.issueId) : nullptr) {
        if (issue->isImported())
            req.description += tr("\nRequerimiento GREQ %1 · %2").arg(issue->requirement.data.id, elideTitle(issue->title, 80));
        if (report.plan.revision > 0) req.description += tr("\nRevisión %1 del control de calidad").arg(report.plan.revision);
    }
    if (!req.environment.isEmpty()) req.description += tr("\nAmbiente: %1").arg(req.environment);
    if (report.plan.isContinuation())
        req.description += tr("\nContinúa el ciclo %1: sólo sus casos fallados y bloqueados").arg(report.plan.continuesCycleId);

    for (const auto& row : report.rows) {
        if (!row.executed) continue;   // los pendientes no se publican: en Zephyr quedarían sin ejecutar
        PublishCase pc;
        pc.caseId = row.caseId;
        pc.runId = row.run.id;
        // El Test de un caso del issue es el del issue (el mismo en todas sus fases); a falta de él, el
        // que ya tenga la ejecución. En un ciclo suelto, el de la ejecución. Sin ninguno, se crea.
        pc.testKey = (owner ? owner->zephyr.tests.value(row.caseId, row.run.testKey) : row.run.testKey).trimmed();
        pc.title = row.title;
        pc.verdict = row.run.verdict;
        pc.steps = row.run.steps;
        pc.defects = defectsOf(report, row.caseId, row.run.id);
        pc.durationSecs = row.run.durationSecs;
        if (const TestCase* c = m_cases.find(row.caseId)) {
            // Con lo que el caso dice hoy se crea el Test de la ejecución.
            pc.preconditions = c->preconditions;
            pc.design = c->steps;
        }
        // Las evidencias son las de esa ejecución que sigan en disco, con su paso. Una continuación
        // lleva también las de los pasos que heredó: su Test los da por buenos, y la prueba es aquélla.
        for (const auto& shot : m_history.evidenceOf(row.run))
            if (QFileInfo::exists(shot.path)) pc.attachments.append(PublishAttachment{shot.path, shot.step});
        // Del caso que ya no está en el catálogo sólo queda la ejecución: sus pasos son el Test.
        if (pc.design.isEmpty())
            for (const auto& s : row.run.steps) pc.design.append(TestStep{s.action, s.data, s.expected});
        req.cases.append(pc);
    }
    return req;
}

QString TestPublishService::cycleUrl(const PlanReport& report) const {
    if (!report.plan.isPublished()) return {};
    // Publicado en el ciclo de su fase, se busca por el nombre de ése; publicado antes de que existieran
    // (en un ciclo propio), por el nombre que tenía entonces.
    if (const Issue* issue = issueOf(report)) {
        const QString phase = phaseOf(report);
        if (!issue->zephyr.cycleOf(phase).isEmpty() && issue->zephyr.cycleOf(phase) == report.plan.zephyrCycleId)
            return m_settings.tracker().zephyrCycleUrl(cycleName(report));
    }
    return m_settings.tracker().zephyrCycleUrl(ownCycleName(report));
}

void TestPublishService::testConnection(std::function<void(const ConnectionResult&)> done) {
    if (!m_zephyr) { done(ConnectionResult{false, {}, tr("Integración no disponible")}); return; }
    m_zephyr->testConnection(m_settings.tracker(), std::move(done));
}

void TestPublishService::publish(const PlanReport& report, std::function<void(const PublishResult&)> done) { send(report, false, std::move(done)); }

void TestPublishService::update(const PlanReport& report, std::function<void(const PublishResult&)> done) {
    if (!report.plan.isPublished() && !sharesPhaseCycle(report)) {
        PublishResult r;
        r.error = tr("Este informe no está publicado en Zephyr: publícalo primero");
        done(r);
        return;
    }
    send(report, true, std::move(done));
}

void TestPublishService::send(const PlanReport& report, bool update, std::function<void(const PublishResult&)> done) {
    if (!enabled()) {
        PublishResult r;
        r.error = tr("Activa Zephyr en Ajustes para publicar los ciclos");
        done(r);
        return;
    }
    const PublishRequest request = requestFor(report, update);
    const Issue* owner = issueOf(report);
    const QString issueId = owner ? owner->id : QString();
    const QString phase = owner ? phaseOf(report) : QString();
    // Qué ejecución publica cada caso, para devolverle su Test.
    QHash<QString, QString> runOfCase;
    for (const auto& row : report.rows) if (row.executed) runOfCase.insert(row.caseId, row.run.id);
    m_zephyr->publish(m_settings.tracker(), request, [this, report, update, request, issueId, phase, runOfCase,
                                                      done = std::move(done)](const PublishResult& r) mutable {
        // El ciclo de la fase se borró en Zephyr: se olvida y se publica en uno nuevo (una vez: sin
        // ciclo guardado, la siguiente petición ya lo crea).
        if (r.cycleMissing && !issueId.isEmpty() && !request.cycleId.isEmpty()) {
            m_issues->forgetZephyrCycle(issueId, phase);
            send(report, update, std::move(done));
            return;
        }
        // Los Tests con los que se publicó cada caso —los que ya tenía y los creados ahora— se guardan
        // aunque el ciclo haya fallado a medias: ya existen en Jira y un reintento debe reutilizarlos.
        QHash<QString, QString> testOfCase;
        for (const auto& c : request.cases) if (!c.testKey.trimmed().isEmpty()) testOfCase.insert(c.caseId, c.testKey.trimmed());
        for (auto it = r.createdTests.constBegin(); it != r.createdTests.constEnd(); ++it) testOfCase.insert(it.key(), it.value());
        if (!issueId.isEmpty()) m_issues->noteZephyrTests(issueId, testOfCase);
        QHash<QString, QString> byRun;
        for (auto it = testOfCase.constBegin(); it != testOfCase.constEnd(); ++it) {
            const QString runId = runOfCase.value(it.key());
            if (!runId.isEmpty() && !it.value().isEmpty()) byRun.insert(runId, it.value());
        }
        if (!byRun.isEmpty()) m_history.assignTestKeys(byRun);
        if (r.ok) {
            // El issue recuerda el ciclo de su fase, y el ciclo de plan, el de Zephyr en el que acabaron
            // sus resultados.
            if (!issueId.isEmpty()) m_issues->noteZephyrCycle(issueId, phase, r.cycleId, request.cycleName);
            m_history.markPublished(report.plan.id, r.cycleId);
        }
        done(r);
    });
}

} // namespace qaflow
