#include "PlanView.h"

#include "application/IssueStore.h"
#include "application/PlanStore.h"
#include "application/TestCaseStore.h"
#include "application/TestPublishService.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/FlowLayout.h"
#include "presentation/widgets/ProgressCells.h"
#include "presentation/widgets/Ui.h"
#include "presentation/widgets/ZephyrPublishFlow.h"

#include "core/Text.h"

#include <QCoreApplication>
#include <QComboBox>
#include <QSignalBlocker>
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
/// Etiqueta del issue que se prueba con el plan, en el color de la pantalla de issues.
QLabel* issuePill(const QString& text) { return ui::pill(text, theme::tint(theme::Cyan, 38), theme::Cyan); }
/// Largo máximo del título del issue dentro de su etiqueta: lo que no cabe se acorta con «…».
constexpr int kMaxTagTitle = 48;
QString when(const QDateTime& dt) { return dt.isValid() ? dt.toString(QStringLiteral("dd/MM/yyyy HH:mm")) : QStringLiteral("—"); }
/// Ciclos que muestra inicialmente el historial al desplegarlo.
constexpr int kRecentCycles = 5;
constexpr int kCasesPerPage = 10;
} // namespace

PlanView::PlanView(TestCaseStore& cases, PlanStore& plans, TestPublishService* publish, IssueStore* issues, QWidget* parent)
    : QWidget(parent), m_cases(cases), m_plans(plans), m_publish(publish), m_issues(issues) {
    auto* root = ui::hbox(this, 0, 0);
    buildListPane(root);
    buildEditor(root);

    connect(&m_plans, &PlanStore::plansChanged, this, [this]() { refreshList(); refreshEditor(); });
    connect(&m_plans, &PlanStore::planChanged, this, [this]() { refreshList(); refreshEditor(); });
    connect(&m_cases, &TestCaseStore::suitesChanged, this, &PlanView::refreshRows);
    connect(&m_cases, &TestCaseStore::caseChanged, this, &PlanView::refreshRows);
    // Vincular o desvincular un plan se hace desde la pantalla de issues: las etiquetas de aquí lo siguen.
    if (m_issues) connect(m_issues, &IssueStore::issuesChanged, this, [this]() { refreshList(); refreshIssueTags(); });
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
    // Un plan puede probar varios issues y la lista se recorre entera: se resuelve de una vez.
    QHash<QString, QStringList> issuesByPlan;
    if (m_issues) {
        for (const auto& issue : m_issues->issues())
            for (const auto& planId : issue.planIds) issuesByPlan[planId] << issue.id;
    }
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
        const QStringList linked = issuesByPlan.value(p.id);
        if (!linked.isEmpty()) {
            th->addWidget(issuePill(linked.size() == 1 ? linked.first() : tr("%1 +%2").arg(linked.first()).arg(linked.size() - 1)));
        }
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
    // Ejecutar el plan se hace desde donde se compone, que es donde se está cuando ya está listo.
    m_runPlan = ui::button(tr("▶ Ejecutar plan"), "primary");
    m_runPlan->setObjectName(QStringLiteral("planRun"));
    m_runPlan->setToolTip(tr("Arranca un ciclo del plan con sus casos, en este orden"));
    connect(m_runPlan, &QPushButton::clicked, this, [this]() {
        if (!m_plans.activeId().isEmpty()) emit runPlanRequested(m_plans.activeId());
    });
    nh->addWidget(m_runPlan);
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
    // A qué issue se le está probando el requerimiento: el plan se compone aquí, pero lo que da
    // sentido a sus casos está en el issue, a un clic.
    m_issueTags = new QWidget;
    m_issueTagsLayout = new FlowLayout(m_issueTags, 6, 6, 6);
    tv->addWidget(m_issueTags);
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
    auto* cycleResults = new QWidget;
    m_cycleResults = ui::vbox(cycleResults, 0, 0);
    cv->addWidget(cycleResults);
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

    m_caseSearch = new QLineEdit;
    m_caseSearch->setObjectName(QStringLiteral("planCaseSearch"));
    m_caseSearch->setPlaceholderText(tr("Buscar casos por ID, título, suite, etiqueta o componente…"));
    m_caseSearch->setClearButtonEnabled(true);
    connect(m_caseSearch, &QLineEdit::textChanged, this, [this]() {
        m_inPlanPager.page = m_availablePager.page = 0;
        refreshRows();
    });
    auto* searchRow = new QWidget;
    auto* searchLayout = ui::hbox(searchRow, 0, 8);
    searchLayout->addWidget(m_caseSearch, 1);
    m_suiteFilter = new QComboBox;
    m_suiteFilter->setObjectName(QStringLiteral("planSuiteFilter"));
    m_suiteFilter->setAccessibleName(tr("Filtrar por suite"));
    m_suiteFilter->setToolTip(tr("Filtrar por suite"));
    m_suiteFilter->setMinimumWidth(180);
    m_suiteFilter->setMaximumWidth(260);
    m_suiteFilter->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    connect(m_suiteFilter, &QComboBox::currentIndexChanged, this, [this]() {
        m_inPlanPager.page = m_availablePager.page = 0;
        refreshRows();
    });
    searchLayout->addWidget(m_suiteFilter);
    v->addWidget(searchRow);

    // Acciones rápidas
    auto* quick = new QWidget;
    auto* qh = ui::hbox(quick, 0, 8);
    auto* newCase = ui::button(tr("+ Nuevo caso"), "chip-lg");
    newCase->setObjectName(QStringLiteral("planNewCase"));
    newCase->setToolTip(tr("Crea un caso, lo añade a este plan y lo abre para escribir sus pasos"));
    connect(newCase, &QPushButton::clicked, this, &PlanView::newCaseInPlan);
    auto* all = ui::button(tr("Añadir todos"), "chip-lg");
    auto* none = ui::button(tr("Vaciar"), "chip-lg");
    auto* high = ui::button(tr("Solo prioridad alta"), "chip-lg");
    auto* sort = ui::button(tr("Ordenar por prioridad"), "chip-lg");
    connect(all, &QPushButton::clicked, this, [this]() { m_plans.selectAll(); });
    connect(none, &QPushButton::clicked, this, [this]() { m_plans.selectNone(); });
    connect(high, &QPushButton::clicked, this, [this]() { m_plans.selectHighPriority(); });
    connect(sort, &QPushButton::clicked, this, [this]() { m_plans.sortByPriority(); });
    qh->addWidget(newCase);
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
    v->addWidget(buildCasePager(m_inPlanPager, QStringLiteral("inPlan")));
    m_availableHeader = ui::label(QString(), "eyebrow");
    v->addWidget(m_availableHeader);
    auto* available = new QWidget;
    m_available = ui::vbox(available, 0, 6);
    v->addWidget(available);
    v->addWidget(buildCasePager(m_availablePager, QStringLiteral("available")));
}

void PlanView::refreshEditor() {
    const TestPlan* p = m_plans.active();
    m_editor->setVisible(p != nullptr);
    if (!p) { m_displayedPlanId.clear(); return; }
    if (m_displayedPlanId != p->id) {
        m_displayedPlanId = p->id;
        m_allCycles = false;
        m_inPlanPager.page = m_availablePager.page = 0;
        m_suiteFilter->setCurrentIndex(0);
        m_caseSearch->clear();
        m_cyclesHeader->setChecked(false);
    }
    const int cycles = m_plans.cycleCount(p->id);
    m_eyebrow->setText(tr("%1 · CREADO %2 · %3").arg(p->id, p->createdAt.isValid() ? p->createdAt.toString(QStringLiteral("dd/MM/yyyy")) : QStringLiteral("—"),
                                                                 cycles == 1 ? tr("1 CICLO") : tr("%1 CICLOS").arg(cycles)));
    if (!m_selfEdit && m_name->text() != p->name) { m_name->setText(p->name); m_name->setCursorPosition(0); }
    m_archivedBadge->setVisible(p->archived);
    refreshIssueTags();
    // Un plan archivado o sin casos no se puede ejecutar: el botón lo dice en vez de fallar al pulsarlo.
    const int caseCount = int(m_plans.orderedCaseIds().size());
    m_runPlan->setEnabled(!p->archived && caseCount > 0);
    m_runPlan->setToolTip(p->archived      ? tr("El plan está archivado")
                          : caseCount == 0 ? tr("El plan todavía no tiene casos que ejecutar")
                                           : tr("Arranca un ciclo del plan con sus casos, en este orden"));
    m_count->setText(QString::number(caseCount));
    m_steps->setText(QString::number(m_plans.totalSteps()));
    m_time->setText(m_plans.estimatedTime());
    m_basis->setText(m_plans.estimateBasis());
    refreshCycle();
    refreshCycles();
    refreshRows();
}

void PlanView::refreshIssueTags() {
    ui::clearLayout(m_issueTagsLayout);
    const TestPlan* p = m_plans.active();
    const QList<Issue> issues = m_issues && p ? m_issues->issuesForPlan(p->id) : QList<Issue>{};
    m_issueTags->setVisible(!issues.isEmpty());
    for (const auto& issue : issues) {
        // La clave del gestor sólo está si el issue se publicó; sin ella, la etiqueta es la del issue local.
        const QString key = issue.publication.key.trimmed();
        auto* tag = ui::button(QStringLiteral("%1 · %2").arg(issue.id, elideTitle(issue.title, kMaxTagTitle)), "chip");
        tag->setObjectName(QStringLiteral("planIssueTag-%1").arg(issue.id));
        tag->setCursor(Qt::PointingHandCursor);
        tag->setStyleSheet(QStringLiteral("color:%1;background:%2;border-color:%3;padding:3px 10px;")
                               .arg(theme::Cyan, theme::tint(theme::Cyan, 30), theme::tint(theme::Cyan, 90)));
        tag->setToolTip(key.isEmpty() ? tr("Prueba el issue %1 (%2) · abrirlo").arg(issue.id, label(issue.state))
                                      : tr("Prueba el issue %1 (%2) · %3 · abrirlo").arg(issue.id, label(issue.state), key));
        connect(tag, &QPushButton::clicked, this, [this, id = issue.id]() { emit openIssueRequested(id); });
        m_issueTagsLayout->addWidget(tag);
    }
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
    ui::clearLayout(m_cycleResults);
    if (auto* results = caseResults(r)) m_cycleResults->addWidget(results);
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
        // Lo que quedó roto se vuelve a probar sin repetir el plan entero.
        if (r.canContinue()) {
            auto* proceed = ui::button(tr("Continuar"), "outline");
            proceed->setObjectName(QStringLiteral("cycleContinue-%1").arg(r.plan.id));
            proceed->setStyleSheet(QStringLiteral("padding:3px 8px;font-size:11.5px;border-radius:7px;"));
            proceed->setToolTip(tr("Volver a ejecutar los %1 caso(s) fallado(s) o bloqueado(s) de este ciclo")
                                    .arg(r.brokenCaseIds().size()));
            connect(proceed, &QPushButton::clicked, this, [this, id = r.plan.id]() { emit continueCycleRequested(id); });
            th->addWidget(proceed);
        }
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
        QStringList colors;
        for (const auto& c : r.rows) colors << (c.executed ? verdictColor(c.run.verdict) : theme::Border);
        cells->setColors(colors);
        bv->addWidget(cells);
        // Los resultados caso a caso, sin salir de la pantalla: las celdas dicen cuántos fallaron,
        // pero no cuáles.
        if (auto* results = caseResults(r)) bv->addWidget(results);
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

QWidget* PlanView::caseResults(const PlanReport& r) {
    if (r.rows.isEmpty()) return nullptr;
    const QString cycleId = r.plan.id;
    const bool open = m_openResults.contains(cycleId);

    auto* block = new QWidget;
    auto* v = ui::vbox(block, 0, 4);
    v->setContentsMargins(0, 4, 0, 0);

    auto* toggle = ui::button(QStringLiteral("%1 %2").arg(open ? QStringLiteral("▾") : QStringLiteral("▸"),
                                                          tr("Resultados por caso · %1").arg(r.total())),
                              "chip");
    toggle->setObjectName(QStringLiteral("cycleResults-%1").arg(cycleId));
    toggle->setToolTip(tr("Ver qué dio cada caso de prueba en este ciclo"));
    connect(toggle, &QPushButton::clicked, this, [this, cycleId]() {
        if (!m_openResults.remove(cycleId)) m_openResults.insert(cycleId);
        refreshCycle();
        refreshCycles();
    });
    auto* head = new QWidget;
    auto* hh = ui::hbox(head, 0, 8);
    hh->addWidget(toggle);
    hh->addStretch(1);
    v->addWidget(head);
    if (!open) return block;

    for (int i = 0; i < r.rows.size(); ++i) {
        const PlanReportRow& row = r.rows[i];
        auto* line = ui::card("card-flat");
        line->setObjectName(QStringLiteral("cycleResultRow-%1-%2").arg(cycleId, row.caseId));
        auto* lh = ui::hbox(line, 0, 8);
        lh->setContentsMargins(10, 6, 10, 6);
        auto* num = ui::label(QStringLiteral("%1").arg(i + 1, 2, 10, QLatin1Char('0')), "mono-muted");
        num->setFixedWidth(22);
        lh->addWidget(num);
        auto* id = ui::button(row.caseId, "ghost");
        id->setToolTip(tr("Abrir el caso"));
        id->setStyleSheet(QStringLiteral("padding:2px 6px;font-size:12px;font-weight:700;font-family:'Consolas','DejaVu Sans Mono',monospace;color:%1;").arg(theme::Blue));
        connect(id, &QPushButton::clicked, this, [this, caseId = row.caseId]() { emit openCaseRequested(caseId); });
        lh->addWidget(id);
        auto* title = new QLabel(row.title.isEmpty() ? tr("(sin título)") : row.title);
        title->setWordWrap(true);
        title->setStyleSheet(QStringLiteral("font-size:12.5px;"));
        lh->addWidget(title, 1);
        if (!row.executed) {
            // Sin ejecución no hay nada que abrir: el caso no llegó a correr en este ciclo.
            lh->addWidget(mutedPill(tr("PENDIENTE")));
            v->addWidget(line);
            continue;
        }
        // Dónde se rompió: con eso se sabe si el caso cayó al principio o casi al final sin abrir nada.
        QString detail = tr("%1/%2 pasos").arg(row.run.steps.size()).arg(row.run.plannedSteps);
        if (const int broken = row.run.brokenStepIndex(); broken >= 0) detail += tr(" · rompió en el paso %1").arg(broken + 1);
        if (row.run.durationSecs > 0) detail += QStringLiteral(" · ") + formatDuration(row.run.durationSecs);
        lh->addWidget(ui::label(detail, "muted-sm"));
        // Lo que se encontró probando este caso en este ciclo: los bugs salen de las ejecuciones, así
        // que es en sus resultados donde se enseñan.
        for (const auto& bug : row.bugs) {
            auto* chip = ui::button(bug.key, "chip");
            chip->setObjectName(QStringLiteral("cycleResultBug-%1-%2").arg(cycleId, bug.key));
            chip->setToolTip(bug.title.isEmpty() ? tr("Abrir el bug en el gestor") : tr("%1 · abrir en el gestor").arg(bug.title));
            const QString color = bug.resolved ? theme::Green : theme::Red;
            chip->setStyleSheet(QStringLiteral("color:%1;background:%2;border-color:%3;padding:2px 8px;font-size:11px;")
                                    .arg(color, theme::tint(color, 30), theme::tint(color, 90)));
            connect(chip, &QPushButton::clicked, this, [this, url = bug.url, key = bug.key]() {
                if (!url.isEmpty()) emit openUrlRequested(url);
                else emit openJiraRequested(key);
            });
            lh->addWidget(chip);
        }
        lh->addWidget(verdictPill(row.run.verdict));
        // Los pasos y las evidencias de la ejecución están en el historial: aquí se salta a ellos.
        auto* detailBtn = ui::button(tr("Ver"), "outline");
        detailBtn->setObjectName(QStringLiteral("cycleResultOpen-%1-%2").arg(cycleId, row.caseId));
        detailBtn->setStyleSheet(QStringLiteral("padding:3px 8px;font-size:11.5px;border-radius:7px;"));
        detailBtn->setToolTip(tr("Ver los pasos y las evidencias de esta ejecución"));
        connect(detailBtn, &QPushButton::clicked, this, [this, runId = row.run.id]() { emit openRunRequested(runId); });
        lh->addWidget(detailBtn);
        v->addWidget(line);
    }
    return block;
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

QWidget* PlanView::buildCasePager(CasePager& pager, const QString& name) {
    auto* bar = new QWidget;
    auto* h = ui::hbox(bar, 0, 8);
    pager.summary = ui::label(QString(), "muted-sm");
    pager.summary->setObjectName(name + QStringLiteral("PageSummary"));
    pager.previous = ui::button(tr("Anterior"), "outline");
    pager.previous->setObjectName(name + QStringLiteral("PreviousPage"));
    pager.next = ui::button(tr("Siguiente"), "outline");
    pager.next->setObjectName(name + QStringLiteral("NextPage"));
    connect(pager.previous, &QPushButton::clicked, this, [this, &pager]() { --pager.page; refreshRows(); });
    connect(pager.next, &QPushButton::clicked, this, [this, &pager]() { ++pager.page; refreshRows(); });
    h->addWidget(pager.summary, 1);
    h->addWidget(pager.previous);
    h->addWidget(pager.next);
    return bar;
}

void PlanView::refreshCasePager(CasePager& pager, int count) {
    const int pages = qMax(1, (count + kCasesPerPage - 1) / kCasesPerPage);
    pager.page = qBound(0, pager.page, pages - 1);
    const int first = count == 0 ? 0 : pager.page * kCasesPerPage + 1;
    const int last = qMin(count, (pager.page + 1) * kCasesPerPage);
    pager.summary->setText(tr("%1–%2 de %3 casos · Página %4 de %5")
                              .arg(first).arg(last).arg(count).arg(pager.page + 1).arg(pages));
    pager.previous->setVisible(pages > 1);
    pager.next->setVisible(pages > 1);
    pager.previous->setEnabled(pager.page > 0);
    pager.next->setEnabled(pager.page + 1 < pages);
}

void PlanView::refreshRows() {
    const TestPlan* p = m_plans.active();
    if (!p) return;
    const QStringList ordered = m_plans.orderedCaseIds();

    // Actualizar las suites sin perder el filtro ni disparar refrescos recursivos.
    {
        const QSignalBlocker blocker(m_suiteFilter);
        const QVariant previous = m_suiteFilter->currentData();
        m_suiteFilter->clear();
        m_suiteFilter->addItem(tr("Todas las suites"));
        m_suiteFilter->addItem(tr("Sin suite"), QStringLiteral(""));
        for (const auto& suite : m_cases.suites()) m_suiteFilter->addItem(suite, suite);
        const int index = previous.isValid() ? m_suiteFilter->findData(previous) : 0;
        m_suiteFilter->setCurrentIndex(qMax(0, index));
        if (index < 0) m_inPlanPager.page = m_availablePager.page = 0;
    }
    const QString query = m_caseSearch->text().trimmed();
    const QVariant suite = m_suiteFilter->currentData();
    auto matches = [&query, &suite](const TestCase& c) {
        const bool matchesSuite = !suite.isValid() || (suite.toString().isEmpty() ? c.suite.trimmed().isEmpty() : c.suite == suite.toString());
        return matchesSuite && c.searchText().contains(query, Qt::CaseInsensitive);
    };
    QList<int> matchingPositions;
    for (int i = 0; i < ordered.size(); ++i) {
        const auto* c = m_cases.find(ordered[i]);
        if (c && matches(*c)) matchingPositions.append(i);
    }
    refreshCasePager(m_inPlanPager, matchingPositions.size());
    ui::clearLayout(m_inPlan);
    m_inPlanHeader->setText(tr("EN EL PLAN · %1 · EN ORDEN DE EJECUCIÓN").arg(ordered.size()));
    if (ordered.isEmpty()) m_inPlan->addWidget(ui::label(tr("Ningún caso todavía. Añade casos de la lista de abajo."), "muted-sm"));
    if (!ordered.isEmpty() && matchingPositions.isEmpty())
        m_inPlan->addWidget(ui::label(tr("No hay casos que coincidan con la búsqueda."), "muted-sm"));
    const int inPlanEnd = qMin(int(matchingPositions.size()), (m_inPlanPager.page + 1) * kCasesPerPage);
    for (int index = m_inPlanPager.page * kCasesPerPage; index < inPlanEnd; ++index) {
        const int i = matchingPositions[index];
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
        auto* up = ui::button(QStringLiteral("▲"), "icon-move");
        up->setEnabled(i > 0);
        up->setToolTip(tr("Ejecutar antes"));
        connect(up, &QPushButton::clicked, this, [this, cid = c->id]() { m_plans.moveCase(cid, -1); });
        auto* down = ui::button(QStringLiteral("▼"), "icon-move");
        down->setEnabled(i < ordered.size() - 1);
        down->setToolTip(tr("Ejecutar después"));
        connect(down, &QPushButton::clicked, this, [this, cid = c->id]() { m_plans.moveCase(cid, +1); });
        auto* remove = ui::button(QStringLiteral("×"), "icon");
        remove->setObjectName(QStringLiteral("removePlanCase-%1").arg(c->id));
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
    QList<const TestCase*> matchingAvailable;
    for (const auto& c : m_cases.cases()) {
        if (c.status == CaseStatus::Obsoleto || p->contains(c.id)) continue;
        ++available;
        if (matches(c)) matchingAvailable.append(&c);
    }
    refreshCasePager(m_availablePager, matchingAvailable.size());
    const int availableEnd = qMin(int(matchingAvailable.size()), (m_availablePager.page + 1) * kCasesPerPage);
    for (int index = m_availablePager.page * kCasesPerPage; index < availableEnd; ++index) {
        const auto& c = *matchingAvailable[index];
        auto* row = ui::button(QString(), "plan-row");
        row->setObjectName(QStringLiteral("addPlanCase-%1").arg(c.id));
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
    else if (matchingAvailable.isEmpty())
        m_available->addWidget(ui::label(tr("No hay casos que coincidan con la búsqueda."), "muted-sm"));
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

void PlanView::newCaseInPlan() {
    const TestPlan* plan = m_plans.active();
    if (!plan) return;
    if (plan->archived) {
        emit toast(tr("El plan está archivado: desarchívalo para añadirle casos"), theme::Amber);
        return;
    }
    // El caso nace dentro del plan: se crea, se añade al final y se abre para escribir sus pasos.
    // El id se copia antes de tocar los stores: crear el caso rehace la lista de planes y `plan` deja de valer.
    const QString planId = plan->id;
    const QString caseId = m_cases.createCase();
    m_plans.toggle(caseId);
    emit toast(tr("%1 creado y añadido a %2").arg(caseId, planId), theme::Green);
    emit openCaseRequested(caseId);
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
    // El id se copia antes de abrir el diálogo: mientras está abierto la lista de planes puede rehacerse.
    const QString planId = p->id;
    QMessageBox box(QMessageBox::Warning, tr("Eliminar plan"), tr("¿Eliminar el plan \"%1\"?").arg(p->name), QMessageBox::NoButton, this);
    box.setInformativeText(tr("Los ciclos ya ejecutados se conservan en el historial. Si quieres guardarlo sin ejecutarlo, archívalo."));
    auto* del = box.addButton(tr("Eliminar"), QMessageBox::DestructiveRole);
    box.addButton(tr("Cancelar"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != del) return;
    m_plans.removePlan(planId);
}

} // namespace qaflow
