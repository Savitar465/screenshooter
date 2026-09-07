#include "PlanView.h"

#include "application/PlanStore.h"
#include "application/TestCaseStore.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/Ui.h"

#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>

namespace qaflow {

namespace {
QWidget* stat(const QString& title, QLabel* value) {
    auto* w = new QWidget;
    auto* v = ui::vbox(w, 0, 0);
    v->addWidget(ui::label(title.toUpper(), "eyebrow"));
    value->setProperty("role", QStringLiteral("stat"));
    v->addWidget(value);
    return w;
}
} // namespace

PlanView::PlanView(TestCaseStore& cases, PlanStore& plan, QWidget* parent) : QWidget(parent), m_cases(cases), m_plan(plan) {
    auto* root = ui::hbox(this, 0, 0);
    QWidget* content;
    QVBoxLayout* outer;
    auto* sa = ui::scrollArea(&content, &outer);
    outer->setContentsMargins(32, 28, 32, 28);
    auto* page = new QWidget;
    page->setMaximumWidth(900);
    auto* v = ui::vbox(page, 0, 18);
    outer->addWidget(page, 0, Qt::AlignTop);
    root->addWidget(sa, 1);

    auto* head = new QWidget;
    auto* hh = ui::hbox(head, 0, 16);
    auto* titleBlock = new QWidget;
    auto* tv = ui::vbox(titleBlock, 0, 0);
    tv->addWidget(ui::label(QStringLiteral("PLAN DE PRUEBAS"), "eyebrow"));
    m_name = new QLineEdit;
    m_name->setProperty("role", QStringLiteral("title"));
    connect(m_name, &QLineEdit::textEdited, this, [this](const QString& t) { m_selfEdit = true; m_plan.setName(t); m_selfEdit = false; });
    tv->addWidget(m_name);
    hh->addWidget(titleBlock, 1);
    auto* stats = ui::card("card");
    auto* sh = ui::hbox(stats, 12, 16);
    sh->setContentsMargins(18, 12, 18, 12);
    m_count = new QLabel;
    m_steps = new QLabel;
    m_time = new QLabel;
    sh->addWidget(stat(QStringLiteral("Casos"), m_count));
    sh->addWidget(stat(QStringLiteral("Pasos"), m_steps));
    sh->addWidget(stat(QStringLiteral("Estimado"), m_time));
    m_time->setStyleSheet(QStringLiteral("color:%1;").arg(theme::Amber));
    hh->addWidget(stats, 0, Qt::AlignBottom);
    v->addWidget(head);

    auto* quick = new QWidget;
    auto* qh = ui::hbox(quick, 0, 8);
    auto* all = ui::button(QStringLiteral("Seleccionar todos"), "chip-lg");
    auto* none = ui::button(QStringLiteral("Ninguno"), "chip-lg");
    auto* high = ui::button(QStringLiteral("Solo prioridad alta"), "chip-lg");
    connect(all, &QPushButton::clicked, this, [this]() { m_plan.selectAll(); });
    connect(none, &QPushButton::clicked, this, [this]() { m_plan.selectNone(); });
    connect(high, &QPushButton::clicked, this, [this]() { m_plan.selectHighPriority(); });
    qh->addWidget(all);
    qh->addWidget(none);
    qh->addWidget(high);
    qh->addStretch(1);
    v->addWidget(quick);

    auto* rows = new QWidget;
    m_rows = ui::vbox(rows, 0, 6);
    v->addWidget(rows);

    auto* footer = new QWidget;
    auto* fh = ui::hbox(footer, 0, 0);
    fh->addStretch(1);
    auto* start = ui::button(QStringLiteral("▶ Iniciar ejecución del plan"), "success");
    start->setStyleSheet(QStringLiteral("padding:10px 18px;font-size:13.5px;font-weight:800;"));
    connect(start, &QPushButton::clicked, this, [this]() {
        const QStringList ids = m_plan.orderedCaseIds();
        if (ids.isEmpty()) { emit toast(QStringLiteral("Selecciona al menos un caso"), theme::Amber); return; }
        emit startPlanRequested(ids, m_plan.plan().name);
    });
    fh->addWidget(start);
    v->addWidget(footer);

    connect(&m_plan, &PlanStore::planChanged, this, &PlanView::refresh);
    connect(&m_cases, &TestCaseStore::caseChanged, this, &PlanView::refresh);
    refresh();
}

void PlanView::refresh() {
    if (!m_selfEdit && m_name->text() != m_plan.plan().name) { m_name->setText(m_plan.plan().name); m_name->setCursorPosition(0); }
    m_count->setText(QString::number(m_plan.orderedCaseIds().size()));
    m_steps->setText(QString::number(m_plan.totalSteps()));
    m_time->setText(m_plan.estimatedTime());

    ui::clearLayout(m_rows);
    for (const auto& c : m_cases.cases()) {
        if (c.status == CaseStatus::Obsoleto) continue;
        const bool on = m_plan.plan().contains(c.id);
        auto* row = ui::button(QString(), "plan-row");
        ui::setFlag(row, "active", on);
        auto* g = new QGridLayout(row);
        g->setContentsMargins(14, 10, 14, 10);
        g->setHorizontalSpacing(14);
        auto* box = new QLabel(on ? QStringLiteral("✓") : QString());
        box->setFixedSize(18, 18);
        box->setAlignment(Qt::AlignCenter);
        box->setStyleSheet(QStringLiteral("border-radius:5px;border:2px solid %1;background:%2;color:%3;font-size:12px;font-weight:900;")
                               .arg(on ? theme::Green : theme::Muted, on ? theme::Green : QStringLiteral("transparent"), theme::Bg));
        g->addWidget(box, 0, 0);
        auto* id = ui::label(c.id, "mono-muted");
        id->setStyleSheet(QStringLiteral("font-size:12px;"));
        id->setFixedWidth(90);
        g->addWidget(id, 0, 1);
        auto* title = new QLabel(c.title.isEmpty() ? QStringLiteral("(sin título)") : c.title);
        title->setStyleSheet(QStringLiteral("font-size:13.5px;font-weight:600;color:%1;").arg(theme::Text));
        g->addWidget(title, 0, 2);
        auto* suite = ui::label(c.suite, "muted-sm");
        suite->setFixedWidth(120);
        g->addWidget(suite, 0, 3);
        const auto pill = theme::priorityPill(toString(c.priority));
        auto* prio = ui::pill(toString(c.priority), pill.bg, pill.fg);
        prio->setFixedWidth(70);
        g->addWidget(prio, 0, 4);
        auto* steps = ui::label(QStringLiteral("%1 pasos").arg(c.steps.size()), "muted-sm");
        steps->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        steps->setFixedWidth(70);
        g->addWidget(steps, 0, 5);
        g->setColumnStretch(2, 1);
        for (auto* child : row->findChildren<QWidget*>()) child->setAttribute(Qt::WA_TransparentForMouseEvents);
        const QString cid = c.id;
        connect(row, &QPushButton::clicked, this, [this, cid]() { m_plan.toggle(cid); });
        m_rows->addWidget(row);
    }
}

} // namespace qaflow
