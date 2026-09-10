#include "TestPublishService.h"

#include "application/RunHistoryStore.h"
#include "application/SettingsStore.h"
#include "application/TestCaseStore.h"

#include <QFileInfo>

namespace qaflow {

TestPublishService::TestPublishService(std::shared_ptr<ITestManagement> zephyr, TestCaseStore& cases, RunHistoryStore& history,
                                       SettingsStore& settings, QObject* parent)
    : QObject(parent), m_zephyr(std::move(zephyr)), m_cases(cases), m_history(history), m_settings(settings) {}

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

PublishRequest TestPublishService::requestFor(const PlanReport& report, bool update) const {
    PublishRequest req;
    if (update) req.cycleId = report.plan.zephyrCycleId.trimmed();
    const QString started = report.plan.startedAt.toString(QStringLiteral("dd/MM/yyyy"));
    req.cycleName = started.isEmpty() ? report.plan.name : QStringLiteral("%1 · %2").arg(report.plan.name, started);
    req.versionName = m_settings.tracker().zephyrVersion;
    req.startedAt = report.plan.startedAt;
    req.finishedAt = report.plan.finishedAt;
    req.description = QObject::tr("Publicado desde QAflow · %1 de %2 casos ejecutados · %3 % de éxito")
                          .arg(report.executed).arg(report.total()).arg(report.successRate());

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
            for (const auto& s : row.run.steps) pc.design.append(TestStep{s.action, s.expected});
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
