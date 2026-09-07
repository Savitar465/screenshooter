#include "HistoryView.h"

#include "application/RunHistoryStore.h"
#include "application/TestCaseStore.h"
#include "core/models/PlanReport.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/FlowLayout.h"
#include "presentation/widgets/ProgressCells.h"
#include "presentation/widgets/Ui.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
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
    return ui::pill(toString(v).toUpper(), verdictColor(v), v == Verdict::Fallido ? QStringLiteral("#ffffff") : theme::Bg);
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

HistoryView::HistoryView(TestCaseStore& cases, RunHistoryStore& history, QWidget* parent)
    : QWidget(parent), m_cases(cases), m_history(history) {
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
    hv->addWidget(ui::label(QStringLiteral("Historial"), "h1-sm"));

    m_searchBox = new QLineEdit;
    m_searchBox->setPlaceholderText(QStringLiteral("Buscar por plan, caso o ID…"));
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
        {Mode::All, QStringLiteral("Todo")}, {Mode::Plans, QStringLiteral("Planes")}, {Mode::Runs, QStringLiteral("Casos")}};
    for (const auto& [mode, text] : modes) {
        auto* b = ui::button(text, "chip");
        ui::setFlag(b, "active", mode == m_mode);
        connect(b, &QPushButton::clicked, this, [this, mode]() { m_mode = mode; refreshFilters(); refreshList(); });
        m_filterRow->addWidget(b);
    }
}

void HistoryView::refreshList() {
    ui::clearLayout(m_listLayout);
    const QString q = m_search.trimmed().toLower();

    QList<Entry> entries;
    if (m_mode != Mode::Runs)
        for (const auto& p : m_history.plans()) {
            if (!q.isEmpty() && !(p.name + p.id + p.caseIds.join(QLatin1Char(' '))).toLower().contains(q)) continue;
            entries.append(Entry{true, p.id, p.startedAt});
        }
    if (m_mode != Mode::Plans)
        for (const auto& r : m_history.runs()) {
            if (m_mode == Mode::All && !r.planRunId.isEmpty()) continue; // dentro del plan
            if (!q.isEmpty() && !(r.caseTitle + r.caseId + r.id + r.suite).toLower().contains(q)) continue;
            entries.append(Entry{false, r.id, r.finishedAt});
        }
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.at > b.at; });

    if (entries.isEmpty()) {
        auto* e = ui::label(m_history.runs().isEmpty() ? QStringLiteral("Todavía no hay ejecuciones.") : QStringLiteral("Nada coincide con el filtro."), "muted");
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
            th->addWidget(ui::pill(QStringLiteral("PLAN"), QStringLiteral("rgba(110,168,254,38)"), theme::Blue));
            if (rep.plan.isFinished()) th->addWidget(verdictPill(rep.verdict()));
            else th->addWidget(ui::pill(QStringLiteral("EN CURSO"), QStringLiteral("rgba(154,167,180,38)"), theme::Muted));
            title->setText(rep.plan.name.isEmpty() ? QStringLiteral("(plan sin nombre)") : rep.plan.name);
            bottom->setText(QStringLiteral("%1 · %2/%3 casos · %4 %").arg(when(rep.plan.startedAt)).arg(rep.executed).arg(rep.total()).arg(rep.successRate()));
            ui::setFlag(row, "active", e.id == m_selectedPlan);
            connect(row, &QPushButton::clicked, this, [this, id = e.id]() { showPlan(id); });
        } else {
            th->addWidget(ui::label(r->caseId, "mono-muted"));
            th->addWidget(ui::label(QStringLiteral("· %1").arg(r->suite), "mono-muted"));
            th->addStretch(1);
            th->addWidget(verdictPill(r->verdict));
            title->setText(r->caseTitle.isEmpty() ? QStringLiteral("(sin título)") : r->caseTitle);
            QString info = QStringLiteral("%1 · %2/%3 pasos").arg(when(r->finishedAt)).arg(r->steps.size()).arg(r->plannedSteps);
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

void HistoryView::showPlan(const QString& planRunId) {
    m_selectedPlan = planRunId;
    m_selectedRun.clear();
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
    hv->addWidget(ui::label(QStringLiteral("HISTORIAL"), "eyebrow"));
    hv->addWidget(ui::label(QStringLiteral("Sin ejecuciones"), "h1"));
    m_detailLayout->addWidget(head);
    auto* e = ui::label(QStringLiteral("Ejecuta un caso o un plan de pruebas y su resultado quedará archivado aquí."), "muted");
    e->setWordWrap(true);
    m_detailLayout->addWidget(e);
}

void HistoryView::renderPlan(const PlanReport& report) {
    const PlanRun& plan = report.plan;

    // Cabecera + acciones
    auto* head = new QWidget;
    auto* hh = ui::hbox(head, 0, 16);
    auto* titleBlock = new QWidget;
    auto* tv = ui::vbox(titleBlock, 0, 2);
    tv->addWidget(ui::label(QStringLiteral("INFORME DE PLAN · %1 · %2").arg(plan.id, when(plan.startedAt)), "eyebrow"));
    auto* title = ui::label(plan.name.isEmpty() ? QStringLiteral("(plan sin nombre)") : plan.name, "h1");
    title->setWordWrap(true);
    tv->addWidget(title);
    auto* sub = new QWidget;
    auto* sh = ui::hbox(sub, 0, 8);
    if (plan.isFinished()) {
        sh->addWidget(verdictPill(report.verdict()));
        sh->addWidget(ui::label(QStringLiteral("Terminado el %1").arg(when(plan.finishedAt)), "muted-sm"));
    } else {
        sh->addWidget(ui::pill(QStringLiteral("EN CURSO"), QStringLiteral("rgba(154,167,180,38)"), theme::Muted));
        sh->addWidget(ui::label(QStringLiteral("La ejecución del plan no ha terminado"), "muted-sm"));
    }
    sh->addStretch(1);
    tv->addWidget(sub);
    hh->addWidget(titleBlock, 1);
    auto* exportBtn = ui::button(QStringLiteral("Exportar Markdown…"), "outline");
    connect(exportBtn, &QPushButton::clicked, this, [this, report]() { exportMarkdown(report); });
    auto* copyBtn = ui::button(QStringLiteral("Copiar"), "outline");
    copyBtn->setToolTip(QStringLiteral("Copiar el informe en Markdown al portapapeles"));
    connect(copyBtn, &QPushButton::clicked, this, [this, report]() { copyMarkdown(report); });
    hh->addWidget(exportBtn, 0, Qt::AlignTop);
    hh->addWidget(copyBtn, 0, Qt::AlignTop);
    m_detailLayout->addWidget(head);

    // Resumen
    auto* stats = ui::card("card");
    auto* sg = ui::hbox(stats, 18, 22);
    sg->setContentsMargins(20, 14, 20, 14);
    sg->addWidget(stat(QStringLiteral("Casos"), QString::number(report.total())));
    sg->addWidget(stat(QStringLiteral("Superados"), QString::number(report.passed), theme::Green));
    sg->addWidget(stat(QStringLiteral("Fallidos"), QString::number(report.failed), theme::Red));
    sg->addWidget(stat(QStringLiteral("Bloqueados"), QString::number(report.blocked), theme::Amber));
    sg->addWidget(stat(QStringLiteral("Pendientes"), QString::number(report.pending()), theme::Muted));
    sg->addWidget(stat(QStringLiteral("Éxito"), QStringLiteral("%1 %").arg(report.successRate()), theme::Blue));
    sg->addWidget(stat(QStringLiteral("Duración"), formatDuration(report.durationSecs)));
    sg->addStretch(1);
    m_detailLayout->addWidget(stats);

    QStringList colors;
    for (const auto& row : report.rows) colors << (row.executed ? verdictColor(row.run.verdict) : theme::Border);
    auto* cells = new ProgressCells;
    cells->setColors(colors);
    m_detailLayout->addWidget(cells);

    // Una tarjeta por caso del plan
    auto* rowsHead = ui::label(QStringLiteral("CASOS · %1").arg(report.total()), "eyebrow");
    m_detailLayout->addWidget(rowsHead);
    for (const auto& row : report.rows) {
        auto* card = ui::card("card");
        auto* cv = ui::vbox(card, 0, 10);
        cv->setContentsMargins(16, 12, 16, 12);
        auto* top = new QWidget;
        auto* th = ui::hbox(top, 0, 10);
        auto* idBtn = ui::button(row.caseId, "ghost");
        idBtn->setToolTip(QStringLiteral("Abrir el caso"));
        idBtn->setStyleSheet(QStringLiteral("padding:2px 6px;font-size:12px;font-weight:700;font-family:'Consolas','DejaVu Sans Mono',monospace;color:%1;").arg(theme::Blue));
        connect(idBtn, &QPushButton::clicked, this, [this, id = row.caseId]() { emit openCaseRequested(id); });
        th->addWidget(idBtn);
        auto* t = new QLabel(row.title.isEmpty() ? QStringLiteral("(sin título)") : row.title);
        t->setWordWrap(true);
        t->setStyleSheet(QStringLiteral("font-size:14px;font-weight:600;"));
        th->addWidget(t, 1);
        if (row.executed) {
            th->addWidget(ui::label(QStringLiteral("%1/%2 pasos · %3").arg(row.run.steps.size()).arg(row.run.plannedSteps).arg(formatDuration(row.run.durationSecs)), "muted-sm"));
            th->addWidget(verdictPill(row.run.verdict));
        } else {
            th->addWidget(ui::pill(QStringLiteral("PENDIENTE"), QStringLiteral("rgba(154,167,180,38)"), theme::Muted));
        }
        cv->addWidget(top);
        if (row.executed) cv->addWidget(stepsList(row.run));
        m_detailLayout->addWidget(card);
    }
}

void HistoryView::renderRun(const RunRecord& run) {
    auto* head = new QWidget;
    auto* hh = ui::hbox(head, 0, 16);
    auto* titleBlock = new QWidget;
    auto* tv = ui::vbox(titleBlock, 0, 2);
    QString eyebrow = QStringLiteral("EJECUCIÓN · %1 · %2 · %3").arg(run.id, run.caseId, run.suite.toUpper());
    if (const PlanRun* p = run.planRunId.isEmpty() ? nullptr : m_history.findPlan(run.planRunId)) eyebrow += QStringLiteral(" · PLAN %1").arg(p->name.toUpper());
    tv->addWidget(ui::label(eyebrow, "eyebrow"));
    auto* title = ui::label(run.caseTitle.isEmpty() ? QStringLiteral("(sin título)") : run.caseTitle, "h1");
    title->setWordWrap(true);
    tv->addWidget(title);
    auto* sub = new QWidget;
    auto* sh = ui::hbox(sub, 0, 8);
    sh->addWidget(verdictPill(run.verdict));
    sh->addWidget(ui::label(QStringLiteral("%1 → %2").arg(when(run.startedAt), when(run.finishedAt)), "muted-sm"));
    sh->addStretch(1);
    tv->addWidget(sub);
    hh->addWidget(titleBlock, 1);
    auto* open = ui::button(QStringLiteral("Abrir caso"), "outline");
    connect(open, &QPushButton::clicked, this, [this, id = run.caseId]() { emit openCaseRequested(id); });
    hh->addWidget(open, 0, Qt::AlignTop);
    if (!run.planRunId.isEmpty()) {
        auto* planBtn = ui::button(QStringLiteral("Ver informe del plan"), "outline");
        connect(planBtn, &QPushButton::clicked, this, [this, id = run.planRunId]() { m_mode = Mode::All; refreshFilters(); showPlan(id); });
        hh->addWidget(planBtn, 0, Qt::AlignTop);
    }
    m_detailLayout->addWidget(head);

    auto* stats = ui::card("card");
    auto* sg = ui::hbox(stats, 18, 22);
    sg->setContentsMargins(20, 14, 20, 14);
    sg->addWidget(stat(QStringLiteral("Pasos"), QStringLiteral("%1/%2").arg(run.steps.size()).arg(run.plannedSteps)));
    sg->addWidget(stat(QStringLiteral("Pasan"), QString::number(run.count(StepResult::Pass)), theme::Green));
    sg->addWidget(stat(QStringLiteral("Fallan"), QString::number(run.count(StepResult::Fail)), theme::Red));
    sg->addWidget(stat(QStringLiteral("Bloqueados"), QString::number(run.count(StepResult::Block)), theme::Amber));
    sg->addWidget(stat(QStringLiteral("N/A"), QString::number(run.count(StepResult::Skip)), theme::Muted));
    sg->addWidget(stat(QStringLiteral("Duración"), formatDuration(run.durationSecs)));
    sg->addStretch(1);
    m_detailLayout->addWidget(stats);

    auto* card = ui::card("card");
    auto* cv = ui::vbox(card, 0, 10);
    cv->setContentsMargins(16, 14, 16, 14);
    cv->addWidget(ui::label(QStringLiteral("REGISTRO"), "eyebrow"));
    cv->addWidget(stepsList(run));
    m_detailLayout->addWidget(card);
}

QWidget* HistoryView::stepsList(const RunRecord& run) const {
    auto* list = new QWidget;
    auto* lv = ui::vbox(list, 0, 6);
    if (run.steps.isEmpty()) {
        lv->addWidget(ui::label(QStringLiteral("No se ejecutó ningún paso."), "muted-sm"));
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
        a->setStyleSheet(QStringLiteral("color:#d0d8e0;"));
        g->addWidget(a, 0, 1);
        auto* secs = ui::label(formatDuration(s.durationSecs), "muted-sm");
        secs->setStyleSheet(QStringLiteral("font-size:11px;"));
        g->addWidget(secs, 0, 2, Qt::AlignTop);
        g->addWidget(ui::pill(toString(s.result).toUpper(), resultColor(s.result), s.result == StepResult::Fail ? QStringLiteral("#ffffff") : theme::Bg), 0, 3, Qt::AlignTop);
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
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Exportar informe"), suggested, QStringLiteral("Markdown (*.md)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) { emit toast(QStringLiteral("No se pudo escribir %1").arg(path), theme::Red); return; }
    f.write(report.toMarkdown().toUtf8());
    emit toast(QStringLiteral("Informe exportado a %1").arg(path), theme::Green);
}

void HistoryView::copyMarkdown(const PlanReport& report) {
    QApplication::clipboard()->setText(report.toMarkdown());
    emit toast(QStringLiteral("Informe copiado al portapapeles"), theme::Cyan);
}

} // namespace qaflow
