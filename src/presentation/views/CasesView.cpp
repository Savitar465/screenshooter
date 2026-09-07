#include "CasesView.h"

#include "application/RunController.h"
#include "application/TestCaseStore.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/FlowLayout.h"
#include "presentation/widgets/ShotCard.h"
#include "presentation/widgets/TextArea.h"
#include "presentation/widgets/Ui.h"

#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>

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
} // namespace

CasesView::CasesView(TestCaseStore& store, RunController& run, QWidget* parent) : QWidget(parent), m_store(store), m_run(run) {
    auto* root = ui::hbox(this, 0, 0);
    buildListPane(root);
    buildEditor(root);

    connect(&m_store, &TestCaseStore::casesChanged, this, [this]() { refreshFilters(); refreshList(); });
    connect(&m_store, &TestCaseStore::selectionChanged, this, [this](const QString&) { refreshList(); loadEditor(); });
    connect(&m_store, &TestCaseStore::caseChanged, this, &CasesView::onCaseChanged);
    connect(&m_run, &RunController::runChanged, this, &CasesView::refreshList);
    refreshFilters();
    refreshList();
    loadEditor();
}

void CasesView::buildListPane(QHBoxLayout* root) {
    auto* pane = ui::card("list-pane");
    pane->setMinimumWidth(240);
    pane->setMaximumWidth(300);
    pane->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* v = ui::vbox(pane, 0, 0);

    auto* head = new QWidget;
    auto* hv = ui::vbox(head, 16, 12);
    hv->setContentsMargins(16, 18, 16, 12);
    auto* titleRow = new QWidget;
    auto* th = ui::hbox(titleRow, 0, 8);
    th->addWidget(ui::label(QStringLiteral("Casos de prueba"), "h1-sm"), 1);
    auto* newBtn = ui::button(QStringLiteral("+ Nuevo"), "primary");
    newBtn->setStyleSheet(QStringLiteral("padding:6px 12px;font-size:12.5px;border-radius:8px;"));
    connect(newBtn, &QPushButton::clicked, this, [this]() { m_store.createCase(); });
    th->addWidget(newBtn);
    hv->addWidget(titleRow);

    auto* search = new QLineEdit;
    search->setPlaceholderText(QStringLiteral("Buscar por título, suite o ID…"));
    search->setClearButtonEnabled(true);
    connect(search, &QLineEdit::textChanged, this, [this](const QString& t) { m_search = t; refreshList(); });
    hv->addWidget(search);

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
    auto* hh = ui::hbox(head, 0, 16);
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
    hh->addWidget(save, 0, Qt::AlignTop);
    hh->addWidget(runBtn, 0, Qt::AlignTop);
    v->addWidget(head);

    // Metadatos
    auto* meta = new QWidget;
    auto* mg = new QGridLayout(meta);
    mg->setContentsMargins(0, 0, 0, 0);
    mg->setSpacing(6);
    m_suiteBox = new QComboBox;
    m_priorityBox = new QComboBox;
    m_priorityBox->addItems({QStringLiteral("Alta"), QStringLiteral("Media"), QStringLiteral("Baja")});
    m_statusBox = new QComboBox;
    m_statusBox->addItems({QStringLiteral("Listo"), QStringLiteral("Borrador"), QStringLiteral("Obsoleto")});
    m_lastRun = new QLabel;
    m_lastRun->setStyleSheet(QStringLiteral("font-weight:600;padding:5px 0;"));
    connect(m_suiteBox, &QComboBox::currentTextChanged, this, [this](const QString& t) { edit([&]() { m_store.updateCase(m_store.selectedId(), [&](TestCase& c) { c.suite = t; }); }); });
    connect(m_priorityBox, &QComboBox::currentTextChanged, this, [this](const QString& t) { edit([&]() { m_store.updateCase(m_store.selectedId(), [&](TestCase& c) { c.priority = priorityFromString(t); }); }); });
    connect(m_statusBox, &QComboBox::currentTextChanged, this, [this](const QString& t) { edit([&]() { m_store.updateCase(m_store.selectedId(), [&](TestCase& c) { c.status = statusFromString(t); }); }); });
    mg->addWidget(fieldCell(QStringLiteral("Suite"), m_suiteBox), 0, 0);
    mg->addWidget(fieldCell(QStringLiteral("Prioridad"), m_priorityBox), 0, 1);
    mg->addWidget(fieldCell(QStringLiteral("Estado"), m_statusBox), 0, 2);
    mg->addWidget(fieldCell(QStringLiteral("Última ejecución"), m_lastRun), 0, 3);
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
    cg->setColumnMinimumWidth(3, 28);
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
    m_sortShots = ui::button(QStringLiteral("Ordenar por paso"), "outline");
    m_sortShots->setStyleSheet(QStringLiteral("padding:5px 10px;font-size:12px;border-radius:8px;"));
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

    root->addWidget(sa, 1);
}

void CasesView::edit(const std::function<void()>& mutation) {
    if (m_selfEdit) return;
    m_selfEdit = true;
    mutation();
    m_selfEdit = false;
}

void CasesView::refreshFilters() {
    ui::clearLayout(m_filterRow);
    QStringList suites{QStringLiteral("Todas")};
    suites << m_store.suites();
    for (const auto& s : suites) {
        auto* b = ui::button(s, "chip");
        ui::setFlag(b, "active", s == m_suite);
        connect(b, &QPushButton::clicked, this, [this, s]() { m_suite = s; refreshFilters(); refreshList(); });
        m_filterRow->addWidget(b);
    }
    if (m_suiteBox) {
        const QString cur = m_suiteBox->currentText();
        m_suiteBox->blockSignals(true);
        m_suiteBox->clear();
        m_suiteBox->addItems(m_store.suites());
        m_suiteBox->setCurrentText(cur);
        m_suiteBox->blockSignals(false);
    }
}

void CasesView::refreshList() {
    ui::clearLayout(m_listLayout);
    const QString q = m_search.trimmed().toLower();
    const QString runningId = m_run.isRunning() ? m_run.state().caseId : QString();

    for (const auto& c : m_store.cases()) {
        if (m_suite != QStringLiteral("Todas") && c.suite != m_suite) continue;
        if (!q.isEmpty() && !(c.title + c.id + c.suite).toLower().contains(q)) continue;

        auto* row = ui::button(QString(), "row");
        ui::setFlag(row, "running", c.id == runningId);
        ui::setFlag(row, "active", c.id == m_store.selectedId() && c.id != runningId);
        auto* v = ui::vbox(row, 0, 4);
        v->setContentsMargins(12, 10, 12, 10);

        auto* top = new QWidget;
        auto* th = ui::hbox(top, 0, 8);
        th->addWidget(ui::label(c.id, "mono-muted"));
        th->addWidget(ui::label(QStringLiteral("· %1").arg(c.suite), "mono-muted"));
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

        for (auto* child : row->findChildren<QWidget*>()) child->setAttribute(Qt::WA_TransparentForMouseEvents);
        const QString id = c.id;
        connect(row, &QPushButton::clicked, this, [this, id]() { m_store.select(id); });
        m_listLayout->addWidget(row);
    }
    m_listLayout->addStretch(1);
}

void CasesView::loadEditor() {
    const TestCase* c = m_store.selected();
    m_editor->setVisible(c != nullptr);
    if (!c) return;
    m_selfEdit = true;
    m_idLabel->setText(c->id);
    if (m_title->text() != c->title) { m_title->setText(c->title); m_title->setCursorPosition(0); }
    m_suiteBox->setCurrentText(c->suite);
    m_priorityBox->setCurrentText(toString(c->priority));
    m_statusBox->setCurrentText(toString(c->status));
    m_lastRun->setText(c->lastRun.label());
    m_lastRun->setStyleSheet(QStringLiteral("font-weight:600;padding:5px 0;color:%1;").arg(lastRunColor(c->lastRun)));
    m_pre->setTextSilently(c->preconditions);
    m_selfEdit = false;
    refreshSteps();
    refreshShots();
}

void CasesView::refreshSteps() {
    const TestCase* c = m_store.selected();
    if (!c) return;
    m_stepsHeader->setText(QStringLiteral("PASOS · %1").arg(c->steps.size()));
    ui::clearLayout(m_stepsLayout);
    const QString id = c->id;
    for (int i = 0; i < c->steps.size(); ++i) {
        auto* row = ui::card("card");
        row->setStyleSheet(QStringLiteral("QFrame[role=\"card\"]{border-radius:10px;}"));
        auto* g = new QGridLayout(row);
        g->setContentsMargins(8, 8, 8, 8);
        g->setHorizontalSpacing(8);
        auto* n = ui::label(QStringLiteral("%1").arg(i + 1, 2, 10, QLatin1Char('0')), "mono-muted");
        n->setAlignment(Qt::AlignCenter);
        n->setFixedWidth(28);
        g->addWidget(n, 0, 0);
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
        auto* remove = ui::button(QStringLiteral("×"), "icon");
        remove->setToolTip(QStringLiteral("Eliminar paso"));
        remove->setFixedWidth(28);
        connect(remove, &QPushButton::clicked, this, [this, id, i]() { m_store.removeStep(id, i); });
        g->addWidget(remove, 0, 3);
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

void CasesView::onCaseChanged(const QString& id) {
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

} // namespace qaflow
