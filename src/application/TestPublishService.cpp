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

namespace {
/// El caso como lo espera la herramienta: lo que hace falta para crear su Test.
PublishCase publishCaseFrom(const TestCase& c) {
    PublishCase pc;
    pc.caseId = c.id;
    pc.testKey = c.testKey.trimmed();
    pc.title = c.title;
    pc.preconditions = c.preconditions;
    pc.design = c.steps;
    return pc;
}
} // namespace

QStringList TestPublishService::casesNeedingTest(const PlanReport& report) const {
    QStringList out;
    for (const auto& row : report.rows) {
        if (!row.executed) continue;
        const TestCase* c = m_cases.find(row.caseId);
        if (!c || c->testKey.trimmed().isEmpty()) out << row.caseId;
    }
    return out;
}

PublishRequest TestPublishService::requestFor(const PlanReport& report) const {
    PublishRequest req;
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
        pc.title = row.title;
        pc.verdict = row.run.verdict;
        pc.steps = row.run.steps;
        pc.durationSecs = row.run.durationSecs;
        if (const TestCase* c = m_cases.find(row.caseId)) {
            // Con lo que el caso dice hoy se crea su Test si todavía no está enlazado a ninguno.
            const PublishCase from = publishCaseFrom(*c);
            pc.testKey = from.testKey;
            pc.preconditions = from.preconditions;
            pc.design = from.design;
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

void TestPublishService::testConnection(std::function<void(const ConnectionResult&)> done) {
    if (!m_zephyr) { done(ConnectionResult{false, {}, tr("Integración no disponible")}); return; }
    m_zephyr->testConnection(m_settings.tracker(), std::move(done));
}

void TestPublishService::publish(const PlanReport& report, std::function<void(const PublishResult&)> done) {
    if (!enabled()) {
        PublishResult r;
        r.error = tr("Activa Zephyr en Ajustes para publicar los ciclos");
        done(r);
        return;
    }
    m_zephyr->publish(m_settings.tracker(), requestFor(report), [this, planRunId = report.plan.id, done = std::move(done)](const PublishResult& r) {
        // El ciclo de plan se queda con el de Zephyr en el que acabaron sus resultados.
        if (r.ok) m_history.markPublished(planRunId, r.cycleId);
        // El Test recién creado queda enlazado al caso: los ciclos siguientes reutilizan ese mismo
        // Test en vez de estrenar otro.
        for (auto it = r.createdTests.constBegin(); it != r.createdTests.constEnd(); ++it) {
            const QString key = it.value();
            if (key.isEmpty()) continue;
            m_cases.updateCase(it.key(), [&key](TestCase& c) { c.testKey = key; });
        }
        done(r);
    });
}

} // namespace qaflow
