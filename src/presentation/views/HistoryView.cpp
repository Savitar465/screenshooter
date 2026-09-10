#include "HistoryView.h"

#include "application/EvidenceService.h"
#include "application/RunController.h"
#include "application/RunHistoryStore.h"
#include "application/TestPublishService.h"
#include "application/TestCaseStore.h"
#include "core/models/Metrics.h"
#include "core/models/PlanReport.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/EvidenceActions.h"
#include "presentation/widgets/FlowLayout.h"
#include "presentation/widgets/ShotCard.h"
#include "presentation/widgets/MetricBars.h"
#include "presentation/widgets/ProgressCells.h"
#include "presentation/widgets/Ui.h"
#include "presentation/widgets/ZephyrPublishFlow.h"

#include <QCoreApplication>
#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QStandardPaths>

#include <algorithm>

namespace qaflow {

namespace {

QString verdictColor(Verdict v) {
    switch (v) {
        case Verdict::Superado: return theme::Green;
        case Verdict::Fallido: return theme::Red;
        case Verdict::Bloqueado: return theme::Amber;
    }
    return theme::Muted;
}

QString resultColor(StepResult r) {
    switch (r) {
        case StepResult::Pass: return theme::Green;
        case StepResult::Fail: return theme::Red;
        case StepResult::Block: return theme::Amber;
        case StepResult::Skip: return theme::Muted;
    }
    return theme::Muted;
}

QLabel* verdictPill(Verdict v) {
    return ui::pill(label(v).toUpper(), verdictColor(v), v == Verdict::Fallido ? QStringLiteral("#ffffff") : theme::Bg);
}

QString when(const QDateTime& dt) { return dt.isValid() ? dt.toString(QStringLiteral("dd/MM/yyyy HH:mm")) : QStringLiteral("—"); }

QWidget* stat(const QString& title, const QString& value, const QString& color = QString()) {
    auto* w = new QWidget;
    auto* v = ui::vbox(w, 0, 0);
    v->addWidget(ui::label(title.toUpper(), "eyebrow"));
    auto* l = ui::label(value, "stat");
    if (!color.isEmpty()) l->setStyleSheet(QStringLiteral("color:%1;").arg(color));
    v->addWidget(l);
    return w;
}

/// Entrada de la lista izquierda: un plan o una ejecución suelta, ordenables por fecha.
struct Entry {
    bool isPlan = false;
    QString id;
    QDateTime at;
};

} // namespace

HistoryView::HistoryView(TestCaseStore& cases, RunHistoryStore& history, TestPublishService* publish,
                         EvidenceService* evidence, RunController* run, QWidget* parent)
    : QWidget(parent), m_cases(cases), m_history(history), m_publish(publish), m_evidence(evidence), m_run(run) {
    auto* root = ui::hbox(this, 0, 0);
    buildListPane(root);
    buildDetailPane(root);

    connect(&m_history, &RunHistoryStore::historyChanged, this, [this]() { refreshList(); refreshDetail(); });
    refreshFilters();
    refreshList();
    refreshDetail();
}

void HistoryView::buildListPane(QHBoxLayout* root) {
    auto* pane = ui::card("list-pane");
    pane->setMinimumWidth(260);
    pane->setMaximumWidth(320);
    pane->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* v = ui::vbox(pane, 0, 0);

    auto* head = new QWidget;
    auto* hv = ui::vbox(head, 16, 12);
    hv->setContentsMargins(16, 18, 16, 12);
    hv->addWidget(ui::label(tr("Historial"), "h1-sm"));

    m_searchBox = new QLineEdit;
    m_searchBox->setPlaceholderText(tr("Buscar por plan, caso o ID…"));
    m_searchBox->setClearButtonEnabled(true);
    connect(m_searchBox, &QLineEdit::textChanged, this, [this](const QString& t) { m_search = t; refreshList(); });
    hv->addWidget(m_searchBox);

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

void HistoryView::buildDetailPane(QHBoxLayout* root) {
    QWidget* content;
    QVBoxLayout* outer;
    auto* sa = ui::scrollArea(&content, &outer);
    outer->setContentsMargins(32, 28, 32, 28);
    auto* page = new QWidget;
    page->setMaximumWidth(900);
    m_detailLayout = ui::vbox(page, 0, 18);
    outer->addWidget(page, 0, Qt::AlignTop);
    root->addWidget(sa, 1);
}

void HistoryView::refreshFilters() {
    ui::clearLayout(m_filterRow);
    const std::pair<Mode, QString> modes[] = {
        {Mode::All, tr("Todo")}, {Mode::Plans, tr("Planes")}, {Mode::Runs, tr("Casos")}, {Mode::Metrics, tr("Métricas")}};
    for (const auto& [mode, text] : modes) {
        auto* b = ui::button(text, "chip");
        ui::setFlag(b, "active", mode == m_mode);
        connect(b, &QPushButton::clicked, this, [this, mode]() { m_mode = mode; refreshFilters(); refreshList(); refreshDetail(); });
        m_filterRow->addWidget(b);
    }
}

void HistoryView::refreshList() {
    ui::clearLayout(m_listLayout);
    const QString q = m_search.trimmed().toLower();

    QList<Entry> entries;
    if (m_mode == Mode::Metrics) {
        // En métricas la lista muestra los ciclos terminados, que son lo que compara el gráfico.
        for (const auto& p : m_history.plans()) if (p.isFinished()) entries.append(Entry{true, p.id, p.startedAt});
    }
    if (m_mode == Mode::All || m_mode == Mode::Plans)
        for (const auto& p : m_history.plans()) {
            if (!q.isEmpty() && !(p.name + p.id + p.caseIds.join(QLatin1Char(' '))).toLower().contains(q)) continue;
            entries.append(Entry{true, p.id, p.startedAt});
        }
    if (m_mode == Mode::All || m_mode == Mode::Runs)
        for (const auto& r : m_history.runs()) {
            if (m_mode == Mode::All && !r.planRunId.isEmpty()) continue; // dentro del plan
            if (!q.isEmpty() && !(r.caseTitle + r.caseId + r.id + r.suite).toLower().contains(q)) continue;
            entries.append(Entry{false, r.id, r.finishedAt});
        }
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.at > b.at; });

    if (entries.isEmpty()) {
        auto* e = ui::label(m_history.runs().isEmpty() ? tr("Todavía no hay ejecuciones.") : tr("Nada coincide con el filtro."), "muted");
        e->setWordWrap(true);
        e->setContentsMargins(8, 8, 8, 8);
        m_listLayout->addWidget(e);
        m_listLayout->addStretch(1);
        return;
    }

    for (const auto& e : entries) {
        const RunRecord* r = e.isPlan ? nullptr : m_history.findRun(e.id);
        if (!e.isPlan && !r) continue;
        auto* row = ui::button(QString(), "row");
        auto* v = ui::vbox(row, 0, 4);
        v->setContentsMargins(12, 10, 12, 10);
        auto* top = new QWidget;
        auto* th = ui::hbox(top, 0, 8);
        auto* title = new QLabel;
        title->setWordWrap(true);
        title->setStyleSheet(QStringLiteral("font-size:13.5px;font-weight:600;color:%1;").arg(theme::Text));
        auto* bottom = ui::label(QString(), "muted-sm");

        if (e.isPlan) {
            const PlanReport rep = m_history.report(e.id);
            th->addWidget(ui::label(e.id, "mono-muted"));
            th->addStretch(1);
            th->addWidget(ui::pill(QStringLiteral("PLAN"), theme::tint(theme::Blue, 38), theme::Blue));
            if (rep.plan.isFinished()) th->addWidget(verdictPill(rep.verdict()));
            else th->addWidget(ui::pill(tr("EN CURSO"), theme::tint(theme::Muted, 38), theme::Muted));
            title->setText(rep.plan.name.isEmpty() ? tr("(plan sin nombre)") : rep.plan.name);
            bottom->setText(tr("%1 · %2/%3 casos · %4 %").arg(when(rep.plan.startedAt)).arg(rep.executed).arg(rep.total()).arg(rep.successRate()));
            ui::setFlag(row, "active", e.id == m_selectedPlan);
            connect(row, &QPushButton::clicked, this, [this, id = e.id]() { showPlan(id); });
        } else {
            th->addWidget(ui::label(r->caseId, "mono-muted"));
            th->addWidget(ui::label(QStringLiteral("· %1").arg(r->suite), "mono-muted"));
            th->addStretch(1);
            th->addWidget(verdictPill(r->verdict));
            title->setText(r->caseTitle.isEmpty() ? tr("(sin título)") : r->caseTitle);
            QString info = tr("%1 · %2/%3 pasos").arg(when(r->finishedAt)).arg(r->steps.size()).arg(r->plannedSteps);
            if (const PlanRun* p = r->planRunId.isEmpty() ? nullptr : m_history.findPlan(r->planRunId)) info += QStringLiteral(" · ") + p->name;
            bottom->setText(info);
            ui::setFlag(row, "active", e.id == m_selectedRun);
            connect(row, &QPushButton::clicked, this, [this, id = e.id]() { showRun(id); });
        }
        v->addWidget(top);
        v->addWidget(title);
        v->addWidget(bottom);
        for (auto* child : row->findChildren<QWidget*>()) child->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_listLayout->addWidget(row);
    }
    m_listLayout->addStretch(1);
}

void HistoryView::refresh() {
    refreshList();
    refreshDetail();
}

void HistoryView::showPlan(const QString& planRunId) {
    if (m_mode == Mode::Metrics || m_mode == Mode::Runs) { m_mode = Mode::All; refreshFilters(); }
    m_selectedPlan = planRunId;
    m_selectedRun.clear();
    refreshList();
    refreshDetail();
}

void HistoryView::showMetrics() {
    m_mode = Mode::Metrics;
    refreshFilters();
    refreshList();
    refreshDetail();
}

void HistoryView::showRun(const QString& runId) {
    m_selectedRun = runId;
    m_selectedPlan.clear();
    refreshList();
    refreshDetail();
}

void HistoryView::showCase(const QString& caseId) {
    m_mode = Mode::Runs;
    refreshFilters();
    m_searchBox->setText(caseId); // dispara refreshList()
    const auto runs = m_history.runsForCase(caseId);
    if (!runs.isEmpty()) showRun(runs.first().id);
}

void HistoryView::refreshDetail() {
    ui::clearLayout(m_detailLayout);
    if (m_mode == Mode::Metrics) { renderMetrics(); return; }

    // Sin selección: el elemento más reciente (en empate, el plan).
    if (m_selectedPlan.isEmpty() && m_selectedRun.isEmpty()) {
        QDateTime best;
        for (const auto& r : m_history.runs()) if (r.planRunId.isEmpty() && (!best.isValid() || r.finishedAt > best)) { best = r.finishedAt; m_selectedRun = r.id; }
        for (const auto& p : m_history.plans()) if (!best.isValid() || p.startedAt >= best) { best = p.startedAt; m_selectedPlan = p.id; m_selectedRun.clear(); }
        if (!m_selectedPlan.isEmpty() || !m_selectedRun.isEmpty()) refreshList();
    }

    if (!m_selectedPlan.isEmpty()) {
        if (m_history.findPlan(m_selectedPlan)) { renderPlan(m_history.report(m_selectedPlan)); return; }
        m_selectedPlan.clear();
    }
    if (!m_selectedRun.isEmpty()) {
        if (const RunRecord* r = m_history.findRun(m_selectedRun)) { renderRun(*r); return; }
        m_selectedRun.clear();
    }

    auto* head = new QWidget;
    auto* hv = ui::vbox(head, 0, 0);
    hv->addWidget(ui::label(tr("HISTORIAL"), "eyebrow"));
    hv->addWidget(ui::label(tr("Sin ejecuciones"), "h1"));
    m_detailLayout->addWidget(head);
    auto* e = ui::label(tr("Ejecuta un caso o un plan de pruebas y su resultado quedará archivado aquí."), "muted");
    e->setWordWrap(true);
    m_detailLayout->addWidget(e);
}

void HistoryView::renderPlan(const PlanReport& report) {
    const PlanRun& plan = report.plan;

    // Cabecera: el título con su estado y, debajo, las acciones en una fila que se ajusta al ancho
    // (con Zephyr son cinco botones, y en una pantalla estrecha aplastaban al título).
    auto* head = new QWidget;
    auto* hv = ui::vbox(head, 0, 10);
    auto* actions = new QWidget;
    auto* ah = new FlowLayout(actions, 0, 8, 8);
    auto* titleBlock = new QWidget;
    auto* tv = ui::vbox(titleBlock, 0, 2);
    tv->addWidget(ui::label(tr("INFORME DE PLAN · %1 · %2").arg(plan.id, when(plan.startedAt)), "eyebrow"));
    auto* title = ui::label(plan.name.isEmpty() ? tr("(plan sin nombre)") : plan.name, "h1");
    title->setWordWrap(true);
    tv->addWidget(title);
    auto* sub = new QWidget;
    auto* sh = ui::hbox(sub, 0, 8);
    if (plan.isFinished()) {
        sh->addWidget(verdictPill(report.verdict()));
        sh->addWidget(ui::label(tr("Terminado el %1").arg(when(plan.finishedAt)), "muted-sm"));
    } else {
        sh->addWidget(ui::pill(tr("EN CURSO"), theme::tint(theme::Muted, 38), theme::Muted));
        sh->addWidget(ui::label(tr("La ejecución del plan no ha terminado"), "muted-sm"));
    }
    sh->addStretch(1);
    tv->addWidget(sub);
    // Dónde acabaron estos resultados: sin esto, saber si un ciclo ya se publicó era mirarlo en Jira.
    // En su propia línea y con ajuste de texto: apretada contra los botones de la cabecera se cortaba.
    if (plan.isPublished()) {
        const QString texto = plan.publishedAt.isValid()
                                  ? tr("Publicado en Zephyr el %1 · ciclo %2").arg(when(plan.publishedAt), plan.zephyrCycleId)
                                  : tr("Publicado en Zephyr · ciclo %1").arg(plan.zephyrCycleId);
        auto* published = ui::label(texto, "muted-sm");
        published->setObjectName(QStringLiteral("planPublished"));
        published->setWordWrap(true);
        published->setStyleSheet(QStringLiteral("color:%1;").arg(theme::Green));
        auto* where = new QWidget;
        auto* wh = ui::hbox(where, 0, 8);
        wh->addWidget(published, 1);
        // Y un salto al ciclo en Jira, para ver allí las ejecuciones tal y como quedaron.
        if (const QString url = m_publish ? m_publish->cycleUrl(report) : QString(); !url.isEmpty()) {
            auto* open = ui::button(tr("Abrir el ciclo en Jira"), "chip");
            open->setObjectName(QStringLiteral("openZephyrCycle"));
            open->setToolTip(tr("Abre en el navegador las ejecuciones de este ciclo en Zephyr"));
            connect(open, &QPushButton::clicked, this, [this, url]() { emit openUrlRequested(url); });
            wh->addWidget(open, 0, Qt::AlignVCenter);
        }
        tv->addWidget(where);
    }
    hv->addWidget(titleBlock);
    auto* exportBtn = ui::button(tr("Exportar Markdown…"), "outline");
    connect(exportBtn, &QPushButton::clicked, this, [this, report]() { exportMarkdown(report); });
    auto* copyBtn = ui::button(tr("Copiar"), "outline");
    copyBtn->setToolTip(tr("Copiar el informe en Markdown al portapapeles"));
    connect(copyBtn, &QPushButton::clicked, this, [this, report]() { copyMarkdown(report); });
    ah->addWidget(exportBtn);
    ah->addWidget(copyBtn);
    // Sólo cuando el ciclo ha terminado: publicar uno a medias dejaría el ciclo incompleto en Zephyr.
    if (m_publish && m_publish->enabled() && plan.isFinished() && report.executed > 0) {
        // Ya publicado: lo normal es actualizar aquel ciclo; crear otro queda como opción secundaria.
        if (plan.isPublished()) {
            auto* updateBtn = ui::button(tr("Actualizar en Zephyr"), "primary");
            updateBtn->setObjectName(QStringLiteral("updateZephyr"));
            updateBtn->setToolTip(tr("Vuelve a mandar al ciclo %1 de Zephyr el veredicto de cada caso y de cada paso, y sube las evidencias que falten").arg(plan.zephyrCycleId));
            connect(updateBtn, &QPushButton::clicked, this, [this, report]() { updateInZephyr(report); });
            ah->addWidget(updateBtn);
        }
        auto* zephyrBtn = ui::button(plan.isPublished() ? tr("Publicar como ciclo nuevo") : tr("Publicar en Zephyr"), plan.isPublished() ? "outline" : "primary");
        zephyrBtn->setObjectName(QStringLiteral("publishZephyr"));
        zephyrBtn->setToolTip(tr("Crea el ciclo en Zephyr con estas ejecuciones, el veredicto de cada paso y sus evidencias"));
        connect(zephyrBtn, &QPushButton::clicked, this, [this, report]() { publishToZephyr(report); });
        ah->addWidget(zephyrBtn);
    }
    // El ciclo en curso no se elimina: la ejecución sigue escribiendo en él. Un ciclo sin terminar
    // que no es el actual quedó a medias (la sesión se perdió) y sí puede irse.
    const bool inProgress = !plan.isFinished() && (!m_run || m_run->planRunId() == plan.id);
    if (!inProgress) {
        auto* deleteBtn = ui::button(tr("Eliminar…"), "outline");
        deleteBtn->setObjectName(QStringLiteral("deletePlan"));
        deleteBtn->setToolTip(tr("Eliminar este informe del historial, con sus ejecuciones y evidencias"));
        deleteBtn->setStyleSheet(QStringLiteral("color:%1;").arg(theme::Red));
        connect(deleteBtn, &QPushButton::clicked, this, [this, report]() { deletePlan(report); });
        ah->addWidget(deleteBtn);
    }
    hv->addWidget(actions);
    m_detailLayout->addWidget(head);

    // Resumen
    auto* stats = ui::card("card");
    auto* sg = ui::hbox(stats, 18, 22);
    sg->setContentsMargins(20, 14, 20, 14);
    sg->addWidget(stat(tr("Casos"), QString::number(report.total())));
    sg->addWidget(stat(tr("Superados"), QString::number(report.passed), theme::Green));
    sg->addWidget(stat(tr("Fallidos"), QString::number(report.failed), theme::Red));
    sg->addWidget(stat(tr("Bloqueados"), QString::number(report.blocked), theme::Amber));
    sg->addWidget(stat(tr("Pendientes"), QString::number(report.pending()), theme::Muted));
    sg->addWidget(stat(tr("Éxito"), QStringLiteral("%1 %").arg(report.successRate()), theme::Blue));
    sg->addWidget(stat(tr("Duración"), formatDuration(report.durationSecs)));
    sg->addStretch(1);
    m_detailLayout->addWidget(stats);

    QStringList colors;
    for (const auto& row : report.rows) colors << (row.executed ? verdictColor(row.run.verdict) : theme::Border);
    auto* cells = new ProgressCells;
    cells->setColors(colors);
    m_detailLayout->addWidget(cells);

    // Una tarjeta por caso del plan
    auto* rowsHead = ui::label(tr("CASOS · %1").arg(report.total()), "eyebrow");
    m_detailLayout->addWidget(rowsHead);
    for (const auto& row : report.rows) {
        auto* card = ui::card("card");
        auto* cv = ui::vbox(card, 0, 10);
        cv->setContentsMargins(16, 12, 16, 12);
        auto* top = new QWidget;
        auto* th = ui::hbox(top, 0, 10);
        auto* idBtn = ui::button(row.caseId, "ghost");
        idBtn->setToolTip(tr("Abrir el caso"));
        idBtn->setStyleSheet(QStringLiteral("padding:2px 6px;font-size:12px;font-weight:700;font-family:'Consolas','DejaVu Sans Mono',monospace;color:%1;").arg(theme::Blue));
        connect(idBtn, &QPushButton::clicked, this, [this, id = row.caseId]() { emit openCaseRequested(id); });
        th->addWidget(idBtn);
        auto* t = new QLabel(row.title.isEmpty() ? tr("(sin título)") : row.title);
        t->setWordWrap(true);
        t->setStyleSheet(QStringLiteral("font-size:14px;font-weight:600;"));
        th->addWidget(t, 1);
        if (row.executed) {
            th->addWidget(ui::label(tr("%1/%2 pasos · %3").arg(row.run.steps.size()).arg(row.run.plannedSteps).arg(formatDuration(row.run.durationSecs)), "muted-sm"));
            th->addWidget(verdictPill(row.run.verdict));
        } else {
            th->addWidget(ui::pill(tr("PENDIENTE"), theme::tint(theme::Muted, 38), theme::Muted));
        }
        cv->addWidget(top);
        // Con qué está enlazado: la historia de Jira del caso y el Test que se creó para esta ejecución.
        if (auto* links = issueLinks(row.jiraKey, row.testKey)) cv->addWidget(links);
        if (row.executed) cv->addWidget(stepsList(row.run));
        if (row.executed)
            if (auto* shots = evidenceGrid(row.caseId, row.run.id, 4)) cv->addWidget(shots);
        m_detailLayout->addWidget(card);
    }
}

void HistoryView::renderMetrics() {
    const MetricsSummary sum = metrics::summary(m_cases.cases());
    const RunHistory history{m_history.runs(), m_history.plans()};
    const auto allCycles = metrics::cycles(history);

    auto* head = new QWidget;
    auto* hv = ui::vbox(head, 0, 2);
    hv->addWidget(ui::label(tr("MÉTRICAS"), "eyebrow"));
    hv->addWidget(ui::label(tr("Calidad por suite y evolución entre ciclos"), "h1"));
    auto* sub = ui::label(tr("La tasa por suite sale de la última ejecución de cada caso; la evolución compara los ciclos terminados de cada plan."), "muted-sm");
    sub->setWordWrap(true);
    hv->addWidget(sub);
    m_detailLayout->addWidget(head);

    const int rate = sum.successRate();
    auto* stats = ui::card("card");
    auto* sg = ui::hbox(stats, 18, 22);
    sg->setContentsMargins(20, 14, 20, 14);
    sg->addWidget(stat(tr("Casos"), QString::number(sum.cases)));
    sg->addWidget(stat(tr("Ejecutados"), QString::number(sum.executed())));
    sg->addWidget(stat(tr("Superados"), QString::number(sum.passed), theme::Green));
    sg->addWidget(stat(tr("Fallidos"), QString::number(sum.failed), theme::Red));
    sg->addWidget(stat(tr("Bloqueados"), QString::number(sum.blocked), theme::Amber));
    sg->addWidget(stat(tr("Tasa de éxito"), sum.executed() ? QStringLiteral("%1 %").arg(rate) : QStringLiteral("—"),
                       rate >= 80 ? theme::Green : rate >= 50 ? theme::Amber : theme::Red));
    sg->addWidget(stat(tr("Ciclos"), QString::number(allCycles.size())));
    sg->addStretch(1);
    m_detailLayout->addWidget(stats);

    // Por suite
    auto* suitesCard = ui::card("card");
    auto* sv = ui::vbox(suitesCard, 0, 10);
    sv->setContentsMargins(16, 14, 16, 14);
    sv->addWidget(ui::label(tr("TASA DE ÉXITO POR SUITE"), "eyebrow"));
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(8);
    int rowIdx = 0;
    for (const auto& s : metrics::bySuite(m_cases.cases())) {
        auto* name = new QLabel(s.suite.isEmpty() ? tr("(sin suite)") : s.suite);
        name->setStyleSheet(QStringLiteral("font-weight:600;"));
        name->setMinimumWidth(140);
        grid->addWidget(name, rowIdx, 0);
        auto* bar = new RateBar;
        bar->setCounts(s.passed, s.failed, s.blocked, s.notRun());
        grid->addWidget(bar, rowIdx, 1);
        const int r = s.successRate();
        auto* pct = new QLabel(s.executed() ? QStringLiteral("%1 %").arg(r) : QStringLiteral("—"));
        pct->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        pct->setMinimumWidth(44);
        pct->setStyleSheet(QStringLiteral("font-weight:800;color:%1;").arg(!s.executed() ? theme::Muted : r >= 80 ? theme::Green : r >= 50 ? theme::Amber : theme::Red));
        grid->addWidget(pct, rowIdx, 2);
        auto* detail = ui::label(tr("%1 ✓ · %2 ✗ · %3 bloq. · %4 sin ejecutar · %5 casos").arg(s.passed).arg(s.failed).arg(s.blocked).arg(s.notRun()).arg(s.cases), "muted-sm");
        grid->addWidget(detail, rowIdx, 3);
        ++rowIdx;
    }
    grid->setColumnStretch(1, 1);
    if (rowIdx == 0) sv->addWidget(ui::label(tr("No hay casos."), "muted-sm"));
    else sv->addLayout(grid);
    m_detailLayout->addWidget(suitesCard);

    // Evolución entre ciclos
    auto* trendCard = ui::card("card");
    auto* tv = ui::vbox(trendCard, 0, 10);
    tv->setContentsMargins(16, 14, 16, 14);
    auto* trendHead = new QWidget;
    auto* th = ui::hbox(trendHead, 0, 8);
    th->addWidget(ui::label(tr("EVOLUCIÓN ENTRE CICLOS"), "eyebrow"), 1);
    tv->addWidget(trendHead);
    // Un chip por plan con ciclos terminados
    QStringList planIds;
    QMap<QString, QString> planNames;
    for (const auto& c : allCycles) if (!c.planId.isEmpty() && !planIds.contains(c.planId)) { planIds << c.planId; planNames[c.planId] = c.name; }
    if (!planIds.contains(m_metricsPlan)) m_metricsPlan.clear();
    auto* chips = new QWidget;
    auto* chipRow = new FlowLayout(chips, 0, 6, 6);
    auto addChip = [&](const QString& id, const QString& text) {
        auto* b = ui::button(text, "chip");
        ui::setFlag(b, "active", id == m_metricsPlan);
        connect(b, &QPushButton::clicked, this, [this, id]() { m_metricsPlan = id; refreshDetail(); });
        chipRow->addWidget(b);
    };
    addChip(QString(), tr("Todos los planes"));
    for (const auto& id : planIds) addChip(id, planNames.value(id));
    tv->addWidget(chips);
    const auto cycles = metrics::cycles(history, m_metricsPlan);
    auto* chart = new TrendChart;
    chart->setCycles(cycles);
    connect(chart, &TrendChart::cycleClicked, this, &HistoryView::showPlan);
    tv->addWidget(chart);
    if (const auto delta = metrics::trend(cycles)) {
        const QString sign = *delta > 0 ? QStringLiteral("▲ +%1").arg(*delta) : *delta < 0 ? QStringLiteral("▼ %1").arg(*delta) : QStringLiteral("= 0");
        auto* t = ui::label(tr("%1 puntos en el último ciclo (%2 %) frente al anterior (%3 %). Pulsa una barra para abrir su informe.")
                                .arg(sign).arg(cycles.last().successRate()).arg(cycles[cycles.size() - 2].successRate()), "muted-sm");
        t->setWordWrap(true);
        t->setStyleSheet(QStringLiteral("color:%1;").arg(*delta > 0 ? theme::Green : *delta < 0 ? theme::Red : theme::Muted));
        tv->addWidget(t);
    } else if (cycles.size() == 1) {
        tv->addWidget(ui::label(tr("Un solo ciclo terminado: la tendencia aparecerá con el siguiente."), "muted-sm"));
    }
    m_detailLayout->addWidget(trendCard);
}

void HistoryView::renderRun(const RunRecord& run) {
    auto* head = new QWidget;
    auto* hh = ui::hbox(head, 0, 16);
    auto* titleBlock = new QWidget;
    auto* tv = ui::vbox(titleBlock, 0, 2);
    QString eyebrow = tr("EJECUCIÓN · %1 · %2 · %3").arg(run.id, run.caseId, run.suite.toUpper());
    if (const PlanRun* p = run.planRunId.isEmpty() ? nullptr : m_history.findPlan(run.planRunId)) eyebrow += tr(" · PLAN %1").arg(p->name.toUpper());
    tv->addWidget(ui::label(eyebrow, "eyebrow"));
    auto* title = ui::label(run.caseTitle.isEmpty() ? tr("(sin título)") : run.caseTitle, "h1");
    title->setWordWrap(true);
    tv->addWidget(title);
    auto* sub = new QWidget;
    auto* sh = ui::hbox(sub, 0, 8);
    sh->addWidget(verdictPill(run.verdict));
    sh->addWidget(ui::label(QStringLiteral("%1 → %2").arg(when(run.startedAt), when(run.finishedAt)), "muted-sm"));
    // Dónde se publicó esta ejecución: el ciclo de Zephyr del plan al que pertenece.
    if (const PlanRun* p = run.planRunId.isEmpty() ? nullptr : m_history.findPlan(run.planRunId); p && p->isPublished()) {
        auto* cycle = ui::label(tr("· Zephyr · ciclo %1").arg(p->zephyrCycleId), "muted-sm");
        cycle->setObjectName(QStringLiteral("runZephyrCycle"));
        cycle->setStyleSheet(QStringLiteral("color:%1;").arg(theme::Green));
        cycle->setMinimumWidth(cycle->sizeHint().width());   // que no se lo coma la fila
        sh->addWidget(cycle);
    }
    sh->addStretch(1);
    tv->addWidget(sub);
    // Y con qué está enlazado: la historia del caso y el Test de Zephyr de esta ejecución.
    const TestCase* linked = m_cases.find(run.caseId);
    if (auto* links = issueLinks(linked ? linked->jiraKey : QString(), run.testKey)) tv->addWidget(links);
    hh->addWidget(titleBlock, 1);
    auto* open = ui::button(tr("Abrir caso"), "outline");
    connect(open, &QPushButton::clicked, this, [this, id = run.caseId]() { emit openCaseRequested(id); });
    hh->addWidget(open, 0, Qt::AlignTop);
    if (!run.planRunId.isEmpty()) {
        auto* planBtn = ui::button(tr("Ver informe del plan"), "outline");
        connect(planBtn, &QPushButton::clicked, this, [this, id = run.planRunId]() { m_mode = Mode::All; refreshFilters(); showPlan(id); });
        hh->addWidget(planBtn, 0, Qt::AlignTop);
    }
    m_detailLayout->addWidget(head);

    auto* stats = ui::card("card");
    auto* sg = ui::hbox(stats, 18, 22);
    sg->setContentsMargins(20, 14, 20, 14);
    sg->addWidget(stat(tr("Pasos"), QStringLiteral("%1/%2").arg(run.steps.size()).arg(run.plannedSteps)));
    sg->addWidget(stat(tr("Pasan"), QString::number(run.count(StepResult::Pass)), theme::Green));
    sg->addWidget(stat(tr("Fallan"), QString::number(run.count(StepResult::Fail)), theme::Red));
    sg->addWidget(stat(tr("Bloqueados"), QString::number(run.count(StepResult::Block)), theme::Amber));
    sg->addWidget(stat(QStringLiteral("N/A"), QString::number(run.count(StepResult::Skip)), theme::Muted));
    sg->addWidget(stat(tr("Duración"), formatDuration(run.durationSecs)));
    sg->addStretch(1);
    m_detailLayout->addWidget(stats);

    auto* card = ui::card("card");
    auto* cv = ui::vbox(card, 0, 10);
    cv->setContentsMargins(16, 14, 16, 14);
    cv->addWidget(ui::label(tr("REGISTRO"), "eyebrow"));
    cv->addWidget(stepsList(run));
    m_detailLayout->addWidget(card);

    // Lo que se capturó ejecutando: es de esta ejecución y aquí es donde se ve.
    if (auto* shots = evidenceGrid(run.caseId, run.id, 3)) {
        auto* box = ui::card("card");
        auto* bv = ui::vbox(box, 0, 10);
        bv->setContentsMargins(16, 14, 16, 14);
        bv->addWidget(shots);
        m_detailLayout->addWidget(box);
    }
}

QWidget* HistoryView::issueLinks(const QString& jiraKey, const QString& testKey) {
    const QString jira = jiraKey.trimmed();
    const QString test = testKey.trimmed();
    if (jira.isEmpty() && test.isEmpty()) return nullptr;
    auto* row = new QWidget;
    auto* h = ui::hbox(row, 0, 6);
    auto add = [&](const char* name, const QString& text, const QString& key, const QString& tip) {
        if (key.isEmpty()) return;
        auto* b = ui::button(QStringLiteral("%1 %2").arg(text, key), "chip");
        b->setObjectName(QString::fromLatin1(name));
        b->setToolTip(tip);
        connect(b, &QPushButton::clicked, this, [this, key]() { emit openJiraRequested(key); });
        h->addWidget(b);
    };
    add("linkJira", tr("Historia"), jira, tr("Abrir en Jira la historia enlazada al caso"));
    add("linkTest", tr("Test"), test, tr("Abrir en Jira el Test de Zephyr que se creó para esta ejecución al publicar el informe"));
    h->addStretch(1);
    return row;
}

QWidget* HistoryView::evidenceGrid(const QString& caseId, const QString& runId, int columns) {
    const TestCase* c = m_cases.find(caseId);
    if (!c) return nullptr;
    // Las evidencias son de la ejecución: se enseñan aquí, con el paso al que se asignaron.
    const QList<Screenshot> shots = c->shotsOfRun(runId);
    if (shots.isEmpty()) return nullptr;
    auto* box = new QWidget;
    auto* v = ui::vbox(box, 0, 8);
    v->addWidget(ui::label(tr("EVIDENCIAS · %1").arg(shots.size()), "eyebrow"));
    auto* gridBox = new QWidget;
    auto* grid = new QGridLayout(gridBox);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(10);
    for (int i = 0; i < shots.size(); ++i) {
        auto* card = new ShotCard(shots[i], c->steps, ShotCard::Layout::Grid);
        // La ejecución ya pasó: su evidencia se mira, se anota y se copia, pero no se reordena ni
        // se reasigna de paso, que la cambiaría después de haberse publicado.
        card->setReadOnly(true);
        if (m_evidence) evidence::wireCard(card, this, m_cases, *m_evidence, caseId);
        grid->addWidget(card, i / columns, i % columns);
    }
    for (int col = 0; col < columns; ++col) grid->setColumnStretch(col, 1);
    v->addWidget(gridBox);
    return box;
}

QWidget* HistoryView::stepsList(const RunRecord& run) const {
    auto* list = new QWidget;
    auto* lv = ui::vbox(list, 0, 6);
    if (run.steps.isEmpty()) {
        lv->addWidget(ui::label(tr("No se ejecutó ningún paso."), "muted-sm"));
        return list;
    }
    for (int i = 0; i < run.steps.size(); ++i) {
        const auto& s = run.steps[i];
        auto* row = ui::card("card-flat");
        auto* g = new QGridLayout(row);
        g->setContentsMargins(10, 8, 10, 8);
        g->setHorizontalSpacing(10);
        g->setVerticalSpacing(4);
        auto* n = ui::label(QString::number(i + 1), "mono-muted");
        n->setFixedWidth(22);
        g->addWidget(n, 0, 0, Qt::AlignTop);
        auto* a = new QLabel(s.action);
        a->setWordWrap(true);
        a->setStyleSheet(QStringLiteral("color:%1;").arg(theme::TextSoft));
        g->addWidget(a, 0, 1);
        auto* secs = ui::label(formatDuration(s.durationSecs), "muted-sm");
        secs->setStyleSheet(QStringLiteral("font-size:11px;"));
        g->addWidget(secs, 0, 2, Qt::AlignTop);
        g->addWidget(ui::pill(label(s.result).toUpper(), resultColor(s.result), s.result == StepResult::Fail ? QStringLiteral("#ffffff") : theme::Bg), 0, 3, Qt::AlignTop);
        if (!s.note.trimmed().isEmpty()) {
            auto* note = new QLabel(s.note.trimmed());
            note->setWordWrap(true);
            note->setStyleSheet(QStringLiteral("font-style:italic;color:%1;").arg(theme::Muted));
            g->addWidget(note, 1, 1, 1, 3);
        }
        g->setColumnStretch(1, 1);
        lv->addWidget(row);
    }
    return list;
}

void HistoryView::exportMarkdown(const PlanReport& report) {
    QString base = report.plan.name.trimmed().toLower();
    base.replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    if (base.isEmpty()) base = report.plan.id.toLower();
    const QString suggested = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                                  .filePath(QStringLiteral("informe-%1-%2.md").arg(base, report.plan.startedAt.toString(QStringLiteral("yyyyMMdd-HHmm"))));
    const QString path = QFileDialog::getSaveFileName(this, tr("Exportar informe"), suggested, tr("Markdown (*.md)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { emit toast(tr("No se pudo escribir %1").arg(path), theme::Red); return; }
    f.write(report.toMarkdown().toUtf8());
    emit toast(tr("Informe exportado a %1").arg(path), theme::Green);
}

void HistoryView::publishToZephyr(const PlanReport& report) {
    ZephyrPublishFlow::run(this, *m_publish, report, false, [this](const QString& m, const QString& c) { emit toast(m, c); });
}

void HistoryView::updateInZephyr(const PlanReport& report) {
    ZephyrPublishFlow::run(this, *m_publish, report, true, [this](const QString& m, const QString& c) { emit toast(m, c); });
}

void HistoryView::copyMarkdown(const PlanReport& report) {
    QApplication::clipboard()->setText(report.toMarkdown());
    emit toast(tr("Informe copiado al portapapeles"), theme::Cyan);
}

void HistoryView::deletePlan(const PlanReport& report) {
    const PlanRun& plan = report.plan;
    const QList<RunRecord> runs = m_history.runsForPlan(plan.id);
    int evidence = 0;
    for (const auto& r : runs) if (const TestCase* c = m_cases.find(r.caseId)) evidence += c->shotsOfRun(r.id).size();

    QMessageBox box(QMessageBox::Warning, tr("Eliminar informe"), tr("¿Eliminar el informe \"%1\" (%2)?").arg(plan.name, plan.id), QMessageBox::NoButton, this);
    QString detail;
    if (runs.isEmpty()) detail = tr("El ciclo no tiene ejecuciones.");
    else {
        detail = runs.size() == 1 ? tr("Se eliminará del historial su única ejecución") : tr("Se eliminarán del historial sus %1 ejecuciones").arg(runs.size());
        if (evidence == 1) detail += tr(" y la evidencia capturada en ella, que se borra del disco");
        else if (evidence > 1) detail += tr(" y las %1 evidencias capturadas en ellas, que se borran del disco").arg(evidence);
        detail += QStringLiteral(".");
    }
    if (plan.isPublished()) detail += tr(" El ciclo ya publicado en Zephyr no se toca.");
    detail += tr(" Esta acción no se puede deshacer.");
    box.setInformativeText(detail);
    auto* del = box.addButton(tr("Eliminar"), QMessageBox::DestructiveRole);
    box.addButton(tr("Cancelar"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != del) return;

    m_selectedPlan.clear();   // antes de borrar: al cambiar el historial la vista elige otro elemento
    if (!m_history.removePlanRun(plan.id)) return;
    emit toast(tr("Informe %1 eliminado").arg(plan.id), theme::Cyan);
}

} // namespace qaflow
