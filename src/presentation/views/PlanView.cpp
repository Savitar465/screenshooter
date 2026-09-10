#include "PlanView.h"

#include "application/PlanStore.h"
#include "application/TestCaseStore.h"
#include "application/TestPublishService.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/FlowLayout.h"
#include "presentation/widgets/ProgressCells.h"
#include "presentation/widgets/Ui.h"
#include "presentation/widgets/ZephyrPublishFlow.h"

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
/// Ciclos que muestra inicialmente el historial al desplegarlo.
constexpr int kRecentCycles = 5;
} // namespace

PlanView::PlanView(TestCaseStore& cases, PlanStore& plans, TestPublishService* publish, QWidget* parent)
    : QWidget(parent), m_cases(cases), m_plans(plans), m_publish(publish) {
    auto* root = ui::hbox(this, 0, 0);
    buildListPane(root);
    buildEditor(root);

    connect(&m_plans, &PlanStore::plansChanged, this, [this]() { refreshList(); refreshEditor(); });
    connect(&m_plans, &PlanStore::planChanged, this, [this]() { refreshList(); refreshEditor(); });
    connect(&m_cases, &TestCaseStore::caseChanged, this, &PlanView::refreshRows);
    refreshList();
    refreshEditor();
}

void PlanView::refresh() {
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
    m_cycleCard->setObjectName(QStringLiteral("currentPlanCycle"));
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
    ch->addWidget(cbody, 1);
    v->addWidget(m_cycleCard);

    // Historial de ciclos: un resumen por ejecución del plan, la más reciente primero
    m_cyclesSection = new QWidget;
    auto* hv = ui::vbox(m_cyclesSection, 0, 6);
    m_cyclesHeader = ui::button(QString(), "outline");
    m_cyclesHeader->setObjectName(QStringLiteral("cycleHistoryToggle"));
    m_cyclesHeader->setCheckable(true);
    m_cyclesHeader->setStyleSheet(QStringLiteral("text-align:left;padding:10px 12px;"));
    connect(m_cyclesHeader, &QPushButton::toggled, this, [this]() { refreshCycles(); });
    hv->addWidget(m_cyclesHeader);
    m_cyclesContent = new QWidget;
    m_cyclesList = ui::vbox(m_cyclesContent, 0, 6);
    hv->addWidget(m_cyclesContent);
    v->addWidget(m_cyclesSection);

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
    if (!p) { m_displayedPlanId.clear(); return; }
    if (m_displayedPlanId != p->id) {
        m_displayedPlanId = p->id;
        m_allCycles = false;
        m_cyclesHeader->setChecked(false);
    }
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
    refreshCycles();
    refreshRows();
}

void PlanView::refreshCycle() {
    const TestPlan* p = m_plans.active();
    if (!p) return;
    const auto cycle = m_plans.latestCycle(p->id);
    m_cycleReport->disconnect();
    m_cycleCard->setVisible(cycle && !cycle->plan.isFinished());
    if (!cycle || cycle->plan.isFinished()) return;
    const PlanReport& r = *cycle;
    m_cycleTitle->setText(QStringLiteral("%1 · %2 · %3").arg(tr("CICLO EN CURSO"), r.plan.id, when(r.plan.startedAt)));
    m_cycleSummary->setText(tr("<b>%1/%2 ejecutados</b> · <span style=\"color:%7\">%3 ✓</span> · <span style=\"color:%8\">%4 ✗</span> · <span style=\"color:%9\">%5 bloq.</span> · %6 % de éxito")
                                .arg(r.executed).arg(r.total()).arg(r.passed).arg(r.failed).arg(r.blocked).arg(r.successRate())
                                .arg(theme::Green, theme::Red, theme::Amber));
    QStringList colors;
    for (const auto& row : r.rows) colors << (row.executed ? verdictColor(row.run.verdict) : theme::Border);
    m_cycleCells->setColors(colors);
    m_cycleCells->show();
    m_cycleReport->show();
    connect(m_cycleReport, &QPushButton::clicked, this, [this, id = r.plan.id]() { emit cycleReportRequested(id); });
}

void PlanView::refreshCycles() {
    const TestPlan* p = m_plans.active();
    if (!p) return;
    ui::clearLayout(m_cyclesList);
    QList<PlanReport> cycles = m_plans.cycles(p->id);
    const auto current = m_plans.latestCycle(p->id);
    if (current && !current->plan.isFinished()) {
        cycles.removeIf([&current](const PlanReport& r) { return r.plan.id == current->plan.id; });
    }
    const bool expanded = m_cyclesHeader->isChecked();
    m_cyclesContent->setVisible(expanded);
    m_cyclesSection->setVisible(!cycles.isEmpty());
    if (cycles.isEmpty()) return;
    const QString title = cycles.size() == 1 ? tr("HISTORIAL DE CICLOS · 1 CICLO") : tr("HISTORIAL DE CICLOS · %1 CICLOS").arg(cycles.size());
    m_cyclesHeader->setText(QStringLiteral("%1 %2").arg(expanded ? QStringLiteral("▾") : QStringLiteral("▸"), title));
    if (!expanded) return;

    const int shown = m_allCycles ? cycles.size() : qMin(cycles.size(), kRecentCycles);
    for (int i = 0; i < shown; ++i) {
        const PlanReport& r = cycles[i];
        const bool finished = r.plan.isFinished();
        auto* row = ui::card("card");
        auto* g = ui::hbox(row, 0, 0);
        g->addWidget(ui::accentBar(finished ? verdictColor(r.verdict()) : theme::Blue));
        auto* body = new QWidget;
        auto* bv = ui::vbox(body, 0, 6);
        bv->setContentsMargins(14, 10, 14, 10);

        auto* top = new QWidget;
        auto* th = ui::hbox(top, 0, 8);
        auto* num = ui::label(QStringLiteral("#%1").arg(cycles.size() - i), "mono-muted");
        num->setFixedWidth(30);
        th->addWidget(num);
        th->addWidget(ui::label(r.plan.id, "mono-muted"));
        auto* date = ui::label(when(r.plan.startedAt), "muted-sm");
        th->addWidget(date);
        th->addStretch(1);
        if (r.plan.isPublished()) th->addWidget(ui::pill(tr("PUBLICADO"), theme::tint(theme::Cyan, 38), theme::Cyan));
        th->addWidget(finished ? verdictPill(r.verdict()) : mutedPill(tr("EN CURSO")));
        auto* report = ui::button(tr("Ver informe"), "outline");
        report->setStyleSheet(QStringLiteral("padding:3px 8px;font-size:11.5px;border-radius:7px;"));
        report->setToolTip(tr("Abrir el informe completo en el historial"));
        connect(report, &QPushButton::clicked, this, [this, id = r.plan.id]() { emit cycleReportRequested(id); });
        th->addWidget(report);
        bv->addWidget(top);

        // Resumen numérico y, si hay un ciclo anterior terminado, cómo cambió la tasa de éxito
        QString summary = tr("<b>%1/%2 ejecutados</b> · <span style=\"color:%7\">%3 ✓</span> · <span style=\"color:%8\">%4 ✗</span> · <span style=\"color:%9\">%5 bloq.</span> · %6 % de éxito")
                              .arg(r.executed).arg(r.total()).arg(r.passed).arg(r.failed).arg(r.blocked).arg(r.successRate())
                              .arg(theme::Green, theme::Red, theme::Amber);
        if (r.durationSecs > 0) summary += QStringLiteral(" · %1").arg(formatDuration(r.durationSecs));
        if (finished && i + 1 < cycles.size() && cycles[i + 1].plan.isFinished()) {
            const int delta = r.successRate() - cycles[i + 1].successRate();
            const QString color = delta > 0 ? theme::Green : delta < 0 ? theme::Red : theme::Muted;
            const QString text = delta > 0 ? tr("▲ +%1 pts").arg(delta) : delta < 0 ? tr("▼ %1 pts").arg(delta) : tr("= igual");
            summary += QStringLiteral(" · <span style=\"color:%1\">%2</span>").arg(color, text);
        }
        auto* sum = new QLabel(summary);
        sum->setStyleSheet(QStringLiteral("font-size:12.5px;color:%1;").arg(theme::Text));
        bv->addWidget(sum);

        auto* cells = new ProgressCells;
        QStringList colors, results;
        for (const auto& c : r.rows) {
            colors << (c.executed ? verdictColor(c.run.verdict) : theme::Border);
            results << QStringLiteral("%1 · %2 · %3").arg(c.caseId, c.title.isEmpty() ? tr("(sin título)") : c.title, c.executed ? label(c.run.verdict) : tr("Pendiente"));
        }
        cells->setColors(colors);
        bv->addWidget(cells);
        // Los resultados caso a caso, sin salir de la pantalla
        row->setToolTip(results.join(QLatin1Char('\n')));
        if (auto* zephyr = zephyrBlock(r)) bv->addWidget(zephyr);

        g->addWidget(body, 1);
        m_cyclesList->addWidget(row);
    }
    if (cycles.size() > kRecentCycles) {
        auto* foot = new QWidget;
        auto* fh = ui::hbox(foot, 0, 0);
        auto* more = ui::button(m_allCycles ? tr("Mostrar solo los últimos %1").arg(kRecentCycles) : tr("Mostrar los %1 ciclos").arg(cycles.size()), "chip-lg");
        connect(more, &QPushButton::clicked, this, [this]() { m_allCycles = !m_allCycles; refreshCycles(); });
        fh->addWidget(more);
        fh->addStretch(1);
        m_cyclesList->addWidget(foot);
    }
}

QWidget* PlanView::zephyrBlock(const PlanReport& r) {
    const PlanRun& plan = r.plan;
    const bool canPublish = m_publish && m_publish->enabled() && plan.isFinished() && r.executed > 0;
    if (!plan.isPublished() && !canPublish) return nullptr;
    auto* block = new QWidget;
    auto* v = ui::vbox(block, 0, 4);
    v->setContentsMargins(0, 4, 0, 0);
    auto* head = new QWidget;
    auto* hh = ui::hbox(head, 0, 8);
    const QString small = QStringLiteral("padding:3px 8px;font-size:11.5px;border-radius:7px;");
    auto flow = [this, r](bool update) {
        ZephyrPublishFlow::run(this, *m_publish, r, update, [this](const QString& m, const QString& c) { emit toast(m, c); });
    };
    if (plan.isPublished()) {
        auto* published = ui::label(tr("Publicado en Zephyr el %1 · ciclo %2").arg(when(plan.publishedAt), plan.zephyrCycleId), "muted-sm");
        published->setStyleSheet(QStringLiteral("color:%1;").arg(theme::Green));
        hh->addWidget(published, 1);
        if (const QString url = m_publish ? m_publish->cycleUrl(r) : QString(); !url.isEmpty()) {
            auto* open = ui::button(tr("Abrir en Jira"), "chip");
            open->setObjectName(QStringLiteral("openZephyrCycle-%1").arg(plan.id));
            open->setToolTip(tr("Abre en el navegador las ejecuciones de este ciclo en Zephyr"));
            connect(open, &QPushButton::clicked, this, [this, url]() { emit openUrlRequested(url); });
            hh->addWidget(open);
        }
        if (canPublish) {
            auto* update = ui::button(tr("Actualizar en Zephyr"), "primary");
            update->setObjectName(QStringLiteral("updateZephyr-%1").arg(plan.id));
            update->setStyleSheet(small);
            update->setToolTip(tr("Vuelve a mandar al ciclo %1 de Zephyr el veredicto de cada caso y de cada paso, y sube las evidencias que falten").arg(plan.zephyrCycleId));
            connect(update, &QPushButton::clicked, this, [flow]() { flow(true); });
            hh->addWidget(update);
        }
    } else {
        hh->addWidget(ui::label(tr("Sin publicar en Zephyr"), "muted-sm"), 1);
        auto* publish = ui::button(tr("Publicar en Zephyr"), "outline");
        publish->setObjectName(QStringLiteral("publishZephyr-%1").arg(plan.id));
        publish->setStyleSheet(small);
        publish->setToolTip(tr("Crea el ciclo en Zephyr con estas ejecuciones, el veredicto de cada paso y sus evidencias"));
        connect(publish, &QPushButton::clicked, this, [flow]() { flow(false); });
        hh->addWidget(publish);
    }
    v->addWidget(head);
    if (!plan.isPublished()) return block;

    // Cada ejecución publicada con su propio Test: el de este ciclo, no el del último que se publicó.
    for (const auto& row : r.rows) {
        if (!row.executed) continue;
        auto* line = new QWidget;
        auto* lh = ui::hbox(line, 0, 8);
        auto* id = ui::label(row.caseId, "mono-muted");
        id->setFixedWidth(54);
        lh->addWidget(id);
        auto* title = ui::label(ui::elide(row.title.isEmpty() ? tr("(sin título)") : row.title, 70), "muted-sm");
        lh->addWidget(title, 1);
        if (row.testKey.trimmed().isEmpty()) {
            auto* none = mutedPill(tr("SIN TEST"));
            none->setToolTip(tr("Esta ejecución se quedó sin Test al publicar; «Actualizar en Zephyr» se lo crea"));
            lh->addWidget(none);
        } else {
            auto* test = ui::button(tr("Test %1").arg(row.testKey.trimmed()), "chip");
            test->setObjectName(QStringLiteral("cycleTest-%1").arg(row.run.id));
            test->setToolTip(tr("Abrir en Jira el Test de Zephyr creado para esta ejecución"));
            connect(test, &QPushButton::clicked, this, [this, key = row.testKey.trimmed()]() { emit openJiraRequested(key); });
            lh->addWidget(test);
        }
        v->addWidget(line);
    }
    return block;
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
