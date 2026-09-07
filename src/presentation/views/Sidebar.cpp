#include "Sidebar.h"

#include "application/PlanStore.h"
#include "application/RunController.h"
#include "application/RunHistoryStore.h"
#include "application/TestCaseStore.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/Ui.h"

#include <QLabel>
#include <QProgressBar>
#include <QPushButton>

namespace qaflow {

Sidebar::Sidebar(TestCaseStore& cases, PlanStore& plan, RunController& run, RunHistoryStore& history, QWidget* parent)
    : QFrame(parent), m_cases(cases), m_plan(plan), m_run(run), m_history(history) {
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
    logo->setStyleSheet(QStringLiteral("background:rgba(16,185,129,46);border:1px solid rgba(16,185,129,115);border-radius:8px;color:%1;font-weight:800;font-size:13px;").arg(theme::Green));
    bh->addWidget(logo);
    auto* brandText = new QWidget;
    auto* bv = ui::vbox(brandText, 0, 0);
    auto* name = new QLabel(QStringLiteral("QAflow"));
    name->setStyleSheet(QStringLiteral("font-weight:800;font-size:15px;"));
    bv->addWidget(name);
    bv->addWidget(ui::label(QStringLiteral("Equipo Calidad · v0.4"), "muted-sm"));
    bh->addWidget(brandText, 1);
    v->addWidget(brand);

    v->addWidget(navButton(Screen::Casos, QStringLiteral("Casos de prueba"), theme::Violet));
    v->addWidget(navButton(Screen::Plan, QStringLiteral("Plan de pruebas"), theme::Amber));
    v->addWidget(navButton(Screen::Run, QStringLiteral("Ejecución"), theme::Green));
    v->addWidget(navButton(Screen::Historial, QStringLiteral("Historial"), theme::Blue));
    v->addWidget(navButton(Screen::Bug, QStringLiteral("Reportar bug"), theme::Red));
    v->addWidget(navButton(Screen::Ajustes, QStringLiteral("Ajustes"), theme::Cyan));
    v->addStretch(1);

    // Tarjeta "En ejecución"
    m_runningCard = ui::button(QString(), "running-card");
    auto* rc = ui::vbox(m_runningCard, 12, 4);
    auto* rcHead = new QWidget;
    auto* rh = ui::hbox(rcHead, 0, 6);
    rh->addWidget(ui::dot(theme::Green, 7));
    auto* rcTitle = ui::label(QStringLiteral("EN EJECUCIÓN"), "eyebrow");
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

    // Plan activo y progreso de su ciclo actual
    auto* planCard = ui::button(QString(), "running-card");
    planCard->setToolTip(QStringLiteral("Abrir el plan activo"));
    auto* sv = ui::vbox(planCard, 12, 6);
    sv->addWidget(ui::label(QStringLiteral("PLAN ACTIVO"), "eyebrow"));
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
    refresh();
}

QPushButton* Sidebar::navButton(Screen s, const QString& label, const QString& dotColor) {
    auto* b = ui::button(QString(), "nav");
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
    m_counts[Screen::Historial]->setText(m_history.runs().isEmpty() ? QString() : QString::number(m_history.runs().size()));
    const RunState& r = m_run.state();
    m_counts[Screen::Run]->setText(r.caseId.isEmpty() || r.results.isEmpty() ? QString()
                                   : QStringLiteral("%1/%2").arg(r.results.size()).arg(m_run.totalSteps()));

    const TestCase* running = m_run.isRunning() ? m_cases.find(r.caseId) : nullptr;
    m_runningCard->setVisible(running != nullptr);
    if (running) {
        m_runId->setText(running->id);
        m_runTitle->setText(ui::elide(running->title, 26));
        m_runStep->setText(QStringLiteral("Paso %1 de %2").arg(r.idx + 1).arg(running->steps.size()));
    }

    const TestPlan* plan = m_plan.active();
    m_planName->setText(plan ? ui::elide(plan->name, 40) : QStringLiteral("—"));
    const auto cycle = plan ? m_plan.latestCycle(plan->id) : std::nullopt;
    if (cycle) {
        m_planCycle->setText(cycle->plan.isFinished() ? QStringLiteral("Último ciclo") : QStringLiteral("Ciclo en curso"));
        m_sprintCount->setText(QStringLiteral("%1/%2").arg(cycle->executed).arg(cycle->total()));
        m_sprintBar->setValue(cycle->total() ? cycle->executed * 100 / cycle->total() : 0);
    } else {
        m_planCycle->setText(QStringLiteral("Sin ciclos"));
        m_sprintCount->setText(QStringLiteral("%1 casos").arg(plan ? m_plan.orderedCaseIds().size() : 0));
        m_sprintBar->setValue(0);
    }
    setActive(m_active);
}

} // namespace qaflow
