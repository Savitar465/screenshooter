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

QFrame* separator() {
    auto* f = new QFrame;
    f->setFixedSize(1, 16);
    f->setStyleSheet(QStringLiteral("background:%1;").arg(theme::Border));
    return f;
}
} // namespace

StatusStrip::StatusStrip(TestCaseStore& cases, PlanStore& plan, RunController& run, RunHistoryStore& history, QWidget* parent)
    : QFrame(parent), m_cases(cases), m_plan(plan), m_run(run), m_history(history) {
    ui::setRole(this, "status-strip");
    setFixedHeight(kHeight);
    auto* h = ui::hbox(this, 0, 6);
    h->setContentsMargins(10, 4, 10, 4);

    // Ejecución en curso
    QHBoxLayout* runBody;
    auto* runItem = item(tr("Ir a la ejecución en curso"), &runBody);
    runItem->setObjectName(QStringLiteral("statusRun"));
    m_runDot = ui::dot(theme::Green, 7);
    runBody->addWidget(m_runDot);
    m_runText = ui::label(QString(), "muted-sm");
    runBody->addWidget(m_runText);
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
    h->addWidget(separator());

    // Plan activo y progreso de su ciclo
    QHBoxLayout* planBody;
    auto* planItem = item(tr("Abrir el plan activo"), &planBody);
    planItem->setObjectName(QStringLiteral("statusPlan"));
    planBody->addWidget(ui::label(tr("PLAN"), "eyebrow"));
    m_planName = new QLabel;
    m_planName->setStyleSheet(QStringLiteral("font-size:12px;font-weight:700;"));
    planBody->addWidget(m_planName);
    m_planCycle = ui::label(QString(), "muted-sm");
    planBody->addWidget(m_planCycle);
    m_planBar = new QProgressBar;
    m_planBar->setTextVisible(false);
    m_planBar->setRange(0, 100);
    m_planBar->setFixedSize(64, 6);
    planBody->addWidget(m_planBar);
    connect(planItem, &QPushButton::clicked, this, [this]() { emit navigate(Screen::Plan); });
    h->addWidget(planItem);

    // Las etiquetas no deben robar el clic a su bloque.
    for (auto* item : {runItem, metricItem, planItem})
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
    m_runDot->setVisible(running != nullptr);
    m_runText->setText(running ? tr("%1 · %2 · paso %3 de %4").arg(running->id, ui::elide(running->title, 34)).arg(r.idx + 1).arg(running->steps.size())
                               : tr("Sin ejecución en curso"));
    m_runText->setStyleSheet(QStringLiteral("font-size:12px;color:%1;").arg(running ? theme::Text : theme::Muted));

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

    // Plan activo y su ciclo
    m_planName->setText(plan ? ui::elide(plan->name, 28) : QStringLiteral("—"));
    const auto cycle = plan ? m_plan.latestCycle(plan->id) : std::nullopt;
    if (cycle) {
        m_planCycle->setText(QStringLiteral("%1 %2/%3").arg(cycle->plan.isFinished() ? tr("último ciclo") : tr("ciclo en curso"))
                                 .arg(cycle->executed).arg(cycle->total()));
        m_planBar->setValue(cycle->total() ? cycle->executed * 100 / cycle->total() : 0);
    } else {
        m_planCycle->setText(tr("sin ciclos · %1 casos").arg(plan ? m_plan.orderedCaseIds().size() : 0));
        m_planBar->setValue(0);
    }
}

} // namespace qaflow
