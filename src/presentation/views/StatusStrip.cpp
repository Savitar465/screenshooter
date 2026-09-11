#include "StatusStrip.h"

#include "application/PlanStore.h"
#include "application/RunController.h"
#include "application/RunHistoryStore.h"
#include "application/TestCaseStore.h"
#include "core/models/Metrics.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/Ui.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>

namespace qaflow {

namespace {
constexpr int kHeight = 34;
} // namespace

StatusStrip::StatusStrip(TestCaseStore& cases, PlanStore& plan, RunController& run, RunHistoryStore& history, QWidget* parent)
    : QFrame(parent), m_cases(cases), m_plan(plan), m_run(run), m_history(history) {
    ui::setRole(this, "status-strip");
    setFixedHeight(kHeight);
    auto* h = ui::hbox(this, 0, 6);
    h->setContentsMargins(10, 4, 10, 4);

    // Un único bloque para la ejecución de un plan o un caso.
    QHBoxLayout* runBody;
    auto* runItem = item(tr("Ir a la ejecución en curso"), &runBody);
    runItem->setObjectName(QStringLiteral("statusRun"));
    m_runDot = ui::dot(theme::Green, 7);
    runBody->addWidget(m_runDot);
    m_runText = ui::label(QString(), "muted-sm");
    runBody->addWidget(m_runText);
    m_runBar = new QProgressBar;
    m_runBar->setObjectName(QStringLiteral("statusRunProgress"));
    m_runBar->setTextVisible(false);
    m_runBar->setRange(0, 100);
    m_runBar->setFixedSize(64, 6);
    runBody->addWidget(m_runBar);
    connect(runItem, &QPushButton::clicked, this, [this]() { emit navigate(Screen::Run); });
    h->addWidget(runItem);
    h->addStretch(1);

    // Tasa de éxito y tendencia entre ciclos
    QHBoxLayout* metricBody;
    auto* metricItem = item(tr("Ver métricas por suite y evolución entre ciclos"), &metricBody);
    metricItem->setObjectName(QStringLiteral("metricCard"));
    metricBody->addWidget(ui::label(tr("ÉXITO"), "eyebrow"));
    m_rate = new QLabel;
    m_rate->setStyleSheet(QStringLiteral("font-size:13px;font-weight:800;"));
    metricBody->addWidget(m_rate);
    m_rateDetail = ui::label(QString(), "muted-sm");
    metricBody->addWidget(m_rateDetail);
    m_trend = ui::label(QString(), "muted-sm");
    metricBody->addWidget(m_trend);
    connect(metricItem, &QPushButton::clicked, this, [this]() { emit metricsRequested(); });
    h->addWidget(metricItem);

    // Las etiquetas no deben robar el clic a su bloque.
    for (auto* item : {runItem, metricItem})
        for (auto* child : item->findChildren<QWidget*>()) child->setAttribute(Qt::WA_TransparentForMouseEvents);

    connect(&m_cases, &TestCaseStore::casesChanged, this, &StatusStrip::refresh);
    connect(&m_cases, &TestCaseStore::caseChanged, this, &StatusStrip::refresh);
    connect(&m_plan, &PlanStore::planChanged, this, &StatusStrip::refresh);
    connect(&m_plan, &PlanStore::plansChanged, this, &StatusStrip::refresh);
    connect(&m_run, &RunController::runChanged, this, &StatusStrip::refresh);
    connect(&m_history, &RunHistoryStore::historyChanged, this, &StatusStrip::refresh);
    refresh();
}

QPushButton* StatusStrip::item(const QString& tooltip, QHBoxLayout** body) {
    auto* b = ui::button(QString(), "status-item");
    b->setToolTip(tooltip);
    auto* h = ui::hbox(b, 0, 7);
    h->setContentsMargins(9, 2, 9, 2);
    *body = h;
    return b;
}

void StatusStrip::refresh() {
    // Ejecución en curso
    const RunState& r = m_run.state();
    const TestCase* running = m_run.isRunning() ? m_cases.find(r.caseId) : nullptr;
    const auto* runningPlan = m_history.findPlan(m_run.planRunId());
    const bool planInProgress = runningPlan && !runningPlan->isFinished();
    m_runDot->setVisible(planInProgress || running);
    m_runBar->setVisible(planInProgress);
    if (planInProgress) {
        const auto report = m_history.report(runningPlan->id);
        m_runText->setText(tr("Plan: %1 · ciclo en curso · %2/%3 casos")
                              .arg(ui::elide(runningPlan->name, 34)).arg(report.executed).arg(report.total()));
        m_runBar->setValue(report.total() ? report.executed * 100 / report.total() : 0);
    } else {
        m_runText->setText(running ? tr("%1 · %2 · paso %3 de %4").arg(running->id, ui::elide(running->title, 34)).arg(r.idx + 1).arg(running->steps.size())
                                   : tr("Sin ejecución en curso"));
    }
    m_runText->setStyleSheet(QStringLiteral("font-size:12px;color:%1;").arg(planInProgress || running ? theme::Text : theme::Muted));

    // Tasa de éxito global (última ejecución de cada caso) y tendencia entre ciclos
    const MetricsSummary sum = metrics::summary(m_cases.cases());
    const int rate = sum.successRate();
    m_rate->setText(sum.executed() ? QStringLiteral("%1 %").arg(rate) : QStringLiteral("—"));
    m_rate->setStyleSheet(QStringLiteral("font-size:13px;font-weight:800;color:%1;")
                              .arg(!sum.executed() ? theme::Muted : rate >= 80 ? theme::Green : rate >= 50 ? theme::Amber : theme::Red));
    m_rateDetail->setText(tr("%1 de %2 ejecutados").arg(sum.executed()).arg(sum.cases));
    const TestPlan* plan = m_plan.active();
    const auto cycles = metrics::cycles(RunHistory{m_history.runs(), m_history.plans()}, plan ? plan->id : QString());
    if (const auto delta = metrics::trend(cycles)) {
        const QString sign = *delta > 0 ? QStringLiteral("▲ +%1").arg(*delta) : *delta < 0 ? QStringLiteral("▼ %1").arg(*delta) : QStringLiteral("= 0");
        m_trend->setText(tr("%1 pts").arg(sign));
        m_trend->setStyleSheet(QStringLiteral("font-size:12px;color:%1;").arg(*delta > 0 ? theme::Green : *delta < 0 ? theme::Red : theme::Muted));
    } else {
        m_trend->setText(cycles.isEmpty() ? tr("sin ciclos terminados") : tr("un ciclo terminado"));
        m_trend->setStyleSheet(QStringLiteral("font-size:12px;color:%1;").arg(theme::Muted));
    }

}

} // namespace qaflow
