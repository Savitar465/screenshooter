#include "Sidebar.h"

#include "application/BugStore.h"
#include "application/PlanStore.h"
#include "application/RunController.h"
#include "application/RunHistoryStore.h"
#include "application/TestCaseStore.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/Ui.h"

#include <QApplication>
#include <QLabel>
#include <QPushButton>

namespace qaflow {

namespace {
constexpr int kRailWidth = 60;
constexpr int kButtonSize = 42;
constexpr int kIconSize = 22;
} // namespace

Sidebar::Sidebar(TestCaseStore& cases, PlanStore& plan, RunController& run, RunHistoryStore& history, BugStore& bugs, QWidget* parent)
    : QFrame(parent), m_cases(cases), m_plan(plan), m_run(run), m_history(history), m_bugs(bugs) {
    ui::setRole(this, "rail");
    setFixedWidth(kRailWidth);
    auto* v = ui::vbox(this, 0, 4);
    v->setContentsMargins(9, 14, 9, 14);

    // Marca
    auto* logo = new QLabel(QStringLiteral("QA"));
    logo->setFixedSize(kButtonSize, kButtonSize);
    logo->setAlignment(Qt::AlignCenter);
    logo->setToolTip(tr("QAflow %1 · Equipo Calidad").arg(QApplication::applicationVersion()));
    logo->setStyleSheet(QStringLiteral("background:%1;border:1px solid %2;border-radius:10px;color:%3;font-weight:800;font-size:13px;")
                            .arg(theme::tint(theme::Green, 46), theme::tint(theme::Green, 115), theme::Green));
    v->addWidget(logo);
    v->addSpacing(10);

    v->addWidget(navButton(Screen::Casos, icons::Glyph::Cases, theme::Violet, tr("Casos de prueba"), QStringLiteral("Ctrl+1")));
    v->addWidget(navButton(Screen::Plan, icons::Glyph::Plan, theme::Amber, tr("Plan de pruebas"), QStringLiteral("Ctrl+2")));
    v->addWidget(navButton(Screen::Run, icons::Glyph::Run, theme::Green, tr("Ejecución"), QStringLiteral("Ctrl+3")));
    v->addWidget(navButton(Screen::Historial, icons::Glyph::History, theme::Blue, tr("Historial"), QStringLiteral("Ctrl+4")));
    v->addWidget(navButton(Screen::Bug, icons::Glyph::Bug, theme::Red, tr("Reportar bug"), QStringLiteral("Ctrl+5")));
    v->addStretch(1);

    // Al pie, como los paneles secundarios de un IDE: las métricas del historial.
    auto* separator = new QFrame;
    separator->setFixedHeight(1);
    separator->setStyleSheet(QStringLiteral("background:%1;").arg(theme::Border));
    v->addWidget(separator);
    v->addSpacing(6);
    auto* metrics = railButton(icons::Glyph::Metrics, theme::Cyan, tr("Métricas · tasa de éxito por suite y evolución entre ciclos"));
    metrics->setObjectName(QStringLiteral("metricButton"));
    connect(metrics, &QPushButton::clicked, this, [this]() { emit metricsRequested(); });
    v->addWidget(metrics);

    connect(&m_cases, &TestCaseStore::casesChanged, this, &Sidebar::refresh);
    connect(&m_cases, &TestCaseStore::caseChanged, this, &Sidebar::refresh);
    connect(&m_plan, &PlanStore::planChanged, this, &Sidebar::refresh);
    connect(&m_plan, &PlanStore::plansChanged, this, &Sidebar::refresh);
    connect(&m_run, &RunController::runChanged, this, &Sidebar::refresh);
    connect(&m_history, &RunHistoryStore::historyChanged, this, &Sidebar::refresh);
    connect(&m_bugs, &BugStore::bugsChanged, this, &Sidebar::refresh);
    refresh();
}

QPushButton* Sidebar::railButton(icons::Glyph glyph, const QString& accent, const QString& tooltip) {
    auto* b = ui::button(QString(), "rail-nav");
    b->setFixedSize(kButtonSize, kButtonSize);
    b->setIconSize(QSize(kIconSize, kIconSize));
    b->setIcon(icons::pixmap(glyph, accent, kIconSize));
    b->setToolTip(tooltip);
    return b;
}

QPushButton* Sidebar::navButton(Screen s, icons::Glyph glyph, const QString& accent, const QString& name, const QString& shortcut) {
    auto* b = railButton(glyph, theme::Muted, QStringLiteral("%1 (%2)").arg(name, shortcut));
    b->setObjectName(QStringLiteral("nav-%1").arg(static_cast<int>(s)));
    connect(b, &QPushButton::clicked, this, [this, s]() { emit navigate(s); });

    // Insignia en la esquina inferior derecha del icono, como los contadores de un IDE.
    auto* badge = new QLabel(b);
    badge->setObjectName(QStringLiteral("badge-%1").arg(static_cast<int>(s)));
    badge->setAttribute(Qt::WA_TransparentForMouseEvents);
    badge->setAlignment(Qt::AlignCenter);
    badge->hide();
    auto* lay = ui::vbox(b, 0, 0);
    lay->setContentsMargins(0, 0, 2, 2);
    lay->addStretch(1);
    lay->addWidget(badge, 0, Qt::AlignRight | Qt::AlignBottom);

    m_items[s] = NavItem{b, badge, glyph, accent, name, shortcut};
    return b;
}

void Sidebar::setActive(Screen s) {
    m_active = s;
    for (auto it = m_items.begin(); it != m_items.end(); ++it) {
        const bool active = it.key() == s;
        ui::setFlag(it->button, "active", active);
        it->button->setIcon(icons::pixmap(it->glyph, active ? it->accent : theme::Muted, kIconSize));
    }
}

void Sidebar::setBadge(Screen s, const QString& text, const QString& color, const QString& textColor) {
    QLabel* badge = m_items[s].badge;
    badge->setVisible(!text.isEmpty());
    if (text.isEmpty()) return;
    badge->setText(text);
    badge->setStyleSheet(QStringLiteral("background:%1;color:%2;border-radius:6px;padding:0 3px;font-size:9px;font-weight:800;")
                             .arg(color, textColor));
}

void Sidebar::setTooltip(Screen s, const QString& detail) {
    const NavItem& item = m_items[s];
    const QString name = detail.isEmpty() ? item.name : QStringLiteral("%1 · %2").arg(item.name, detail);
    item.button->setToolTip(QStringLiteral("%1 (%2)").arg(name, item.shortcut));
}

void Sidebar::refresh() {
    setTooltip(Screen::Casos, tr("%1 casos").arg(m_cases.cases().size()));
    setTooltip(Screen::Plan, tr("%1 casos en el plan").arg(m_plan.orderedCaseIds().size()));
    setTooltip(Screen::Historial, m_history.runs().isEmpty() ? tr("sin ejecuciones archivadas")
                                                             : tr("%1 ejecuciones archivadas").arg(m_history.runs().size()));

    // Ejecución: progreso del caso en curso.
    const RunState& r = m_run.state();
    const TestCase* running = m_run.isRunning() ? m_cases.find(r.caseId) : nullptr;
    setBadge(Screen::Run, running ? QStringLiteral("%1/%2").arg(r.results.size()).arg(m_run.totalSteps()) : QString(),
             theme::Green, theme::OnAccent);
    setTooltip(Screen::Run, running ? tr("%1 · paso %2 de %3").arg(running->id).arg(r.idx + 1).arg(running->steps.size())
                                    : tr("sin ejecución en curso"));

    // Bugs: primero lo que está pendiente de enviar; si no, los issues abiertos.
    const int pending = m_bugs.pending().size();
    const int open = m_bugs.openIssueCount();
    setBadge(Screen::Bug, pending ? QString::number(pending) : open ? QString::number(open) : QString(),
             pending ? theme::Amber : theme::Red, pending ? theme::OnAccent : QStringLiteral("#ffffff"));
    setTooltip(Screen::Bug, pending ? tr("%1 pendientes de enviar").arg(pending)
                                    : open ? tr("%1 issues abiertos").arg(open) : QString());
    setActive(m_active);
}

} // namespace qaflow
