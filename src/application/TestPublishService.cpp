#include "TestPublishService.h"

#include "application/SettingsStore.h"
#include "application/TestCaseStore.h"

#include <QFileInfo>

namespace qaflow {

TestPublishService::TestPublishService(std::shared_ptr<ITestManagement> zephyr, TestCaseStore& cases, SettingsStore& settings, QObject* parent)
    : QObject(parent), m_zephyr(std::move(zephyr)), m_cases(cases), m_settings(settings) {}

bool TestPublishService::enabled() const {
    const TrackerSettings& t = m_settings.tracker();
    return m_zephyr && t.zephyr && t.kind == TrackerKind::Jira;
}

QStringList TestPublishService::casesWithoutTestKey(const PlanReport& report) const {
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
            pc.testKey = c->testKey.trimmed();
            // Las evidencias son las que el caso tiene ahora en disco, con el paso al que se asignaron.
            for (const auto& shot : c->shots)
                if (QFileInfo::exists(shot.path)) pc.attachments.append(PublishAttachment{shot.path, shot.step});
        }
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
    m_zephyr->publish(m_settings.tracker(), requestFor(report), std::move(done));
}

} // namespace qaflow
