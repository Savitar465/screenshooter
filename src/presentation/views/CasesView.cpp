#include "CasesView.h"

#include "application/CaseTransferService.h"
#include "application/RunController.h"
#include "application/RunHistoryStore.h"
#include "application/TestCaseStore.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/FlowLayout.h"
#include "presentation/widgets/ShotCard.h"
#include "presentation/widgets/TextArea.h"
#include "presentation/widgets/Ui.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardPaths>

namespace qaflow {

namespace {
QString statusColor(CaseStatus s) {
    switch (s) {
        case CaseStatus::Listo: return theme::Green;
        case CaseStatus::Borrador: return theme::Amber;
        case CaseStatus::Obsoleto: return theme::Muted;
    }
    return theme::Muted;
}
QString lastRunColor(const LastRun& lr) {
    if (lr.outcome == RunOutcome::Passed) return theme::Green;
    if (lr.outcome == RunOutcome::Failed) return theme::Red;
    if (lr.outcome == RunOutcome::Blocked) return theme::Amber;
    return theme::Muted;
}
QWidget* fieldCell(const QString& title, QWidget* field) {
    auto* cell = ui::card("grid-cell");
    auto* v = ui::vbox(cell, 12, 5);
    v->setContentsMargins(14, 12, 14, 12);
    v->addWidget(ui::label(title.toUpper(), "eyebrow"));
    v->addWidget(field);
    return cell;
}
QComboBox* filterBox(const QString& all, const QStringList& items) {
    auto* b = new QComboBox;
    b->addItem(all);
    b->addItems(items);
    b->setStyleSheet(QStringLiteral("font-size:11.5px;padding:3px 6px;"));
    return b;
}
QPushButton* smallButton(const QString& text, const char* role, const QString& tip = QString()) {
    auto* b = ui::button(text, role);
    b->setStyleSheet(QStringLiteral("padding:6px 12px;font-size:12.5px;border-radius:8px;"));
    if (!tip.isEmpty()) b->setToolTip(tip);
    return b;
}
} // namespace

CasesView::CasesView(TestCaseStore& store, RunController& run, RunHistoryStore& history, CaseTransferService& transfer, QWidget* parent)
    : QWidget(parent), m_store(store), m_run(run), m_history(history), m_transfer(transfer) {
    auto* root = ui::hbox(this, 0, 0);
    buildListPane(root);
    buildEditor(root);

    connect(&m_store, &TestCaseStore::casesChanged, this, [this]() { refreshFilters(); refreshList(); });
    connect(&m_store, &TestCaseStore::selectionChanged, this, [this](const QString&) { refreshList(); loadEditor(); });
    connect(&m_store, &TestCaseStore::caseChanged, this, &CasesView::onCaseChanged);
    connect(&m_run, &RunController::runChanged, this, &CasesView::refreshList);
    connect(&m_history, &RunHistoryStore::historyChanged, this, &CasesView::refreshHistory);
    refreshFilters();
    refreshList();
    loadEditor();
}

// ---- Lista ---------------------------------------------------------------------------------

void CasesView::buildListPane(QHBoxLayout* root) {
    auto* pane = ui::card("list-pane");
    pane->setMinimumWidth(260);
    pane->setMaximumWidth(320);
    pane->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* v = ui::vbox(pane, 0, 0);

    auto* head = new QWidget;
    auto* hv = ui::vbox(head, 16, 10);
    hv->setContentsMargins(16, 18, 16, 12);
    auto* titleRow = new QWidget;
    auto* th = ui::hbox(titleRow, 0, 6);
    th->addWidget(ui::label(QStringLiteral("Casos"), "h1-sm"), 1);
    auto* newBtn = smallButton(QStringLiteral("+ Nuevo"), "primary");
    connect(newBtn, &QPushButton::clicked, this, [this]() { m_store.createCase(); });
    th->addWidget(newBtn);
    auto* more = smallButton(QStringLiteral("⋯"), "outline", QStringLiteral("Importar y exportar"));
    more->setStyleSheet(QStringLiteral("padding:6px 8px;font-size:13px;border-radius:8px;"));
    more->setFixedWidth(30);
    auto* menu = new QMenu(more);
    menu->addAction(QStringLiteral("Importar casos (JSON o CSV)…"), this, &CasesView::importCases);
    menu->addSeparator();
    menu->addAction(QStringLiteral("Exportar todo a JSON…"), this, [this]() { exportCases(static_cast<int>(CaseTransferService::Format::Json)); });
    menu->addAction(QStringLiteral("Exportar todo a CSV…"), this, [this]() { exportCases(static_cast<int>(CaseTransferService::Format::Csv)); });
    menu->addAction(QStringLiteral("Exportar todo a Markdown…"), this, [this]() { exportCases(static_cast<int>(CaseTransferService::Format::Markdown)); });
    more->setMenu(menu);
    th->addWidget(more);
    hv->addWidget(titleRow);

    auto* search = new QLineEdit;
    search->setPlaceholderText(QStringLiteral("Buscar por título, ID, etiqueta, componente…"));
    search->setClearButtonEnabled(true);
    connect(search, &QLineEdit::textChanged, this, [this](const QString& t) { m_filter.text = t; refreshList(); });
    hv->addWidget(search);

    auto* filters = new QWidget;
    m_filterRow = new FlowLayout(filters, 0, 6, 6);
    hv->addWidget(filters);

    // Filtros por estado, prioridad y última ejecución
    auto* combos = new QWidget;
    auto* ch = ui::hbox(combos, 0, 6);
    m_statusFilter = filterBox(QStringLiteral("Estado"), {QStringLiteral("Listo"), QStringLiteral("Borrador"), QStringLiteral("Obsoleto")});
    m_priorityFilter = filterBox(QStringLiteral("Prioridad"), {QStringLiteral("Alta"), QStringLiteral("Media"), QStringLiteral("Baja")});
    m_outcomeFilter = filterBox(QStringLiteral("Ejecución"), {QStringLiteral("Pasó"), QStringLiteral("Falló"), QStringLiteral("Bloqueado"), QStringLiteral("Sin ejecutar")});
    connect(m_statusFilter, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_filter.status = i <= 0 ? std::nullopt : std::optional<CaseStatus>(statusFromString(m_statusFilter->currentText()));
        refreshList();
    });
    connect(m_priorityFilter, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_filter.priority = i <= 0 ? std::nullopt : std::optional<Priority>(priorityFromString(m_priorityFilter->currentText()));
        refreshList();
    });
    connect(m_outcomeFilter, &QComboBox::currentIndexChanged, this, [this](int i) {
        static const RunOutcome map[] = {RunOutcome::Passed, RunOutcome::Failed, RunOutcome::Blocked, RunOutcome::None};
        m_filter.outcome = i <= 0 ? std::nullopt : std::optional<RunOutcome>(map[i - 1]);
        refreshList();
    });
    ch->addWidget(m_statusFilter, 1);
    ch->addWidget(m_priorityFilter, 1);
    ch->addWidget(m_outcomeFilter, 1);
    hv->addWidget(combos);
    m_listCount = ui::label(QString(), "muted-sm");
    hv->addWidget(m_listCount);
    v->addWidget(head);

    QWidget* content;
    auto* sa = ui::scrollArea(&content, &m_listLayout);
    m_listLayout->setContentsMargins(10, 0, 10, 16);
    m_listLayout->setSpacing(4);
    v->addWidget(sa, 1);
    root->addWidget(pane);
}

void CasesView::refreshFilters() {
    ui::clearLayout(m_filterRow);
    const QStringList suites = m_store.suites();
    if (!suites.contains(m_filter.suite)) m_filter.suite.clear();
    QStringList chips{QStringLiteral("Todas")};
    chips << suites;
    for (const auto& s : chips) {
        const QString value = s == QStringLiteral("Todas") ? QString() : s;
        auto* b = ui::button(s, "chip");
        ui::setFlag(b, "active", value == m_filter.suite);
        connect(b, &QPushButton::clicked, this, [this, value]() { m_filter.suite = value; refreshFilters(); refreshList(); });
        m_filterRow->addWidget(b);
    }
    if (m_suiteBox) {
        const QString cur = m_suiteBox->currentText();
        m_suiteBox->blockSignals(true);
        m_suiteBox->clear();
        m_suiteBox->addItems(suites);
        if (!cur.isEmpty() && !suites.contains(cur)) m_suiteBox->addItem(cur);
        m_suiteBox->setCurrentText(cur);
        m_suiteBox->blockSignals(false);
    }
}

void CasesView::refreshList() {
    ui::clearLayout(m_listLayout);
    const QString runningId = m_run.isRunning() ? m_run.state().caseId : QString();
    int shown = 0;

    for (const auto& c : m_store.cases()) {
        if (!m_filter.matches(c)) continue;
        ++shown;

        auto* row = ui::button(QString(), "row");
        ui::setFlag(row, "running", c.id == runningId);
        ui::setFlag(row, "active", c.id == m_store.selectedId() && c.id != runningId);
        auto* v = ui::vbox(row, 0, 4);
        v->setContentsMargins(12, 10, 12, 10);

        auto* top = new QWidget;
        auto* th = ui::hbox(top, 0, 8);
        th->addWidget(ui::label(c.id, "mono-muted"));
        th->addWidget(ui::label(QStringLiteral("· %1").arg(c.suite.isEmpty() ? QStringLiteral("sin suite") : c.suite), "mono-muted"));
        th->addStretch(1);
        const auto pill = theme::priorityPill(toString(c.priority));
        th->addWidget(ui::pill(toString(c.priority), pill.bg, pill.fg));
        v->addWidget(top);

        auto* title = new QLabel(c.title.isEmpty() ? QStringLiteral("(sin título)") : c.title);
        title->setWordWrap(true);
        title->setStyleSheet(QStringLiteral("font-size:13.5px;font-weight:600;color:%1;").arg(theme::Text));
        v->addWidget(title);

        auto* bottom = new QWidget;
        auto* bh = ui::hbox(bottom, 0, 8);
        auto* status = new QLabel(QStringLiteral("%1 · %2 pasos · %3 capturas").arg(toString(c.status)).arg(c.steps.size()).arg(c.shots.size()));
        status->setStyleSheet(QStringLiteral("font-size:11.5px;font-weight:600;color:%1;").arg(statusColor(c.status)));
        bh->addWidget(status, 1);
        if (c.id == runningId) {
            auto* badge = ui::pill(QStringLiteral("● EN EJECUCIÓN"), theme::Green, theme::Bg);
            badge->setStyleSheet(badge->styleSheet() + QStringLiteral("font-size:10.5px;font-weight:800;"));
            bh->addWidget(badge);
        }
        v->addWidget(bottom);

        if (!c.tags.isEmpty() || !c.component.isEmpty() || !c.jiraKey.isEmpty()) {
            QStringList bits;
            if (!c.component.isEmpty()) bits << c.component;
            if (!c.jiraKey.isEmpty()) bits << c.jiraKey;
            for (const auto& t : c.tags) bits << QLatin1Char('#') + t;
            auto* meta = ui::label(ui::elide(bits.join(QStringLiteral("  ")), 48), "muted-sm");
            meta->setStyleSheet(QStringLiteral("font-size:11px;"));
            v->addWidget(meta);
        }

        for (auto* child : row->findChildren<QWidget*>()) child->setAttribute(Qt::WA_TransparentForMouseEvents);
        const QString id = c.id;
        connect(row, &QPushButton::clicked, this, [this, id]() { m_store.select(id); });
        m_listLayout->addWidget(row);
    }
    if (shown == 0) {
        auto* e = ui::label(m_store.cases().isEmpty() ? QStringLiteral("No hay casos. Crea uno o importa un archivo.") : QStringLiteral("Ningún caso coincide con los filtros."), "muted");
        e->setWordWrap(true);
        e->setContentsMargins(8, 8, 8, 8);
        m_listLayout->addWidget(e);
    }
    m_listCount->setText(m_filter.isEmpty() ? QStringLiteral("%1 casos").arg(shown)
                                            : QStringLiteral("%1 de %2 casos").arg(shown).arg(m_store.cases().size()));
    m_listLayout->addStretch(1);
}

// ---- Editor --------------------------------------------------------------------------------

void CasesView::buildEditor(QHBoxLayout* root) {
    QWidget* content;
    QVBoxLayout* outer;
    auto* sa = ui::scrollArea(&content, &outer);
    outer->setContentsMargins(24, 28, 24, 28);
    m_editor = new QWidget;
    m_editor->setMaximumWidth(860);
    auto* v = ui::vbox(m_editor, 0, 20);
    outer->addWidget(m_editor, 0, Qt::AlignTop);

    // Cabecera: id + título + acciones
    auto* head = new QWidget;
    auto* hh = ui::hbox(head, 0, 12);
    auto* titleBlock = new QWidget;
    auto* tv = ui::vbox(titleBlock, 0, 0);
    m_idLabel = ui::label(QString(), "eyebrow-mono");
    tv->addWidget(m_idLabel);
    m_title = new QLineEdit;
    m_title->setProperty("role", QStringLiteral("title"));
    m_title->setPlaceholderText(QStringLiteral("Título del caso"));
    connect(m_title, &QLineEdit::textEdited, this, [this](const QString& t) {
        edit([&]() { m_store.updateCase(m_store.selectedId(), [&](TestCase& c) { c.title = t; }); });
    });
    tv->addWidget(m_title);
    hh->addWidget(titleBlock, 1);
    auto* save = ui::button(QStringLiteral("Guardar"), "outline");
    connect(save, &QPushButton::clicked, this, [this]() {
        const TestCase* c = m_store.selected();
        if (!c) return;
        const QString id = c->id;
        m_store.updateCase(id, [](TestCase& tc) { if (tc.status == CaseStatus::Borrador && tc.readyToBeMarkedListo()) tc.status = CaseStatus::Listo; });
        emit toast(QStringLiteral("%1 guardado").arg(id), theme::Green);
    });
    auto* runBtn = ui::button(QStringLiteral("▶ Ejecutar"), "success");
    connect(runBtn, &QPushButton::clicked, this, [this]() { if (!m_store.selectedId().isEmpty()) emit runRequested(m_store.selectedId()); });
    auto* more = ui::button(QStringLiteral("⋯"), "outline");
    more->setToolTip(QStringLiteral("Más acciones"));
    more->setFixedWidth(40);
    auto* menu = new QMenu(more);
    menu->addAction(QStringLiteral("Duplicar caso"), this, &CasesView::duplicateSelected);
    menu->addAction(QStringLiteral("Ver historial de ejecuciones"), this, [this]() { if (!m_store.selectedId().isEmpty()) emit historyRequested(m_store.selectedId()); });
    menu->addSeparator();
    menu->addAction(QStringLiteral("Eliminar caso…"), this, &CasesView::removeSelected);
    more->setMenu(menu);
    hh->addWidget(save, 0, Qt::AlignTop);
    hh->addWidget(runBtn, 0, Qt::AlignTop);
    hh->addWidget(more, 0, Qt::AlignTop);
    v->addWidget(head);

    // Metadatos
    auto* meta = new QWidget;
    auto* mg = new QGridLayout(meta);
    mg->setContentsMargins(0, 0, 0, 0);
    mg->setSpacing(6);
    auto* suiteRow = new QWidget;
    auto* srh = ui::hbox(suiteRow, 0, 4);
    m_suiteBox = new QComboBox;
    srh->addWidget(m_suiteBox, 1);
    auto* addSuite = ui::button(QStringLiteral("+"), "icon-move");
    addSuite->setToolTip(QStringLiteral("Nueva suite"));
    addSuite->setFixedWidth(24);
    connect(addSuite, &QPushButton::clicked, this, &CasesView::newSuite);
    srh->addWidget(addSuite);
    m_priorityBox = new QComboBox;
    m_priorityBox->addItems({QStringLiteral("Alta"), QStringLiteral("Media"), QStringLiteral("Baja")});
    m_statusBox = new QComboBox;
    m_statusBox->addItems({QStringLiteral("Listo"), QStringLiteral("Borrador"), QStringLiteral("Obsoleto")});
    m_lastRun = new QLabel;
    m_lastRun->setStyleSheet(QStringLiteral("font-weight:600;padding:5px 0;"));
    connect(m_suiteBox, &QComboBox::currentTextChanged, this, [this](const QString& t) { edit([&]() { m_store.updateCase(m_store.selectedId(), [&](TestCase& c) { c.suite = t; }); }); });
    connect(m_priorityBox, &QComboBox::currentTextChanged, this, [this](const QString& t) { edit([&]() { m_store.updateCase(m_store.selectedId(), [&](TestCase& c) { c.priority = priorityFromString(t); }); }); });
    connect(m_statusBox, &QComboBox::currentTextChanged, this, [this](const QString& t) { edit([&]() { m_store.updateCase(m_store.selectedId(), [&](TestCase& c) { c.status = statusFromString(t); }); }); });
    mg->addWidget(fieldCell(QStringLiteral("Suite"), suiteRow), 0, 0);
    mg->addWidget(fieldCell(QStringLiteral("Prioridad"), m_priorityBox), 0, 1);
    mg->addWidget(fieldCell(QStringLiteral("Estado"), m_statusBox), 0, 2);
    mg->addWidget(fieldCell(QStringLiteral("Última ejecución"), m_lastRun), 0, 3);

    m_component = new QLineEdit;
    m_component->setPlaceholderText(QStringLiteral("p. ej. Carrito"));
    connect(m_component, &QLineEdit::textEdited, this, [this](const QString& t) { edit([&]() { m_store.updateCase(m_store.selectedId(), [&](TestCase& c) { c.component = t.trimmed(); }); }); });
    auto* jiraRow = new QWidget;
    auto* jrh = ui::hbox(jiraRow, 0, 4);
    m_jiraKey = new QLineEdit;
    m_jiraKey->setProperty("role", QStringLiteral("mono"));
    m_jiraKey->setPlaceholderText(QStringLiteral("SHOP-12"));
    connect(m_jiraKey, &QLineEdit::textEdited, this, [this](const QString& t) {
        edit([&]() { m_store.updateCase(m_store.selectedId(), [&](TestCase& c) { c.jiraKey = t.trimmed().toUpper(); }); });
        m_openJira->setEnabled(!t.trimmed().isEmpty());
    });
    jrh->addWidget(m_jiraKey, 1);
    m_openJira = ui::button(QStringLiteral("↗"), "icon-move");
    m_openJira->setToolTip(QStringLiteral("Abrir en Jira"));
    m_openJira->setFixedWidth(24);
    connect(m_openJira, &QPushButton::clicked, this, [this]() { if (const TestCase* c = m_store.selected(); c && !c->jiraKey.isEmpty()) emit openJiraRequested(c->jiraKey); });
    jrh->addWidget(m_openJira);
    m_tags = new QLineEdit;
    m_tags->setPlaceholderText(QStringLiteral("regresión, smoke…"));
    m_tags->setToolTip(QStringLiteral("Etiquetas separadas por comas"));
    connect(m_tags, &QLineEdit::textEdited, this, [this](const QString& t) { edit([&]() { m_store.updateCase(m_store.selectedId(), [&](TestCase& c) { c.tags = parseTags(t); }); }); });
    mg->addWidget(fieldCell(QStringLiteral("Componente"), m_component), 1, 0);
    mg->addWidget(fieldCell(QStringLiteral("Historia Jira"), jiraRow), 1, 1);
    mg->addWidget(fieldCell(QStringLiteral("Etiquetas"), m_tags), 1, 2, 1, 2);
    for (int i = 0; i < 4; ++i) mg->setColumnStretch(i, 1);
    v->addWidget(meta);

    // Precondiciones
    auto* preBlock = new QWidget;
    auto* pv = ui::vbox(preBlock, 0, 8);
    pv->addWidget(ui::label(QStringLiteral("PRECONDICIONES"), "eyebrow"));
    m_pre = new TextArea(2);
    m_pre->setProperty("role", QStringLiteral("panel"));
    m_pre->setPlaceholderText(QStringLiteral("Estado inicial del sistema, datos de prueba, cuenta…"));
    connect(m_pre, &TextArea::edited, this, [this](const QString& t) { edit([&]() { m_store.updateCase(m_store.selectedId(), [&](TestCase& c) { c.preconditions = t; }); }); });
    pv->addWidget(m_pre);
    v->addWidget(preBlock);

    // Pasos
    auto* stepsBlock = new QWidget;
    auto* sv = ui::vbox(stepsBlock, 0, 8);
    auto* stepsHead = new QWidget;
    auto* sh = ui::hbox(stepsHead, 0, 8);
    m_stepsHeader = ui::label(QString(), "eyebrow");
    sh->addWidget(m_stepsHeader, 1);
    auto* addStep = ui::button(QStringLiteral("+ Añadir paso"), "dashed");
    connect(addStep, &QPushButton::clicked, this, [this]() { m_store.addStep(m_store.selectedId()); });
    sh->addWidget(addStep);
    sv->addWidget(stepsHead);
    auto* cols = new QWidget;
    auto* cg = new QGridLayout(cols);
    cg->setContentsMargins(4, 0, 4, 0);
    cg->setHorizontalSpacing(8);
    cg->addWidget(ui::label(QStringLiteral("#"), "eyebrow"), 0, 0);
    cg->addWidget(ui::label(QStringLiteral("ACCIÓN"), "eyebrow"), 0, 1);
    cg->addWidget(ui::label(QStringLiteral("RESULTADO ESPERADO"), "eyebrow"), 0, 2);
    cg->setColumnMinimumWidth(0, 28);
    cg->setColumnMinimumWidth(3, 60);
    cg->setColumnStretch(1, 1);
    cg->setColumnStretch(2, 1);
    sv->addWidget(cols);
    auto* stepsList = new QWidget;
    m_stepsLayout = ui::vbox(stepsList, 0, 8);
    sv->addWidget(stepsList);
    v->addWidget(stepsBlock);

    // Evidencias
    auto* shotsBlock = new QWidget;
    auto* shv = ui::vbox(shotsBlock, 0, 8);
    auto* shotsHead = new QWidget;
    auto* shh = ui::hbox(shotsHead, 0, 8);
    m_shotsHeader = ui::label(QString(), "eyebrow");
    shh->addWidget(m_shotsHeader, 1);
    m_unassigned = ui::label(QString(), "warn");
    shh->addWidget(m_unassigned);
    m_sortShots = smallButton(QStringLiteral("Ordenar por paso"), "outline");
    connect(m_sortShots, &QPushButton::clicked, this, [this]() { m_store.sortShotsByStep(m_store.selectedId()); });
    shh->addWidget(m_sortShots);
    auto* capture = ui::button(QStringLiteral("+ Capturar pantalla"), "dashed");
    connect(capture, &QPushButton::clicked, this, &CasesView::captureRequested);
    shh->addWidget(capture);
    shv->addWidget(shotsHead);
    m_shotsContainer = new QWidget;
    m_shotsGrid = new QGridLayout(m_shotsContainer);
    m_shotsGrid->setContentsMargins(0, 0, 0, 0);
    m_shotsGrid->setSpacing(10);
    shv->addWidget(m_shotsContainer);
    v->addWidget(shotsBlock);

    // Últimas ejecuciones
    auto* histBlock = new QWidget;
    auto* hv2 = ui::vbox(histBlock, 0, 8);
    auto* histHead = new QWidget;
    auto* hhh = ui::hbox(histHead, 0, 8);
    m_historyHeader = ui::label(QString(), "eyebrow");
    hhh->addWidget(m_historyHeader, 1);
    auto* all = smallButton(QStringLiteral("Ver historial"), "outline");
    connect(all, &QPushButton::clicked, this, [this]() { if (!m_store.selectedId().isEmpty()) emit historyRequested(m_store.selectedId()); });
    hhh->addWidget(all);
    hv2->addWidget(histHead);
    auto* histList = new QWidget;
    m_historyLayout = ui::vbox(histList, 0, 6);
    hv2->addWidget(histList);
    v->addWidget(histBlock);

    root->addWidget(sa, 1);
}

void CasesView::edit(const std::function<void()>& mutation) {
    if (m_selfEdit) return;
    m_selfEdit = true;
    mutation();
    m_selfEdit = false;
}

void CasesView::loadEditor() {
    const TestCase* c = m_store.selected();
    m_editor->setVisible(c != nullptr);
    if (!c) return;
    m_selfEdit = true;
    m_idLabel->setText(c->id);
    if (m_title->text() != c->title) { m_title->setText(c->title); m_title->setCursorPosition(0); }
    if (!c->suite.isEmpty() && m_suiteBox->findText(c->suite) < 0) m_suiteBox->addItem(c->suite);
    m_suiteBox->setCurrentText(c->suite);
    m_priorityBox->setCurrentText(toString(c->priority));
    m_statusBox->setCurrentText(toString(c->status));
    m_lastRun->setText(c->lastRun.label());
    m_lastRun->setStyleSheet(QStringLiteral("font-weight:600;padding:5px 0;color:%1;").arg(lastRunColor(c->lastRun)));
    if (m_component->text() != c->component) m_component->setText(c->component);
    if (m_jiraKey->text() != c->jiraKey) m_jiraKey->setText(c->jiraKey);
    m_openJira->setEnabled(!c->jiraKey.isEmpty());
    if (parseTags(m_tags->text()) != c->tags) m_tags->setText(c->tags.join(QStringLiteral(", ")));
    m_pre->setTextSilently(c->preconditions);
    m_selfEdit = false;
    refreshSteps();
    refreshShots();
    refreshHistory();
}

void CasesView::refreshSteps() {
    const TestCase* c = m_store.selected();
    if (!c) return;
    m_stepsHeader->setText(QStringLiteral("PASOS · %1").arg(c->steps.size()));
    ui::clearLayout(m_stepsLayout);
    const QString id = c->id;
    const int n = c->steps.size();
    for (int i = 0; i < n; ++i) {
        auto* row = ui::card("card");
        row->setStyleSheet(QStringLiteral("QFrame[role=\"card\"]{border-radius:10px;}"));
        auto* g = new QGridLayout(row);
        g->setContentsMargins(8, 8, 8, 8);
        g->setHorizontalSpacing(8);
        auto* num = ui::label(QStringLiteral("%1").arg(i + 1, 2, 10, QLatin1Char('0')), "mono-muted");
        num->setAlignment(Qt::AlignCenter);
        num->setFixedWidth(28);
        g->addWidget(num, 0, 0);
        auto* action = new TextArea(2);
        action->setPlaceholderText(QStringLiteral("Qué hace el tester…"));
        action->setTextSilently(c->steps[i].action);
        connect(action, &TextArea::edited, this, [this, id, i](const QString& t) { edit([&]() { m_store.updateStep(id, i, [&](TestStep& s) { s.action = t; }); }); });
        g->addWidget(action, 0, 1);
        auto* expected = new TextArea(2);
        expected->setPlaceholderText(QStringLiteral("Qué debe ocurrir…"));
        expected->setTextSilently(c->steps[i].expected);
        connect(expected, &TextArea::edited, this, [this, id, i](const QString& t) { edit([&]() { m_store.updateStep(id, i, [&](TestStep& s) { s.expected = t; }); }); });
        g->addWidget(expected, 0, 2);

        // Reordenar / insertar / eliminar
        auto* tools = new QWidget;
        auto* tg = new QGridLayout(tools);
        tg->setContentsMargins(0, 0, 0, 0);
        tg->setSpacing(2);
        auto* up = ui::button(QStringLiteral("▲"), "icon-move");
        up->setToolTip(QStringLiteral("Subir paso"));
        up->setEnabled(i > 0);
        connect(up, &QPushButton::clicked, this, [this, id, i]() { m_store.moveStep(id, i, -1); });
        auto* down = ui::button(QStringLiteral("▼"), "icon-move");
        down->setToolTip(QStringLiteral("Bajar paso"));
        down->setEnabled(i < n - 1);
        connect(down, &QPushButton::clicked, this, [this, id, i]() { m_store.moveStep(id, i, +1); });
        auto* insert = ui::button(QStringLiteral("+"), "icon-move");
        insert->setToolTip(QStringLiteral("Insertar paso debajo"));
        connect(insert, &QPushButton::clicked, this, [this, id, i]() { m_store.insertStep(id, i + 1); });
        auto* remove = ui::button(QStringLiteral("×"), "icon");
        remove->setToolTip(QStringLiteral("Eliminar paso (se puede deshacer)"));
        connect(remove, &QPushButton::clicked, this, [this, id, i]() { m_store.removeStep(id, i); });
        for (auto* b : {up, down, insert}) b->setFixedSize(26, 22);
        remove->setFixedSize(26, 22);
        tg->addWidget(up, 0, 0);
        tg->addWidget(down, 0, 1);
        tg->addWidget(insert, 1, 0);
        tg->addWidget(remove, 1, 1);
        g->addWidget(tools, 0, 3, Qt::AlignTop);
        g->setColumnStretch(1, 1);
        g->setColumnStretch(2, 1);
        m_stepsLayout->addWidget(row);
    }
}

void CasesView::refreshShots() {
    const TestCase* c = m_store.selected();
    if (!c) return;
    m_shotsHeader->setText(QStringLiteral("EVIDENCIAS · %1 CAPTURAS").arg(c->shots.size()));
    const int unassigned = c->unassignedShots();
    m_unassigned->setVisible(unassigned > 0);
    m_unassigned->setText(QStringLiteral("%1 sin paso asignado").arg(unassigned));
    m_sortShots->setVisible(!c->shots.isEmpty());
    ui::clearLayout(m_shotsGrid);
    m_shotsContainer->setVisible(!c->shots.isEmpty());

    const QString id = c->id;
    const int columns = std::max(1, std::min(4, (m_editor->width() > 0 ? m_editor->width() : 800) / 210));
    for (int i = 0; i < c->shots.size(); ++i) {
        auto* card = new ShotCard(c->shots[i], c->steps, ShotCard::Layout::Grid);
        connect(card, &ShotCard::stepChanged, this, [this, id](int shotId, int step) { m_store.assignShotStep(id, shotId, step); });
        connect(card, &ShotCard::moveRequested, this, [this, id](int shotId, int delta) { m_store.moveShot(id, shotId, delta); });
        connect(card, &ShotCard::removeRequested, this, [this, id](int shotId) { m_store.removeShot(id, shotId); });
        m_shotsGrid->addWidget(card, i / columns, i % columns);
    }
    for (int col = 0; col < columns; ++col) m_shotsGrid->setColumnStretch(col, 1);
}

void CasesView::refreshHistory() {
    ui::clearLayout(m_historyLayout);
    const TestCase* c = m_store.selected();
    if (!c) return;
    const auto runs = m_history.runsForCase(c->id);
    m_historyHeader->setText(QStringLiteral("ÚLTIMAS EJECUCIONES · %1").arg(runs.size()));
    if (runs.isEmpty()) {
        m_historyLayout->addWidget(ui::label(QStringLiteral("Este caso todavía no se ha ejecutado."), "muted-sm"));
        return;
    }
    constexpr int kMax = 5;
    for (int i = 0; i < runs.size() && i < kMax; ++i) {
        const RunRecord& r = runs[i];
        auto* row = ui::card("card-flat");
        auto* h = ui::hbox(row, 0, 10);
        h->setContentsMargins(10, 7, 10, 7);
        const QString color = r.verdict == Verdict::Superado ? theme::Green : r.verdict == Verdict::Fallido ? theme::Red : theme::Amber;
        h->addWidget(ui::pill(toString(r.verdict).toUpper(), color, r.verdict == Verdict::Fallido ? QStringLiteral("#ffffff") : theme::Bg));
        h->addWidget(ui::label(r.finishedAt.toString(QStringLiteral("dd/MM/yyyy HH:mm")), "muted-sm"));
        h->addWidget(ui::label(QStringLiteral("%1/%2 pasos · %3").arg(r.steps.size()).arg(r.plannedSteps).arg(formatDuration(r.durationSecs)), "muted-sm"));
        h->addStretch(1);
        const PlanRun* p = r.planRunId.isEmpty() ? nullptr : m_history.findPlan(r.planRunId);
        h->addWidget(ui::label(p ? p->name : QStringLiteral("Ejecución suelta"), "muted-sm"));
        m_historyLayout->addWidget(row);
    }
}

void CasesView::onCaseChanged(const QString& id) {
    refreshFilters();
    refreshList();
    if (id != m_store.selectedId()) return;
    if (m_selfEdit) {
        // Edición desde este mismo editor: no reconstruir campos con foco; sólo derivados.
        const TestCase* c = m_store.selected();
        if (c) {
            m_stepsHeader->setText(QStringLiteral("PASOS · %1").arg(c->steps.size()));
            m_lastRun->setText(c->lastRun.label());
        }
        return;
    }
    loadEditor();
}

// ---- Acciones ------------------------------------------------------------------------------

void CasesView::newSuite() {
    const TestCase* c = m_store.selected();
    if (!c) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Nueva suite"), QStringLiteral("Nombre de la suite:"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    const QString id = c->id;
    m_store.updateCase(id, [&](TestCase& tc) { tc.suite = name; });
    emit toast(QStringLiteral("%1 movido a la suite \"%2\"").arg(id, name), theme::Green);
}

void CasesView::duplicateSelected() {
    const QString src = m_store.selectedId();
    if (src.isEmpty()) return;
    const QString id = m_store.duplicateCase(src);
    if (!id.isEmpty()) emit toast(QStringLiteral("%1 duplicado como %2").arg(src, id), theme::Green);
}

void CasesView::removeSelected() {
    const TestCase* c = m_store.selected();
    if (!c) return;
    QString detail = QStringLiteral("Se eliminará el caso con sus %1 pasos").arg(c->steps.size());
    if (!c->shots.isEmpty()) detail += QStringLiteral(" y sus %1 capturas (los ficheros se borran del disco)").arg(c->shots.size());
    detail += QStringLiteral(". Podrás deshacerlo durante unos segundos.");
    QMessageBox box(QMessageBox::Warning, QStringLiteral("Eliminar %1").arg(c->id),
                    QStringLiteral("¿Eliminar \"%1\"?").arg(c->title.isEmpty() ? c->id : c->title), QMessageBox::NoButton, this);
    box.setInformativeText(detail);
    auto* del = box.addButton(QStringLiteral("Eliminar"), QMessageBox::DestructiveRole);
    box.addButton(QStringLiteral("Cancelar"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != del) return;
    m_store.removeCase(c->id);
}

void CasesView::importCases() {
    const QString start = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Importar casos"), start,
                                                      QStringLiteral("Casos (*.json *.csv);;JSON (*.json);;CSV (*.csv)"));
    if (path.isEmpty()) return;
    const auto r = m_transfer.importFrom(path);
    emit toast(r.message, r.ok ? theme::Green : theme::Red);
}

void CasesView::exportCases(int format) {
    const auto fmt = static_cast<CaseTransferService::Format>(format);
    const QString ext = CaseTransferService::extension(fmt);
    const QString filter = fmt == CaseTransferService::Format::Json ? QStringLiteral("JSON (*.json)")
                           : fmt == CaseTransferService::Format::Csv ? QStringLiteral("CSV (*.csv)")
                                                                     : QStringLiteral("Markdown (*.md)");
    const QString suggested = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(QStringLiteral("casos-qaflow.") + ext);
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Exportar casos"), suggested, filter);
    if (path.isEmpty()) return;
    if (!path.endsWith(QLatin1Char('.') + ext, Qt::CaseInsensitive)) path += QLatin1Char('.') + ext;
    const auto r = m_transfer.exportTo(path, fmt);
    emit toast(r.message, r.ok ? theme::Green : theme::Red);
}

} // namespace qaflow
