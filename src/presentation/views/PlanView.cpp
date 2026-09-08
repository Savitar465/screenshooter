#include "PlanView.h"

#include "application/PlanStore.h"
#include "application/TestCaseStore.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/FlowLayout.h"
#include "presentation/widgets/ProgressCells.h"
#include "presentation/widgets/Ui.h"

#include <QCoreApplication>
#include <QGridLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>

namespace qaflow {

namespace {
QWidget* stat(const QString& title, QLabel* value, QLabel* sub = nullptr) {
    auto* w = new QWidget;
    auto* v = ui::vbox(w, 0, 0);
    v->addWidget(ui::label(title.toUpper(), "eyebrow"));
    value->setProperty("role", QStringLiteral("stat"));
    v->addWidget(value);
    if (sub) v->addWidget(sub);
    return w;
}
QString verdictColor(Verdict v) {
    switch (v) {
        case Verdict::Superado: return theme::Green;
        case Verdict::Fallido: return theme::Red;
        case Verdict::Bloqueado: return theme::Amber;
    }
    return theme::Muted;
}
QLabel* verdictPill(Verdict v) {
    return ui::pill(label(v).toUpper(), verdictColor(v), v == Verdict::Fallido ? QStringLiteral("#ffffff") : theme::Bg);
}
QLabel* mutedPill(const QString& text) { return ui::pill(text, theme::tint(theme::Muted, 38), theme::Muted); }
QString when(const QDateTime& dt) { return dt.isValid() ? dt.toString(QStringLiteral("dd/MM/yyyy HH:mm")) : QStringLiteral("—"); }
} // namespace

PlanView::PlanView(TestCaseStore& cases, PlanStore& plans, QWidget* parent) : QWidget(parent), m_cases(cases), m_plans(plans) {
    auto* root = ui::hbox(this, 0, 0);
    buildListPane(root);
    buildEditor(root);

    connect(&m_plans, &PlanStore::plansChanged, this, [this]() { refreshList(); refreshEditor(); });
    connect(&m_plans, &PlanStore::planChanged, this, [this]() { refreshList(); refreshEditor(); });
    connect(&m_cases, &TestCaseStore::caseChanged, this, &PlanView::refreshRows);
    refreshList();
    refreshEditor();
}

// ---- Lista de planes -----------------------------------------------------------------------

void PlanView::buildListPane(QHBoxLayout* root) {
    auto* pane = ui::card("list-pane");
    pane->setMinimumWidth(250);
    pane->setMaximumWidth(300);
    pane->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* v = ui::vbox(pane, 0, 0);

    auto* head = new QWidget;
    auto* hv = ui::vbox(head, 16, 12);
    hv->setContentsMargins(16, 18, 16, 12);
    auto* titleRow = new QWidget;
    auto* th = ui::hbox(titleRow, 0, 8);
    th->addWidget(ui::label(tr("Planes"), "h1-sm"), 1);
    auto* newBtn = ui::button(tr("+ Nuevo"), "primary");
    newBtn->setStyleSheet(QStringLiteral("padding:6px 12px;font-size:12.5px;border-radius:8px;"));
    connect(newBtn, &QPushButton::clicked, this, &PlanView::newPlan);
    th->addWidget(newBtn);
    hv->addWidget(titleRow);
    auto* filters = new QWidget;
    m_filterRow = new FlowLayout(filters, 0, 6, 6);
    hv->addWidget(filters);
    v->addWidget(head);

    QWidget* content;
    auto* sa = ui::scrollArea(&content, &m_listLayout);
    m_listLayout->setContentsMargins(10, 0, 10, 16);
    m_listLayout->setSpacing(4);
    v->addWidget(sa, 1);
    root->addWidget(pane);
}

void PlanView::refreshList() {
    ui::clearLayout(m_filterRow);
    int activeCount = 0, archivedCount = 0;
    for (const auto& p : m_plans.plans()) (p.archived ? archivedCount : activeCount)++;
    for (auto [archived, text] : {std::pair{false, tr("Activos · %1").arg(activeCount)}, std::pair{true, tr("Archivados · %1").arg(archivedCount)}}) {
        auto* b = ui::button(text, "chip");
        ui::setFlag(b, "active", archived == m_showArchived);
        connect(b, &QPushButton::clicked, this, [this, archived]() { m_showArchived = archived; refreshList(); });
        m_filterRow->addWidget(b);
    }

    ui::clearLayout(m_listLayout);
    int shown = 0;
    for (const auto& p : m_plans.plans()) {
        if (p.archived != m_showArchived) continue;
        ++shown;
        auto* row = ui::button(QString(), "row");
        ui::setFlag(row, "active", p.id == m_plans.activeId());
        auto* v = ui::vbox(row, 0, 4);
        v->setContentsMargins(12, 10, 12, 10);
        auto* top = new QWidget;
        auto* th = ui::hbox(top, 0, 8);
        th->addWidget(ui::label(p.id, "mono-muted"));
        th->addStretch(1);
        const auto cycle = m_plans.latestCycle(p.id);
        if (!cycle) th->addWidget(mutedPill(tr("SIN CICLOS")));
        else if (!cycle->plan.isFinished()) th->addWidget(mutedPill(tr("EN CURSO")));
        else th->addWidget(verdictPill(cycle->verdict()));
        v->addWidget(top);
        auto* title = new QLabel(p.name.isEmpty() ? tr("(sin nombre)") : p.name);
        title->setWordWrap(true);
        title->setStyleSheet(QStringLiteral("font-size:13.5px;font-weight:600;color:%1;").arg(theme::Text));
        v->addWidget(title);
        QString info = tr("%1 casos · %2 ciclos").arg(m_plans.orderedCaseIds(p.id).size()).arg(m_plans.cycleCount(p.id));
        if (cycle) info += tr(" · último %1/%2 · %3 %").arg(cycle->executed).arg(cycle->total()).arg(cycle->successRate());
        v->addWidget(ui::label(info, "muted-sm"));
        for (auto* child : row->findChildren<QWidget*>()) child->setAttribute(Qt::WA_TransparentForMouseEvents);
        connect(row, &QPushButton::clicked, this, [this, id = p.id]() { m_plans.setActive(id); });
        m_listLayout->addWidget(row);
    }
    if (shown == 0) {
        auto* e = ui::label(m_showArchived ? tr("No hay planes archivados.") : tr("No hay planes activos."), "muted");
        e->setContentsMargins(8, 8, 8, 8);
        m_listLayout->addWidget(e);
    }
    m_listLayout->addStretch(1);
}

// ---- Editor del plan activo ----------------------------------------------------------------

void PlanView::buildEditor(QHBoxLayout* root) {
    QWidget* content;
    QVBoxLayout* outer;
    auto* sa = ui::scrollArea(&content, &outer);
    outer->setContentsMargins(32, 28, 32, 28);
    m_editor = new QWidget;
    m_editor->setMaximumWidth(900);
    auto* v = ui::vbox(m_editor, 0, 18);
    outer->addWidget(m_editor, 0, Qt::AlignTop);
    root->addWidget(sa, 1);

    // Cabecera
    auto* head = new QWidget;
    auto* hh = ui::hbox(head, 0, 16);
    auto* titleBlock = new QWidget;
    auto* tv = ui::vbox(titleBlock, 0, 0);
    m_eyebrow = ui::label(QString(), "eyebrow");
    tv->addWidget(m_eyebrow);
    auto* nameRow = new QWidget;
    auto* nh = ui::hbox(nameRow, 0, 10);
    m_name = new QLineEdit;
    m_name->setProperty("role", QStringLiteral("title"));
    m_name->setPlaceholderText(tr("Nombre del plan"));
    connect(m_name, &QLineEdit::textEdited, this, [this](const QString& t) { m_selfEdit = true; m_plans.setName(t); m_selfEdit = false; });
    nh->addWidget(m_name, 1);
    m_archivedBadge = mutedPill(tr("ARCHIVADO"));
    nh->addWidget(m_archivedBadge);
    auto* more = ui::button(QStringLiteral("⋯"), "outline");
    more->setFixedWidth(40);
    more->setToolTip(tr("Más acciones"));
    auto* menu = new QMenu(more);
    menu->addAction(tr("Duplicar plan"), this, &PlanView::duplicateActive);
    menu->addAction(tr("Archivar / desarchivar"), this, &PlanView::toggleArchiveActive);
    menu->addSeparator();
    menu->addAction(tr("Eliminar plan…"), this, &PlanView::removeActive);
    more->setMenu(menu);
    nh->addWidget(more);
    tv->addWidget(nameRow);
    hh->addWidget(titleBlock, 1);
    auto* stats = ui::card("card");
    auto* sh = ui::hbox(stats, 12, 16);
    sh->setContentsMargins(18, 12, 18, 12);
    m_count = new QLabel;
    m_steps = new QLabel;
    m_time = new QLabel;
    m_time->setStyleSheet(QStringLiteral("color:%1;").arg(theme::Amber));
    m_basis = ui::label(QString(), "muted-sm");
    m_basis->setStyleSheet(QStringLiteral("font-size:10.5px;"));
    sh->addWidget(stat(tr("Casos"), m_count));
    sh->addWidget(stat(tr("Pasos"), m_steps));
    sh->addWidget(stat(tr("Estimado"), m_time, m_basis));
    hh->addWidget(stats, 0, Qt::AlignTop);
    v->addWidget(head);

    // Ciclo actual
    m_cycleCard = ui::card("card");
    auto* ch = ui::hbox(m_cycleCard, 0, 0);
    ch->addWidget(ui::accentBar(theme::Blue));
    auto* cbody = new QWidget;
    auto* cv = ui::vbox(cbody, 0, 8);
    cv->setContentsMargins(18, 14, 18, 14);
    auto* ctop = new QWidget;
    auto* cth = ui::hbox(ctop, 0, 10);
    m_cycleTitle = ui::label(QString(), "eyebrow");
    cth->addWidget(m_cycleTitle, 1);
    m_cycleReport = ui::button(tr("Ver informe"), "outline");
    m_cycleReport->setStyleSheet(QStringLiteral("padding:5px 10px;font-size:12px;border-radius:8px;"));
    cth->addWidget(m_cycleReport);
    cv->addWidget(ctop);
    m_cycleSummary = new QLabel;
    cv->addWidget(m_cycleSummary);
    m_cycleCells = new ProgressCells;
    cv->addWidget(m_cycleCells);
    // Ocultos cuando no hay ciclo, pero reservan su sitio: así la tarjeta no cambia de altura.
    for (QWidget* w : {static_cast<QWidget*>(m_cycleCells), static_cast<QWidget*>(m_cycleReport)}) {
        QSizePolicy sp = w->sizePolicy();
        sp.setRetainSizeWhenHidden(true);
        w->setSizePolicy(sp);
    }
    ch->addWidget(cbody, 1);
    v->addWidget(m_cycleCard);

    // Acciones rápidas
    auto* quick = new QWidget;
    auto* qh = ui::hbox(quick, 0, 8);
    auto* all = ui::button(tr("Añadir todos"), "chip-lg");
    auto* none = ui::button(tr("Vaciar"), "chip-lg");
    auto* high = ui::button(tr("Solo prioridad alta"), "chip-lg");
    auto* sort = ui::button(tr("Ordenar por prioridad"), "chip-lg");
    connect(all, &QPushButton::clicked, this, [this]() { m_plans.selectAll(); });
    connect(none, &QPushButton::clicked, this, [this]() { m_plans.selectNone(); });
    connect(high, &QPushButton::clicked, this, [this]() { m_plans.selectHighPriority(); });
    connect(sort, &QPushButton::clicked, this, [this]() { m_plans.sortByPriority(); });
    qh->addWidget(all);
    qh->addWidget(none);
    qh->addWidget(high);
    qh->addWidget(sort);
    qh->addStretch(1);
    v->addWidget(quick);

    // Casos en el plan (ordenados) y disponibles
    m_inPlanHeader = ui::label(QString(), "eyebrow");
    v->addWidget(m_inPlanHeader);
    auto* inPlan = new QWidget;
    m_inPlan = ui::vbox(inPlan, 0, 6);
    v->addWidget(inPlan);
    m_availableHeader = ui::label(QString(), "eyebrow");
    v->addWidget(m_availableHeader);
    auto* available = new QWidget;
    m_available = ui::vbox(available, 0, 6);
    v->addWidget(available);

    auto* footer = new QWidget;
    auto* fh = ui::hbox(footer, 0, 0);
    fh->addStretch(1);
    m_start = ui::button(tr("▶ Iniciar ciclo"), "success");
    m_start->setStyleSheet(QStringLiteral("padding:10px 18px;font-size:13.5px;font-weight:800;"));
    connect(m_start, &QPushButton::clicked, this, [this]() {
        const TestPlan* p = m_plans.active();
        if (!p) return;
        const QStringList ids = m_plans.orderedCaseIds();
        if (ids.isEmpty()) { emit toast(tr("Añade al menos un caso al plan"), theme::Amber); return; }
        emit startPlanRequested(ids, p->name, p->id);
    });
    fh->addWidget(m_start);
    v->addWidget(footer);
}

void PlanView::refreshEditor() {
    const TestPlan* p = m_plans.active();
    m_editor->setVisible(p != nullptr);
    if (!p) return;
    const int cycles = m_plans.cycleCount(p->id);
    m_eyebrow->setText(tr("%1 · CREADO %2 · %3").arg(p->id, p->createdAt.isValid() ? p->createdAt.toString(QStringLiteral("dd/MM/yyyy")) : QStringLiteral("—"),
                                                                 cycles == 1 ? tr("1 CICLO") : tr("%1 CICLOS").arg(cycles)));
    if (!m_selfEdit && m_name->text() != p->name) { m_name->setText(p->name); m_name->setCursorPosition(0); }
    m_archivedBadge->setVisible(p->archived);
    m_count->setText(QString::number(m_plans.orderedCaseIds().size()));
    m_steps->setText(QString::number(m_plans.totalSteps()));
    m_time->setText(m_plans.estimatedTime());
    m_basis->setText(m_plans.estimateBasis());
    m_start->setEnabled(!p->archived);
    m_start->setToolTip(p->archived ? tr("Desarchiva el plan para ejecutarlo") : QString());
    refreshCycle();
    refreshRows();
}

void PlanView::refreshCycle() {
    const TestPlan* p = m_plans.active();
    if (!p) return;
    const auto cycle = m_plans.latestCycle(p->id);
    m_cycleReport->disconnect();
    if (!cycle) {
        m_cycleTitle->setText(tr("CICLO ACTUAL"));
        m_cycleSummary->setText(tr("Este plan aún no se ha ejecutado. Pulsa «Iniciar ciclo» para empezar."));
        m_cycleCells->setColors({});
        m_cycleCells->hide();
        m_cycleReport->hide();
        return;
    }
    const PlanReport& r = *cycle;
    m_cycleTitle->setText(QStringLiteral("%1 · %2 · %3").arg(r.plan.isFinished() ? tr("ÚLTIMO CICLO") : tr("CICLO EN CURSO"), r.plan.id, when(r.plan.startedAt)));
    m_cycleSummary->setText(QStringLiteral("<b>%1/%2 ejecutados</b> · <span style=\"color:%7\">%3 ✓</span> · <span style=\"color:%8\">%4 ✗</span> · <span style=\"color:%9\">%5 bloq.</span> · %6 % de éxito")
                                .arg(r.executed).arg(r.total()).arg(r.passed).arg(r.failed).arg(r.blocked).arg(r.successRate())
                                .arg(theme::Green, theme::Red, theme::Amber));
    QStringList colors;
    for (const auto& row : r.rows) colors << (row.executed ? verdictColor(row.run.verdict) : theme::Border);
    m_cycleCells->setColors(colors);
    m_cycleCells->show();
    m_cycleReport->show();
    connect(m_cycleReport, &QPushButton::clicked, this, [this, id = r.plan.id]() { emit cycleReportRequested(id); });
}

void PlanView::refreshRows() {
    const TestPlan* p = m_plans.active();
    if (!p) return;
    const QStringList ordered = m_plans.orderedCaseIds();
    const auto cycle = m_plans.latestCycle(p->id);

    ui::clearLayout(m_inPlan);
    m_inPlanHeader->setText(tr("EN EL PLAN · %1 · EN ORDEN DE EJECUCIÓN").arg(ordered.size()));
    if (ordered.isEmpty()) m_inPlan->addWidget(ui::label(tr("Ningún caso todavía. Añade casos de la lista de abajo."), "muted-sm"));
    for (int i = 0; i < ordered.size(); ++i) {
        const TestCase* c = m_cases.find(ordered[i]);
        if (!c) continue;
        auto* row = ui::card("card");
        auto* g = ui::hbox(row, 0, 8);
        g->setContentsMargins(12, 8, 8, 8);
        auto* pos = ui::label(QStringLiteral("%1").arg(i + 1, 2, 10, QLatin1Char('0')), "mono-muted");
        pos->setFixedWidth(24);
        g->addWidget(pos);
        auto* id = ui::label(c->id, "mono-muted");
        id->setFixedWidth(54);
        g->addWidget(id);
        auto* title = new QLabel(c->title.isEmpty() ? tr("(sin título)") : c->title);
        title->setWordWrap(true);
        title->setStyleSheet(QStringLiteral("font-size:13.5px;font-weight:600;"));
        g->addWidget(title, 1);
        auto* suite = ui::label(c->suite, "muted-sm");
        suite->setFixedWidth(96);
        g->addWidget(suite);
        const auto pill = theme::priorityPill(toString(c->priority));
        auto* prio = ui::pill(label(c->priority), pill.bg, pill.fg);
        prio->setFixedWidth(54);
        g->addWidget(prio);
        auto* steps = ui::label(tr("%1 pasos").arg(c->steps.size()), "muted-sm");
        steps->setFixedWidth(52);
        g->addWidget(steps);
        // Resultado en el último ciclo
        QLabel* res = nullptr;
        if (cycle) for (const auto& r : cycle->rows) if (r.caseId == c->id) res = r.executed ? verdictPill(r.run.verdict) : mutedPill(tr("PENDIENTE"));
        if (!res) res = mutedPill(QStringLiteral("—"));
        res->setFixedWidth(78);
        g->addWidget(res);
        auto* up = ui::button(QStringLiteral("▲"), "icon-move");
        up->setEnabled(i > 0);
        up->setToolTip(tr("Ejecutar antes"));
        connect(up, &QPushButton::clicked, this, [this, cid = c->id]() { m_plans.moveCase(cid, -1); });
        auto* down = ui::button(QStringLiteral("▼"), "icon-move");
        down->setEnabled(i < ordered.size() - 1);
        down->setToolTip(tr("Ejecutar después"));
        connect(down, &QPushButton::clicked, this, [this, cid = c->id]() { m_plans.moveCase(cid, +1); });
        auto* remove = ui::button(QStringLiteral("×"), "icon");
        remove->setToolTip(tr("Quitar del plan"));
        connect(remove, &QPushButton::clicked, this, [this, cid = c->id]() { m_plans.toggle(cid); });
        for (auto* b : {up, down, remove}) b->setFixedSize(24, 22);
        g->addWidget(up);
        g->addWidget(down);
        g->addWidget(remove);
        m_inPlan->addWidget(row);
    }

    ui::clearLayout(m_available);
    int available = 0;
    for (const auto& c : m_cases.cases()) {
        if (c.status == CaseStatus::Obsoleto || p->contains(c.id)) continue;
        ++available;
        auto* row = ui::button(QString(), "plan-row");
        row->setToolTip(tr("Añadir al plan"));
        auto* g = ui::hbox(row, 0, 8);
        g->setContentsMargins(14, 8, 14, 8);
        auto* plus = ui::label(QStringLiteral("+"), "mono-muted");
        plus->setFixedWidth(24);
        plus->setStyleSheet(QStringLiteral("font-size:14px;color:%1;").arg(theme::Blue));
        g->addWidget(plus);
        auto* id = ui::label(c.id, "mono-muted");
        id->setFixedWidth(54);
        g->addWidget(id);
        auto* title = new QLabel(c.title.isEmpty() ? tr("(sin título)") : c.title);
        title->setStyleSheet(QStringLiteral("font-size:13.5px;font-weight:600;color:%1;").arg(theme::Text));
        g->addWidget(title, 1);
        auto* suite = ui::label(c.suite, "muted-sm");
        suite->setFixedWidth(96);
        g->addWidget(suite);
        const auto pill = theme::priorityPill(toString(c.priority));
        auto* prio = ui::pill(label(c.priority), pill.bg, pill.fg);
        prio->setFixedWidth(54);
        g->addWidget(prio);
        auto* steps = ui::label(tr("%1 pasos").arg(c.steps.size()), "muted-sm");
        steps->setFixedWidth(52);
        g->addWidget(steps);
        for (auto* child : row->findChildren<QWidget*>()) child->setAttribute(Qt::WA_TransparentForMouseEvents);
        connect(row, &QPushButton::clicked, this, [this, cid = c.id]() { m_plans.toggle(cid); });
        m_available->addWidget(row);
    }
    m_availableHeader->setText(tr("DISPONIBLES · %1").arg(available));
    if (available == 0) m_available->addWidget(ui::label(tr("Todos los casos están en el plan."), "muted-sm"));
}

// ---- Acciones ------------------------------------------------------------------------------

void PlanView::newPlan() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Nuevo plan"), tr("Nombre del plan:"), QLineEdit::Normal, tr("Regresión Sprint 15"), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    m_showArchived = false;
    m_plans.createPlan(name);
    emit toast(tr("Plan \"%1\" creado").arg(name), theme::Green);
}

void PlanView::duplicateActive() {
    const TestPlan* p = m_plans.active();
    if (!p) return;
    const QString name = p->name;
    m_showArchived = false;
    m_plans.duplicatePlan(p->id);
    emit toast(tr("Plan \"%1\" duplicado").arg(name), theme::Green);
}

void PlanView::toggleArchiveActive() {
    const TestPlan* p = m_plans.active();
    if (!p) return;
    const bool archive = !p->archived;
    m_showArchived = archive;
    m_plans.setArchived(p->id, archive);
    emit toast(archive ? tr("Plan archivado · sus ciclos siguen en el historial") : tr("Plan desarchivado"), theme::Cyan);
}

void PlanView::removeActive() {
    const TestPlan* p = m_plans.active();
    if (!p) return;
    QMessageBox box(QMessageBox::Warning, tr("Eliminar plan"), tr("¿Eliminar el plan \"%1\"?").arg(p->name), QMessageBox::NoButton, this);
    box.setInformativeText(tr("Los ciclos ya ejecutados se conservan en el historial. Si quieres guardarlo sin ejecutarlo, archívalo."));
    auto* del = box.addButton(tr("Eliminar"), QMessageBox::DestructiveRole);
    box.addButton(tr("Cancelar"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != del) return;
    m_plans.removePlan(p->id);
}

} // namespace qaflow
