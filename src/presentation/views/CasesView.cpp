#include "CasesView.h"

#include "application/BugStore.h"
#include "application/CaseTransferService.h"
#include "application/EvidenceService.h"
#include "application/RunController.h"
#include "application/RunHistoryStore.h"
#include "application/TestCaseStore.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/EvidenceActions.h"
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
QComboBox* filterBox(const QString& all, const QList<std::pair<QString, int>>& items) {
    auto* b = new QComboBox;
    b->addItem(all, -1);
    for (const auto& [text, value] : items) b->addItem(text, value);
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

CasesView::CasesView(TestCaseStore& store, RunController& run, RunHistoryStore& history, CaseTransferService& transfer, BugStore& bugs,
                     EvidenceService& evidence, QWidget* parent)
    : QWidget(parent), m_store(store), m_run(run), m_history(history), m_transfer(transfer), m_bugs(bugs), m_evidence(evidence) {
    auto* root = ui::hbox(this, 0, 0);
    buildListPane(root);
    buildEditor(root);

    connect(&m_store, &TestCaseStore::suitesChanged, this, &CasesView::refreshFilters);
    connect(&m_store, &TestCaseStore::casesChanged, this, [this]() { refreshFilters(); refreshList(); });
    connect(&m_store, &TestCaseStore::selectionChanged, this, [this](const QString&) { refreshList(); loadEditor(); });
    connect(&m_store, &TestCaseStore::caseChanged, this, &CasesView::onCaseChanged);
    connect(&m_run, &RunController::runChanged, this, &CasesView::refreshList);
    connect(&m_history, &RunHistoryStore::historyChanged, this, &CasesView::refreshHistory);
    connect(&m_bugs, &BugStore::bugsChanged, this, &CasesView::refreshBugs);
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
    th->addWidget(ui::label(tr("Casos"), "h1-sm"), 1);
    auto* newBtn = smallButton(tr("+ Nuevo"), "primary");
    connect(newBtn, &QPushButton::clicked, this, [this]() { m_store.createCase(); });
    th->addWidget(newBtn);
    auto* more = smallButton(QStringLiteral("⋯"), "outline", tr("Importar y exportar"));
    more->setStyleSheet(QStringLiteral("padding:6px 8px;font-size:13px;border-radius:8px;"));
    more->setFixedWidth(30);
    auto* menu = new QMenu(more);
    menu->addAction(tr("Importar casos (JSON o CSV)…"), this, &CasesView::importCases);
    menu->addSeparator();
    menu->addAction(tr("Exportar todo a JSON…"), this, [this]() { exportCases(CaseTransferService::Format::Json); });
    menu->addAction(tr("Exportar todo a CSV…"), this, [this]() { exportCases(CaseTransferService::Format::Csv); });
    menu->addAction(tr("Exportar todo a Markdown…"), this, [this]() { exportCases(CaseTransferService::Format::Markdown); });
    more->setMenu(menu);
    th->addWidget(more);
    hv->addWidget(titleRow);

    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("caseSearch"));
    m_search->setPlaceholderText(tr("Buscar por título, ID, etiqueta, componente…"));
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& t) { m_filter.text = t; refreshList(); });
    hv->addWidget(m_search);

    auto* filters = new QWidget;
    m_filterRow = new FlowLayout(filters, 0, 6, 6);
    hv->addWidget(filters);

    // Filtros por estado, prioridad y última ejecución
    auto* combos = new QWidget;
    auto* ch = ui::hbox(combos, 0, 6);
    // Cada opción lleva el valor del enum como dato: el texto se traduce, el filtro no.
    m_statusFilter = filterBox(tr("Estado"), {{label(CaseStatus::Listo), static_cast<int>(CaseStatus::Listo)},
                                              {label(CaseStatus::Borrador), static_cast<int>(CaseStatus::Borrador)},
                                              {label(CaseStatus::Obsoleto), static_cast<int>(CaseStatus::Obsoleto)}});
    m_priorityFilter = filterBox(tr("Prioridad"), {{label(Priority::Alta), static_cast<int>(Priority::Alta)},
                                                   {label(Priority::Media), static_cast<int>(Priority::Media)},
                                                   {label(Priority::Baja), static_cast<int>(Priority::Baja)}});
    m_outcomeFilter = filterBox(tr("Ejecución"), {{label(RunOutcome::Passed), static_cast<int>(RunOutcome::Passed)},
                                                  {label(RunOutcome::Failed), static_cast<int>(RunOutcome::Failed)},
                                                  {label(RunOutcome::Blocked), static_cast<int>(RunOutcome::Blocked)},
                                                  {label(RunOutcome::None), static_cast<int>(RunOutcome::None)}});
    connect(m_statusFilter, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_filter.status = i <= 0 ? std::nullopt : std::optional<CaseStatus>(static_cast<CaseStatus>(m_statusFilter->currentData().toInt()));
        refreshList();
    });
    connect(m_priorityFilter, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_filter.priority = i <= 0 ? std::nullopt : std::optional<Priority>(static_cast<Priority>(m_priorityFilter->currentData().toInt()));
        refreshList();
    });
    connect(m_outcomeFilter, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_filter.outcome = i <= 0 ? std::nullopt : std::optional<RunOutcome>(static_cast<RunOutcome>(m_outcomeFilter->currentData().toInt()));
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
    QStringList chips{tr("Todas")};
    chips << suites;
    for (const auto& s : chips) {
        const QString value = s == tr("Todas") ? QString() : s;
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
        th->addWidget(ui::label(QStringLiteral("· %1").arg(c.suite.isEmpty() ? tr("sin suite") : c.suite), "mono-muted"));
        th->addStretch(1);
        const auto pill = theme::priorityPill(toString(c.priority));
        th->addWidget(ui::pill(label(c.priority), pill.bg, pill.fg));
        v->addWidget(top);

        auto* title = new QLabel(c.title.isEmpty() ? tr("(sin título)") : c.title);
        title->setWordWrap(true);
        title->setStyleSheet(QStringLiteral("font-size:13.5px;font-weight:600;color:%1;").arg(theme::Text));
        v->addWidget(title);

        auto* bottom = new QWidget;
        auto* bh = ui::hbox(bottom, 0, 8);
        auto* status = new QLabel(tr("%1 · %2 pasos").arg(label(c.status)).arg(c.steps.size()));
        status->setStyleSheet(QStringLiteral("font-size:11.5px;font-weight:600;color:%1;").arg(statusColor(c.status)));
        bh->addWidget(status, 1);
        if (c.id == runningId) {
            auto* badge = ui::pill(tr("● EN EJECUCIÓN"), theme::Green, theme::Bg);
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
        auto* e = ui::label(m_store.cases().isEmpty() ? tr("No hay casos. Crea uno o importa un archivo.") : tr("Ningún caso coincide con los filtros."), "muted");
        e->setWordWrap(true);
        e->setContentsMargins(8, 8, 8, 8);
        m_listLayout->addWidget(e);
    }
    m_listCount->setText(m_filter.isEmpty() ? tr("%1 casos").arg(shown)
                                            : tr("%1 de %2 casos").arg(shown).arg(m_store.cases().size()));
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
    m_title->setPlaceholderText(tr("Título del caso"));
    connect(m_title, &QLineEdit::textEdited, this, [this](const QString& t) {
        edit([&]() { m_store.updateCase(m_store.selectedId(), [&](TestCase& c) { c.title = t; }); });
    });
    tv->addWidget(m_title);
    hh->addWidget(titleBlock, 1);
    auto* save = ui::button(tr("Guardar"), "outline");
    connect(save, &QPushButton::clicked, this, [this]() {
        const TestCase* c = m_store.selected();
        if (!c) return;
        const QString id = c->id;
        m_store.updateCase(id, [](TestCase& tc) { if (tc.status == CaseStatus::Borrador && tc.readyToBeMarkedListo()) tc.status = CaseStatus::Listo; });
        emit toast(tr("%1 guardado").arg(id), theme::Green);
    });
    auto* more = ui::button(QStringLiteral("⋯"), "outline");
    more->setToolTip(tr("Más acciones"));
    more->setFixedWidth(40);
    auto* menu = new QMenu(more);
    menu->addAction(tr("Duplicar caso"), this, &CasesView::duplicateSelected);
    menu->addAction(tr("Ver historial de ejecuciones"), this, [this]() { if (!m_store.selectedId().isEmpty()) emit historyRequested(m_store.selectedId()); });
    menu->addSeparator();
    menu->addAction(tr("Eliminar caso…"), this, &CasesView::removeSelected);
    more->setMenu(menu);
    hh->addWidget(save, 0, Qt::AlignTop);
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
    addSuite->setToolTip(tr("Nueva suite"));
    addSuite->setFixedWidth(24);
    connect(addSuite, &QPushButton::clicked, this, &CasesView::newSuite);
    srh->addWidget(addSuite);
    m_priorityBox = new QComboBox;
    for (auto p : {Priority::Alta, Priority::Media, Priority::Baja}) m_priorityBox->addItem(label(p), static_cast<int>(p));
    m_statusBox = new QComboBox;
    for (auto st : {CaseStatus::Listo, CaseStatus::Borrador, CaseStatus::Obsoleto}) m_statusBox->addItem(label(st), static_cast<int>(st));
    m_lastRun = new QLabel;
    m_lastRun->setStyleSheet(QStringLiteral("font-weight:600;padding:5px 0;"));
    connect(m_suiteBox, &QComboBox::currentTextChanged, this, [this](const QString& t) { edit([&]() { m_store.updateCase(m_store.selectedId(), [&](TestCase& c) { c.suite = t; }); }); });
    connect(m_priorityBox, &QComboBox::currentIndexChanged, this, [this](int) {
        const auto p = static_cast<Priority>(m_priorityBox->currentData().toInt());
        edit([&]() { m_store.updateCase(m_store.selectedId(), [&](TestCase& c) { c.priority = p; }); });
    });
    connect(m_statusBox, &QComboBox::currentIndexChanged, this, [this](int) {
        const auto st = static_cast<CaseStatus>(m_statusBox->currentData().toInt());
        edit([&]() { m_store.updateCase(m_store.selectedId(), [&](TestCase& c) { c.status = st; }); });
    });
    mg->addWidget(fieldCell(tr("Suite"), suiteRow), 0, 0);
    mg->addWidget(fieldCell(tr("Prioridad"), m_priorityBox), 0, 1);
    mg->addWidget(fieldCell(tr("Estado"), m_statusBox), 0, 2);
    mg->addWidget(fieldCell(tr("Última ejecución"), m_lastRun), 0, 3);

    m_component = new QLineEdit;
    m_component->setPlaceholderText(tr("p. ej. Carrito"));
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
    m_openJira->setToolTip(tr("Abrir en Jira"));
    m_openJira->setFixedWidth(24);
    connect(m_openJira, &QPushButton::clicked, this, [this]() { if (const TestCase* c = m_store.selected(); c && !c->jiraKey.isEmpty()) emit openJiraRequested(c->jiraKey); });
    jrh->addWidget(m_openJira);
    m_tags = new QLineEdit;
    m_tags->setPlaceholderText(tr("regresión, smoke…"));
    m_tags->setToolTip(tr("Etiquetas separadas por comas"));
    connect(m_tags, &QLineEdit::textEdited, this, [this](const QString& t) { edit([&]() { m_store.updateCase(m_store.selectedId(), [&](TestCase& c) { c.tags = parseTags(t); }); }); });
    mg->addWidget(fieldCell(tr("Componente"), m_component), 1, 0);
    mg->addWidget(fieldCell(tr("Historia Jira"), jiraRow), 1, 1);
    // El Test de Zephyr no se enseña aquí: lo que se enlaza son las ejecuciones, y se ve en el
    // historial (informe del plan y detalle de la ejecución). El caso sólo lo guarda.
    mg->addWidget(fieldCell(tr("Etiquetas"), m_tags), 1, 2, 1, 2);
    for (int i = 0; i < 4; ++i) mg->setColumnStretch(i, 1);
    v->addWidget(meta);

    // Precondiciones
    auto* preBlock = new QWidget;
    auto* pv = ui::vbox(preBlock, 0, 8);
    pv->addWidget(ui::label(tr("PRECONDICIONES"), "eyebrow"));
    m_pre = new TextArea(2);
    m_pre->setProperty("role", QStringLiteral("panel"));
    m_pre->setPlaceholderText(tr("Estado inicial del sistema, datos de prueba, cuenta…"));
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
    auto* addStep = ui::button(tr("+ Añadir paso"), "dashed");
    connect(addStep, &QPushButton::clicked, this, [this]() { m_store.addStep(m_store.selectedId()); });
    sh->addWidget(addStep);
    sv->addWidget(stepsHead);
    auto* cols = new QWidget;
    auto* cg = new QGridLayout(cols);
    cg->setContentsMargins(4, 0, 4, 0);
    cg->setHorizontalSpacing(8);
    cg->addWidget(ui::label(QStringLiteral("#"), "eyebrow"), 0, 0);
    cg->addWidget(ui::label(tr("ACCIÓN"), "eyebrow"), 0, 1);
    cg->addWidget(ui::label(tr("RESULTADO ESPERADO"), "eyebrow"), 0, 2);
    cg->setColumnMinimumWidth(0, 28);
    cg->setColumnMinimumWidth(3, 60);
    cg->setColumnStretch(1, 1);
    cg->setColumnStretch(2, 1);
    sv->addWidget(cols);
    auto* stepsList = new QWidget;
    m_stepsLayout = ui::vbox(stepsList, 0, 8);
    sv->addWidget(stepsList);
    v->addWidget(stepsBlock);

    // Las evidencias no están aquí: son de cada ejecución y se ven en su ficha del historial.

    // Últimas ejecuciones
    auto* histBlock = new QWidget;
    auto* hv2 = ui::vbox(histBlock, 0, 8);
    auto* histHead = new QWidget;
    auto* hhh = ui::hbox(histHead, 0, 8);
    m_historyHeader = ui::label(QString(), "eyebrow");
    hhh->addWidget(m_historyHeader, 1);
    auto* all = smallButton(tr("Ver historial"), "outline");
    connect(all, &QPushButton::clicked, this, [this]() { if (!m_store.selectedId().isEmpty()) emit historyRequested(m_store.selectedId()); });
    hhh->addWidget(all);
    hv2->addWidget(histHead);
    auto* histList = new QWidget;
    m_historyLayout = ui::vbox(histList, 0, 6);
    hv2->addWidget(histList);
    v->addWidget(histBlock);

    // Bugs reportados desde este caso
    auto* bugsBlock = new QWidget;
    auto* bv = ui::vbox(bugsBlock, 0, 8);
    m_bugsHeader = ui::label(QString(), "eyebrow");
    bv->addWidget(m_bugsHeader);
    auto* bugsList = new QWidget;
    m_bugsLayout = ui::vbox(bugsList, 0, 6);
    bv->addWidget(bugsList);
    v->addWidget(bugsBlock);

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
    m_priorityBox->setCurrentIndex(std::max(0, m_priorityBox->findData(static_cast<int>(c->priority))));
    m_statusBox->setCurrentIndex(std::max(0, m_statusBox->findData(static_cast<int>(c->status))));
    m_lastRun->setText(c->lastRun.label());
    m_lastRun->setStyleSheet(QStringLiteral("font-weight:600;padding:5px 0;color:%1;").arg(lastRunColor(c->lastRun)));
    if (m_component->text() != c->component) m_component->setText(c->component);
    if (m_jiraKey->text() != c->jiraKey) m_jiraKey->setText(c->jiraKey);
    m_openJira->setEnabled(!c->jiraKey.isEmpty());
    if (parseTags(m_tags->text()) != c->tags) m_tags->setText(c->tags.join(QStringLiteral(", ")));
    m_pre->setTextSilently(c->preconditions);
    m_selfEdit = false;
    refreshSteps();
    refreshHistory();
    refreshBugs();
}

void CasesView::refreshSteps() {
    const TestCase* c = m_store.selected();
    if (!c) return;
    m_stepsHeader->setText(tr("PASOS · %1").arg(c->steps.size()));
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
        action->setPlaceholderText(tr("Qué hace el tester…"));
        action->setTextSilently(c->steps[i].action);
        connect(action, &TextArea::edited, this, [this, id, i](const QString& t) { edit([&]() { m_store.updateStep(id, i, [&](TestStep& s) { s.action = t; }); }); });
        g->addWidget(action, 0, 1);
        auto* expected = new TextArea(2);
        expected->setPlaceholderText(tr("Qué debe ocurrir…"));
        expected->setTextSilently(c->steps[i].expected);
        connect(expected, &TextArea::edited, this, [this, id, i](const QString& t) { edit([&]() { m_store.updateStep(id, i, [&](TestStep& s) { s.expected = t; }); }); });
        g->addWidget(expected, 0, 2);

        // Reordenar / insertar / eliminar
        auto* tools = new QWidget;
        auto* tg = new QGridLayout(tools);
        tg->setContentsMargins(0, 0, 0, 0);
        tg->setSpacing(2);
        auto* up = ui::button(QStringLiteral("▲"), "icon-move");
        up->setToolTip(tr("Subir paso"));
        up->setEnabled(i > 0);
        connect(up, &QPushButton::clicked, this, [this, id, i]() { m_store.moveStep(id, i, -1); });
        auto* down = ui::button(QStringLiteral("▼"), "icon-move");
        down->setToolTip(tr("Bajar paso"));
        down->setEnabled(i < n - 1);
        connect(down, &QPushButton::clicked, this, [this, id, i]() { m_store.moveStep(id, i, +1); });
        auto* insert = ui::button(QStringLiteral("+"), "icon-move");
        insert->setToolTip(tr("Insertar paso debajo"));
        connect(insert, &QPushButton::clicked, this, [this, id, i]() { m_store.insertStep(id, i + 1); });
        auto* remove = ui::button(QStringLiteral("×"), "icon");
        remove->setToolTip(tr("Eliminar paso (se puede deshacer)"));
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

void CasesView::refreshHistory() {
    ui::clearLayout(m_historyLayout);
    const TestCase* c = m_store.selected();
    if (!c) return;
    const auto runs = m_history.runsForCase(c->id);
    m_historyHeader->setText(tr("ÚLTIMAS EJECUCIONES · %1").arg(runs.size()));
    if (runs.isEmpty()) {
        m_historyLayout->addWidget(ui::label(tr("Este caso todavía no se ha ejecutado."), "muted-sm"));
        return;
    }
    constexpr int kMax = 5;
    for (int i = 0; i < runs.size() && i < kMax; ++i) {
        const RunRecord& r = runs[i];
        // La fila entera abre los resultados de esa ejecución en el historial: sus pasos con su
        // veredicto, sus evidencias y con qué está enlazada.
        auto* row = ui::button(QString(), "row");
        row->setObjectName(QStringLiteral("caseRun-%1").arg(r.id));
        row->setToolTip(tr("Ver los resultados de esta ejecución"));
        connect(row, &QPushButton::clicked, this, [this, id = r.id]() { emit openRunRequested(id); });
        auto* h = ui::hbox(row, 0, 10);
        h->setContentsMargins(10, 7, 10, 7);
        const QString color = r.verdict == Verdict::Superado ? theme::Green : r.verdict == Verdict::Fallido ? theme::Red : theme::Amber;
        h->addWidget(ui::pill(label(r.verdict).toUpper(), color, r.verdict == Verdict::Fallido ? QStringLiteral("#ffffff") : theme::Bg));
        h->addWidget(ui::label(r.finishedAt.toString(QStringLiteral("dd/MM/yyyy HH:mm")), "muted-sm"));
        QString detail = tr("%1/%2 pasos · %3").arg(r.steps.size()).arg(r.plannedSteps).arg(formatDuration(r.durationSecs));
        // Las evidencias son de la ejecución: aquí sólo se dice cuántas tiene cada una.
        if (const int shots = c->shotsOfRun(r.id).size(); shots > 0)
            detail += shots == 1 ? tr(" · 1 evidencia") : tr(" · %1 evidencias").arg(shots);
        h->addWidget(ui::label(detail, "muted-sm"));
        h->addStretch(1);
        const PlanRun* p = r.planRunId.isEmpty() ? nullptr : m_history.findPlan(r.planRunId);
        h->addWidget(ui::label(p ? p->name : tr("Ejecución suelta"), "muted-sm"));
        h->addWidget(ui::label(QStringLiteral("›"), "muted"));
        m_historyLayout->addWidget(row);
    }
}

void CasesView::refreshBugs() {
    ui::clearLayout(m_bugsLayout);
    const TestCase* c = m_store.selected();
    if (!c) return;
    const auto issues = m_bugs.issuesForCase(c->id);
    m_bugsHeader->setText(tr("BUGS REPORTADOS · %1").arg(issues.size()));
    if (issues.isEmpty()) {
        m_bugsLayout->addWidget(ui::label(tr("Ningún bug reportado desde este caso."), "muted-sm"));
        return;
    }
    for (const auto& i : issues) {
        auto* row = ui::card("card-flat");
        auto* h = ui::hbox(row, 0, 10);
        h->setContentsMargins(10, 7, 10, 7);
        auto* key = ui::button(i.key, "ghost");
        key->setToolTip(tr("Abrir en %1").arg(i.tracker));
        key->setStyleSheet(QStringLiteral("padding:2px 8px;font-size:12px;font-weight:700;font-family:'Consolas','DejaVu Sans Mono',monospace;color:%1;").arg(theme::Blue));
        connect(key, &QPushButton::clicked, this, [this, url = i.url]() { emit openIssueRequested(url); });
        h->addWidget(key);
        auto* title = new QLabel(i.title);
        title->setWordWrap(true);
        h->addWidget(title, 1);
        h->addWidget(ui::label(i.createdAt.toString(QStringLiteral("dd/MM/yyyy")), "muted-sm"));
        const QString status = i.status.isEmpty() ? tr("SIN CONSULTAR") : i.status.toUpper();
        h->addWidget(ui::pill(status, i.status.isEmpty() ? theme::tint(theme::Muted, 38) : i.resolved ? theme::Green : theme::tint(theme::Blue, 38),
                              i.status.isEmpty() ? theme::Muted : i.resolved ? theme::Bg : theme::Blue));
        m_bugsLayout->addWidget(row);
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
            m_stepsHeader->setText(tr("PASOS · %1").arg(c->steps.size()));
            m_lastRun->setText(c->lastRun.label());
        }
        return;
    }
    loadEditor();
}

// ---- Acciones ------------------------------------------------------------------------------

void CasesView::focusSearch() {
    m_search->setFocus(Qt::ShortcutFocusReason);
    m_search->selectAll();
}

void CasesView::newSuite() {
    const TestCase* c = m_store.selected();
    if (!c) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Nueva suite"), tr("Nombre de la suite:"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    const QString id = c->id;
    m_store.updateCase(id, [&](TestCase& tc) { tc.suite = name; });
    emit toast(tr("%1 movido a la suite \"%2\"").arg(id, name), theme::Green);
}

void CasesView::duplicateSelected() {
    const QString src = m_store.selectedId();
    if (src.isEmpty()) return;
    const QString id = m_store.duplicateCase(src);
    if (!id.isEmpty()) emit toast(tr("%1 duplicado como %2").arg(src, id), theme::Green);
}

void CasesView::removeSelected() {
    const TestCase* c = m_store.selected();
    if (!c) return;
    QString detail = tr("Se eliminará el caso con sus %1 pasos").arg(c->steps.size());
    if (!c->shots.isEmpty())
        detail += tr(" y las %1 evidencias de sus ejecuciones (los ficheros se borran del disco)").arg(c->shots.size());
    detail += tr(". Podrás deshacerlo durante unos segundos.");
    QMessageBox box(QMessageBox::Warning, tr("Eliminar %1").arg(c->id),
                    tr("¿Eliminar \"%1\"?").arg(c->title.isEmpty() ? c->id : c->title), QMessageBox::NoButton, this);
    box.setInformativeText(detail);
    auto* del = box.addButton(tr("Eliminar"), QMessageBox::DestructiveRole);
    box.addButton(tr("Cancelar"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() != del) return;
    m_store.removeCase(c->id);
}

void CasesView::importCases() {
    const QString start = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getOpenFileName(this, tr("Importar casos"), start,
                                                      tr("Casos (*.json *.csv);;JSON (*.json);;CSV (*.csv)"));
    if (path.isEmpty()) return;
    const auto r = m_transfer.importFrom(path);
    emit toast(r.message, r.ok ? theme::Green : theme::Red);
}

void CasesView::exportCases(CaseTransferService::Format fmt) {
    const QString ext = CaseTransferService::extension(fmt);
    const QString filter = fmt == CaseTransferService::Format::Json ? tr("JSON (*.json)")
                           : fmt == CaseTransferService::Format::Csv ? tr("CSV (*.csv)")
                                                                     : tr("Markdown (*.md)");
    const QString suggested = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(QStringLiteral("casos-qaflow.") + ext);
    QString path = QFileDialog::getSaveFileName(this, tr("Exportar casos"), suggested, filter);
    if (path.isEmpty()) return;
    if (!path.endsWith(QLatin1Char('.') + ext, Qt::CaseInsensitive)) path += QLatin1Char('.') + ext;
    const auto r = m_transfer.exportTo(path, fmt);
    emit toast(r.message, r.ok ? theme::Green : theme::Red);
}

} // namespace qaflow
