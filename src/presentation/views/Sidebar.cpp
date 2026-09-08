#include "Sidebar.h"

#include "application/PlanStore.h"
#include "application/RunController.h"
#include "application/BugStore.h"
#include "application/RunHistoryStore.h"
#include "application/TestCaseStore.h"
#include "core/models/Metrics.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/Ui.h"

#include <QApplication>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>

namespace qaflow {

Sidebar::Sidebar(TestCaseStore& cases, PlanStore& plan, RunController& run, RunHistoryStore& history, BugStore& bugs, QWidget* parent)
    : QFrame(parent), m_cases(cases), m_plan(plan), m_run(run), m_history(history), m_bugs(bugs) {
    ui::setRole(this, "sidebar");
    setFixedWidth(220);
    auto* v = ui::vbox(this, 12, 6);
    v->setContentsMargins(12, 18, 12, 18);

    // Marca
    auto* brand = new QWidget;
    auto* bh = ui::hbox(brand, 0, 10);
    bh->setContentsMargins(10, 4, 10, 18);
    auto* logo = new QLabel(QStringLiteral("QA"));
    logo->setFixedSize(28, 28);
    logo->setAlignment(Qt::AlignCenter);
    logo->setStyleSheet(QStringLiteral("background:%1;border:1px solid %2;border-radius:8px;color:%3;font-weight:800;font-size:13px;").arg(theme::tint(theme::Green, 46), theme::tint(theme::Green, 115), theme::Green));
    bh->addWidget(logo);
    auto* brandText = new QWidget;
    auto* bv = ui::vbox(brandText, 0, 0);
    auto* name = new QLabel(QStringLiteral("QAflow"));
    name->setStyleSheet(QStringLiteral("font-weight:800;font-size:15px;"));
    bv->addWidget(name);
    bv->addWidget(ui::label(tr("Equipo Calidad · v%1").arg(QApplication::applicationVersion()), "muted-sm"));
    bh->addWidget(brandText, 1);
    v->addWidget(brand);

    v->addWidget(navButton(Screen::Casos, tr("Casos de prueba"), theme::Violet));
    v->addWidget(navButton(Screen::Plan, tr("Plan de pruebas"), theme::Amber));
    v->addWidget(navButton(Screen::Run, tr("Ejecución"), theme::Green));
    v->addWidget(navButton(Screen::Historial, tr("Historial"), theme::Blue));
    v->addWidget(navButton(Screen::Bug, tr("Reportar bug"), theme::Red));
    v->addStretch(1);

    // Tarjeta "En ejecución"
    m_runningCard = ui::button(QString(), "running-card");
    auto* rc = ui::vbox(m_runningCard, 12, 4);
    auto* rcHead = new QWidget;
    auto* rh = ui::hbox(rcHead, 0, 6);
    rh->addWidget(ui::dot(theme::Green, 7));
    auto* rcTitle = ui::label(tr("EN EJECUCIÓN"), "eyebrow");
    rcTitle->setStyleSheet(QStringLiteral("color:%1;").arg(theme::Green));
    rh->addWidget(rcTitle, 1);
    rc->addWidget(rcHead);
    m_runId = new QLabel;
    m_runId->setStyleSheet(QStringLiteral("font-size:12.5px;font-weight:700;font-family:'Consolas','DejaVu Sans Mono',monospace;"));
    rc->addWidget(m_runId);
    m_runTitle = ui::label(QString(), "muted-sm");
    rc->addWidget(m_runTitle);
    m_runStep = ui::label(QString(), "muted-sm");
    rc->addWidget(m_runStep);
    connect(m_runningCard, &QPushButton::clicked, this, [this]() { emit navigate(Screen::Run); });
    v->addWidget(m_runningCard);

    // Tasa de éxito global (última ejecución de cada caso) y tendencia entre ciclos
    auto* metricCard = ui::button(QString(), "metric-card");
    metricCard->setObjectName(QStringLiteral("metricCard"));
    metricCard->setToolTip(tr("Ver métricas por suite y evolución entre ciclos"));
    auto* mv = ui::vbox(metricCard, 12, 4);
    mv->addWidget(ui::label(tr("TASA DE ÉXITO"), "eyebrow"));
    auto* rateRow = new QWidget;
    auto* rh2 = ui::hbox(rateRow, 0, 8);
    m_rate = new QLabel;
    m_rate->setStyleSheet(QStringLiteral("font-size:22px;font-weight:800;"));
    rh2->addWidget(m_rate);
    m_rateDetail = ui::label(QString(), "muted-sm");
    m_rateDetail->setWordWrap(true);
    rh2->addWidget(m_rateDetail, 1);
    mv->addWidget(rateRow);
    m_trend = ui::label(QString(), "muted-sm");
    m_trend->setWordWrap(true);
    mv->addWidget(m_trend);
    for (auto* child : metricCard->findChildren<QWidget*>()) child->setAttribute(Qt::WA_TransparentForMouseEvents);
    connect(metricCard, &QPushButton::clicked, this, [this]() { emit metricsRequested(); });
    v->addWidget(metricCard);

    // Plan activo y progreso de su ciclo actual
    auto* planCard = ui::button(QString(), "running-card");
    planCard->setToolTip(tr("Abrir el plan activo"));
    auto* sv = ui::vbox(planCard, 12, 6);
    sv->addWidget(ui::label(tr("PLAN ACTIVO"), "eyebrow"));
    m_planName = new QLabel;
    m_planName->setWordWrap(true);
    m_planName->setStyleSheet(QStringLiteral("font-weight:700;font-size:12.5px;"));
    sv->addWidget(m_planName);
    auto* row = new QWidget;
    auto* rowH = ui::hbox(row, 0, 0);
    m_planCycle = ui::label(QString(), "muted-sm");
    rowH->addWidget(m_planCycle, 1);
    m_sprintCount = new QLabel;
    m_sprintCount->setStyleSheet(QStringLiteral("font-weight:700;"));
    rowH->addWidget(m_sprintCount);
    sv->addWidget(row);
    m_sprintBar = new QProgressBar;
    m_sprintBar->setTextVisible(false);
    m_sprintBar->setRange(0, 100);
    sv->addWidget(m_sprintBar);
    for (auto* child : planCard->findChildren<QWidget*>()) child->setAttribute(Qt::WA_TransparentForMouseEvents);
    connect(planCard, &QPushButton::clicked, this, [this]() { emit navigate(Screen::Plan); });
    v->addWidget(planCard);

    connect(&m_cases, &TestCaseStore::casesChanged, this, &Sidebar::refresh);
    connect(&m_cases, &TestCaseStore::caseChanged, this, &Sidebar::refresh);
    connect(&m_plan, &PlanStore::planChanged, this, &Sidebar::refresh);
    connect(&m_plan, &PlanStore::plansChanged, this, &Sidebar::refresh);
    connect(&m_run, &RunController::runChanged, this, &Sidebar::refresh);
    connect(&m_history, &RunHistoryStore::historyChanged, this, &Sidebar::refresh);
    connect(&m_bugs, &BugStore::bugsChanged, this, &Sidebar::refresh);
    refresh();
}

QPushButton* Sidebar::navButton(Screen s, const QString& label, const QString& dotColor) {
    auto* b = ui::button(QString(), "nav");
    b->setObjectName(QStringLiteral("nav-%1").arg(static_cast<int>(s)));
    auto* h = ui::hbox(b, 0, 10);
    h->setContentsMargins(10, 9, 10, 9);
    h->addWidget(ui::dot(dotColor, 8));
    auto* text = new QLabel(label);
    text->setStyleSheet(QStringLiteral("font-size:13.5px;font-weight:600;color:inherit;"));
    text->setAttribute(Qt::WA_TransparentForMouseEvents);
    h->addWidget(text, 1);
    auto* count = ui::label(QString(), "mono-muted");
    count->setStyleSheet(QStringLiteral("font-family:inherit;font-size:11px;"));
    h->addWidget(count);
    connect(b, &QPushButton::clicked, this, [this, s]() { emit navigate(s); });
    m_buttons[s] = b;
    m_counts[s] = count;
    return b;
}

void Sidebar::setActive(Screen s) {
    m_active = s;
    for (auto it = m_buttons.begin(); it != m_buttons.end(); ++it) {
        ui::setFlag(it.value(), "active", it.key() == s);
        // El color del texto interno no hereda por QSS; lo fijamos a mano.
        for (auto* l : it.value()->findChildren<QLabel*>()) {
            if (l == m_counts[it.key()]) continue;
            l->setStyleSheet(QStringLiteral("font-size:13.5px;font-weight:600;color:%1;").arg(it.key() == s ? theme::Text : theme::Muted));
        }
    }
}

void Sidebar::refresh() {
    const int total = m_cases.cases().size();
    m_counts[Screen::Casos]->setText(QString::number(total));
    m_counts[Screen::Plan]->setText(QString::number(m_plan.orderedCaseIds().size()));
    m_counts[Screen::Bug]->setText(m_bugs.pending().isEmpty() ? (m_bugs.openIssueCount() ? QString::number(m_bugs.openIssueCount()) : QString())
                                                             : QStringLiteral("%1 ⏳").arg(m_bugs.pending().size()));
    m_counts[Screen::Historial]->setText(m_history.runs().isEmpty() ? QString() : QString::number(m_history.runs().size()));
    const RunState& r = m_run.state();
    m_counts[Screen::Run]->setText(r.caseId.isEmpty() || r.results.isEmpty() ? QString()
                                   : QStringLiteral("%1/%2").arg(r.results.size()).arg(m_run.totalSteps()));

    const TestCase* running = m_run.isRunning() ? m_cases.find(r.caseId) : nullptr;
    m_runningCard->setVisible(running != nullptr);
    if (running) {
        m_runId->setText(running->id);
        m_runTitle->setText(ui::elide(running->title, 26));
        m_runStep->setText(tr("Paso %1 de %2").arg(r.idx + 1).arg(running->steps.size()));
    }

    // Métricas: tasa global y tendencia (último ciclo terminado frente al anterior, del plan activo)
    const MetricsSummary sum = metrics::summary(m_cases.cases());
    const int rate = sum.successRate();
    m_rate->setText(sum.executed() ? QStringLiteral("%1 %").arg(rate) : QStringLiteral("—"));
    m_rate->setStyleSheet(QStringLiteral("font-size:22px;font-weight:800;color:%1;")
                              .arg(!sum.executed() ? theme::Muted : rate >= 80 ? theme::Green : rate >= 50 ? theme::Amber : theme::Red));
    m_rateDetail->setText(tr("%1 de %2 ejecutados").arg(sum.executed()).arg(sum.cases));
    const TestPlan* plan = m_plan.active();
    const auto cycles = metrics::cycles(RunHistory{m_history.runs(), m_history.plans()}, plan ? plan->id : QString());
    if (const auto delta = metrics::trend(cycles)) {
        const QString sign = *delta > 0 ? QStringLiteral("▲ +%1").arg(*delta) : *delta < 0 ? QStringLiteral("▼ %1").arg(*delta) : QStringLiteral("= 0");
        m_trend->setText(tr("%1 pts frente al ciclo anterior").arg(sign));
        m_trend->setStyleSheet(QStringLiteral("font-size:12px;color:%1;").arg(*delta > 0 ? theme::Green : *delta < 0 ? theme::Red : theme::Muted));
    } else {
        m_trend->setText(cycles.isEmpty() ? tr("Sin ciclos terminados") : tr("Un ciclo terminado · sin comparación"));
        m_trend->setStyleSheet(QStringLiteral("font-size:12px;color:%1;").arg(theme::Muted));
    }
    m_planName->setText(plan ? ui::elide(plan->name, 40) : QStringLiteral("—"));
    const auto cycle = plan ? m_plan.latestCycle(plan->id) : std::nullopt;
    if (cycle) {
        m_planCycle->setText(cycle->plan.isFinished() ? tr("Último ciclo") : tr("Ciclo en curso"));
        m_sprintCount->setText(QStringLiteral("%1/%2").arg(cycle->executed).arg(cycle->total()));
        m_sprintBar->setValue(cycle->total() ? cycle->executed * 100 / cycle->total() : 0);
    } else {
        m_planCycle->setText(tr("Sin ciclos"));
        m_sprintCount->setText(tr("%1 casos").arg(plan ? m_plan.orderedCaseIds().size() : 0));
        m_sprintBar->setValue(0);
    }
    setActive(m_active);
}

} // namespace qaflow
