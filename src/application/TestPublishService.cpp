#include "TestPublishService.h"

#include "application/BugStore.h"
#include "application/IssueStore.h"
#include "application/RunHistoryStore.h"
#include "application/SettingsStore.h"
#include "application/TestCaseStore.h"

#include <QFileInfo>

namespace qaflow {

TestPublishService::TestPublishService(std::shared_ptr<ITestManagement> zephyr, TestCaseStore& cases, RunHistoryStore& history,
                                       SettingsStore& settings, BugStore& bugs, QObject* parent)
    : QObject(parent), m_zephyr(std::move(zephyr)), m_cases(cases), m_history(history), m_settings(settings), m_bugs(bugs) {}

QList<PublishDefect> TestPublishService::defectsOf(const PlanReport& report, const QString& caseId) const {
    // Los bugs de ese caso que salieron de este ciclo, con la misma regla con la que el informe los
    // enseña (`PlanReport::foundIn`): lo que se ve en la pantalla es lo que se sube.
    QList<PublishDefect> defects;
    for (const auto& bug : m_bugs.issues()) {
        if (bug.caseId != caseId || bug.key.trimmed().isEmpty()) continue;
        if (!PlanReport::foundIn(report.plan, bug)) continue;
        defects << PublishDefect{bug.key.trimmed(), bug.step};
    }
    return defects;
}

bool TestPublishService::enabled() const {
    const TrackerSettings& t = m_settings.tracker();
    return m_zephyr && t.zephyr && t.kind == TrackerKind::Jira;
}

QStringList TestPublishService::casesNeedingTest(const PlanReport& report) const {
    QStringList out;
    for (const auto& row : report.rows)
        if (row.executed && row.run.testKey.trimmed().isEmpty()) out << row.caseId;
    return out;
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
    const PlanRun& plan = report.plan;
    QStringList parts;
    // El requerimiento primero: en Zephyr los ciclos de un mismo control de calidad se buscan por él.
    if (const Issue* issue = m_issues && !plan.issueId.isEmpty() ? m_issues->find(plan.issueId) : nullptr)
        if (issue->isImported()) parts << tr("GREQ %1").arg(issue->requirement.data.id);
    if (plan.revision > 0) parts << tr("Rev. %1").arg(plan.revision);
    // Y el plan, que es lo que distingue los ciclos de una misma ronda entre sí.
    if (!plan.name.trimmed().isEmpty()) parts << plan.name.trimmed();
    // Una continuación repite plan, revisión y, casi siempre, día y ambiente: sin decir por dónde va la
    // cadena, su ciclo se llamaría igual que aquel al que continúa.
    if (const int depth = continuationDepth(plan); depth > 0) parts << tr("Cont. %1").arg(depth);
    if (plan.startedAt.isValid()) parts << plan.startedAt.toString(QStringLiteral("dd/MM/yyyy"));
    if (!plan.environment.trimmed().isEmpty()) parts << plan.environment.trimmed();
    return parts.isEmpty() ? plan.name : parts.join(QStringLiteral(" · "));
}

PublishRequest TestPublishService::requestFor(const PlanReport& report, bool update) const {
    PublishRequest req;
    if (update) req.cycleId = report.plan.zephyrCycleId.trimmed();
    req.cycleName = cycleName(report);
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
            req.description += tr("\nRequerimiento GREQ %1 · %2").arg(issue->requirement.data.id, issue->title);
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
        // El Test es de la ejecución: si este informe ya se publicó lo tiene; si no, se crea.
        pc.testKey = row.run.testKey.trimmed();
        pc.title = row.title;
        pc.verdict = row.run.verdict;
        pc.steps = row.run.steps;
        pc.defects = defectsOf(report, row.caseId);
        pc.durationSecs = row.run.durationSecs;
        if (const TestCase* c = m_cases.find(row.caseId)) {
            // Con lo que el caso dice hoy se crea el Test de la ejecución.
            pc.preconditions = c->preconditions;
            pc.design = c->steps;
            // Las evidencias son las de esa ejecución que sigan en disco, con su paso.
            for (const auto& shot : c->shotsOfRun(row.run.id))
                if (QFileInfo::exists(shot.path)) pc.attachments.append(PublishAttachment{shot.path, shot.step});
        }
        // Del caso que ya no está en el catálogo sólo queda la ejecución: sus pasos son el Test.
        if (pc.design.isEmpty())
            for (const auto& s : row.run.steps) pc.design.append(TestStep{s.action, s.data, s.expected});
        req.cases.append(pc);
    }
    return req;
}

QString TestPublishService::cycleUrl(const PlanReport& report) const {
    if (!report.plan.isPublished()) return {};
    return m_settings.tracker().zephyrCycleUrl(requestFor(report).cycleName);
}

void TestPublishService::testConnection(std::function<void(const ConnectionResult&)> done) {
    if (!m_zephyr) { done(ConnectionResult{false, {}, tr("Integración no disponible")}); return; }
    m_zephyr->testConnection(m_settings.tracker(), std::move(done));
}

void TestPublishService::publish(const PlanReport& report, std::function<void(const PublishResult&)> done) { send(report, false, std::move(done)); }

void TestPublishService::update(const PlanReport& report, std::function<void(const PublishResult&)> done) {
    if (!report.plan.isPublished()) {
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
    // Qué ejecución publica cada caso, para devolverle el Test que se le cree.
    QHash<QString, QString> runOfCase;
    for (const auto& row : report.rows) if (row.executed) runOfCase.insert(row.caseId, row.run.id);
    m_zephyr->publish(m_settings.tracker(), requestFor(report, update), [this, planRunId = report.plan.id, runOfCase, done = std::move(done)](const PublishResult& r) {
        // Los Tests creados son de las ejecuciones publicadas, y se guardan aunque el ciclo haya
        // fallado a medias: ya existen en Jira y un reintento debe reutilizarlos, no duplicarlos.
        QHash<QString, QString> byRun;
        for (auto it = r.createdTests.constBegin(); it != r.createdTests.constEnd(); ++it) {
            const QString runId = runOfCase.value(it.key());
            if (!runId.isEmpty() && !it.value().isEmpty()) byRun.insert(runId, it.value());
        }
        if (!byRun.isEmpty()) m_history.assignTestKeys(byRun);
        // El ciclo de plan se queda con el de Zephyr en el que acabaron sus resultados.
        if (r.ok) m_history.markPublished(planRunId, r.cycleId);
        done(r);
    });
}

} // namespace qaflow
