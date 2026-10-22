#include "CasesView.h"

#include "application/CaseTransferService.h"
#include "application/EvidenceService.h"
#include "application/RunController.h"
#include "application/RunHistoryStore.h"
#include "application/TestCaseStore.h"
#include "presentation/theme/Theme.h"
#include "presentation/views/CaseForm.h"
#include "presentation/widgets/EvidenceActions.h"
#include "presentation/widgets/Responsive.h"
#include "presentation/widgets/ShotCard.h"
#include "presentation/widgets/Ui.h"

#include <QComboBox>
#include <QCompleter>
#include <QDir>
#include <QFileDialog>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QTimer>

namespace qaflow {

namespace {
QString lastRunColor(const LastRun& lr) {
    if (lr.outcome == RunOutcome::Passed) return theme::Green;
    if (lr.outcome == RunOutcome::Failed) return theme::Red;
    if (lr.outcome == RunOutcome::Blocked) return theme::Amber;
    return theme::Muted;
}
QString priorityColor(Priority p) {
    switch (p) {
        case Priority::Alta: return theme::RedSoft;
        case Priority::Media: return theme::AmberSoft;
        case Priority::Baja: return theme::Muted;
    }
    return theme::Muted;
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

CasesView::CasesView(TestCaseStore& store, RunController& run, RunHistoryStore& history, CaseTransferService& transfer,
                     EvidenceService& evidence, QWidget* parent)
    : QWidget(parent), m_store(store), m_run(run), m_history(history), m_transfer(transfer), m_evidence(evidence) {
    // La lista se ensancha o se estrecha arrastrando su borde; el ancho se recuerda.
    auto* split = new SideSplitter(QStringLiteral("casesSplit"), QStringLiteral("cases/listWidth"), 290);
    ui::hbox(this, 0, 0)->addWidget(split);
    auto* list = buildListPane();
    split->setPanes(list, buildEditor());

    // Al escribir en el editor cada tecla cambia el caso; la lista se rehace una vez al parar.
    m_listRefresh = new QTimer(this);
    m_listRefresh->setSingleShot(true);
    m_listRefresh->setInterval(250);
    connect(m_listRefresh, &QTimer::timeout, this, &CasesView::refreshList);

    connect(&m_store, &TestCaseStore::suitesChanged, this, &CasesView::refreshSuites);
    connect(&m_store, &TestCaseStore::casesChanged, this, [this]() { refreshSuites(); refreshList(); });
    connect(&m_store, &TestCaseStore::selectionChanged, this, [this](const QString&) { refreshList(); loadEditor(); });
    connect(&m_store, &TestCaseStore::caseChanged, this, &CasesView::onCaseChanged);
    connect(&m_run, &RunController::runChanged, this, &CasesView::refreshList);
    connect(&m_history, &RunHistoryStore::historyChanged, this, &CasesView::refreshHistory);
    refreshSuites();
    refreshList();
    loadEditor();
}

// ---- Lista ---------------------------------------------------------------------------------

QWidget* CasesView::buildListPane() {
    auto* pane = ui::card("list-pane");
    pane->setObjectName(QStringLiteral("casesList"));
    pane->setMinimumWidth(240);
    pane->setMaximumWidth(560);
    pane->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* v = ui::vbox(pane, 0, 0);

    auto* head = new QWidget;
    auto* hv = ui::vbox(head, 16, 10);
    hv->setContentsMargins(16, 18, 16, 10);
    auto* titleRow = new QWidget;
    auto* th = ui::hbox(titleRow, 0, 6);
    th->addWidget(ui::label(tr("Casos"), "h1-sm"));
    m_total = ui::label(QString(), "muted");
    th->addWidget(m_total, 1, Qt::AlignBottom);
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

    // Búsqueda siempre a la vista; los filtros, plegados tras su botón.
    auto* searchRow = new QWidget;
    auto* sh = ui::hbox(searchRow, 0, 6);
    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("caseSearch"));
    m_search->setPlaceholderText(tr("Buscar casos…"));
    m_search->setToolTip(tr("Busca por título, ID, suite, etiqueta o componente"));
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& t) { m_filter.text = t; refreshList(); });
    sh->addWidget(m_search, 1);
    m_filtersButton = smallButton(tr("Filtros"), "outline", tr("Filtrar por suite, estado, prioridad o última ejecución"));
    m_filtersButton->setObjectName(QStringLiteral("caseFiltersToggle"));
    m_filtersButton->setCheckable(true);
    sh->addWidget(m_filtersButton);
    hv->addWidget(searchRow);

    m_filtersPanel = new QWidget;
    m_filtersPanel->setObjectName(QStringLiteral("caseFilters"));
    auto* fv = ui::vbox(m_filtersPanel, 0, 6);
    m_filtersPanel->setVisible(false);
    connect(m_filtersButton, &QPushButton::toggled, m_filtersPanel, &QWidget::setVisible);
    hv->addWidget(m_filtersPanel);

    // Suite: desplegable con búsqueda en vez de una fila de botones, que con muchas suites no cabe.
    m_suiteFilter = new QComboBox;
    m_suiteFilter->setObjectName(QStringLiteral("caseSuiteFilter"));
    m_suiteFilter->setAccessibleName(tr("Filtrar por suite"));
    m_suiteFilter->setToolTip(tr("Filtrar por suite: escribe para buscarla"));
    m_suiteFilter->setEditable(true);
    m_suiteFilter->setInsertPolicy(QComboBox::NoInsert);
    m_suiteFilter->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_suiteFilter->setMaxVisibleItems(15);
    m_suiteFilter->completer()->setFilterMode(Qt::MatchContains);
    m_suiteFilter->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    m_suiteFilter->completer()->setCompletionMode(QCompleter::PopupCompletion);
    m_suiteFilter->lineEdit()->setPlaceholderText(tr("Buscar suite…"));
    connect(m_suiteFilter, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (i < 0) return;
        m_filter.suite = m_suiteFilter->itemData(i).toString();
        refreshList();
    });
    // Lo escrito que no es ninguna suite no filtra: se vuelve a la elegida (vacío = todas).
    connect(m_suiteFilter->lineEdit(), &QLineEdit::editingFinished, this, [this]() {
        const QString typed = m_suiteFilter->currentText().trimmed();
        const int match = typed.isEmpty() ? 0 : m_suiteFilter->findText(typed, Qt::MatchFixedString);
        const int current = m_filter.suite.isEmpty() ? 0 : m_suiteFilter->findData(m_filter.suite);
        const int target = match >= 0 ? match : current;
        if (target != m_suiteFilter->currentIndex()) m_suiteFilter->setCurrentIndex(target);
        else m_suiteFilter->setEditText(m_suiteFilter->itemText(target));
    });
    fv->addWidget(m_suiteFilter);

    // Filtros por estado, prioridad y última ejecución
    auto* combos = new QWidget;
    auto* ch = ui::hbox(combos, 0, 6);
    // Cada opción lleva el valor del enum como dato: el texto se traduce, el filtro no.
    m_statusFilter = filterBox(tr("Estado"), {{label(CaseStatus::Listo), static_cast<int>(CaseStatus::Listo)},
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
    fv->addWidget(combos);
    auto* countRow = new QWidget;
    auto* ch2 = ui::hbox(countRow, 0, 6);
    m_listCount = ui::label(QString(), "muted-sm");
    ch2->addWidget(m_listCount, 1);
    auto* clear = ui::button(tr("Quitar filtros"), "text-link");
    clear->setObjectName(QStringLiteral("caseClearFilters"));
    connect(clear, &QPushButton::clicked, this, [this]() {
        m_search->clear();
        for (auto* box : {m_suiteFilter, m_statusFilter, m_priorityFilter, m_outcomeFilter}) box->setCurrentIndex(0);
    });
    ch2->addWidget(clear);
    hv->addWidget(countRow);
    countRow->setObjectName(QStringLiteral("caseCountRow"));
    v->addWidget(head);

    QWidget* content;
    auto* sa = ui::scrollArea(&content, &m_listLayout);
    sa->setMinimumHeight(32);   // con la cabecera de filtros encima, el mínimo de un QScrollArea (68 px) pesa en una ventana baja
    m_listLayout->setContentsMargins(10, 0, 10, 16);
    m_listLayout->setSpacing(4);
    v->addWidget(sa, 1);
    return pane;
}

void CasesView::refreshSuites() {
    const QStringList suites = m_store.suites();
    if (suites == m_knownSuites && m_suiteFilter->count() > 0) return;
    m_knownSuites = suites;
    if (!suites.contains(m_filter.suite)) m_filter.suite.clear();
    {
        const QSignalBlocker block(m_suiteFilter);
        m_suiteFilter->clear();
        m_suiteFilter->addItem(tr("Todas las suites"), QString());
        for (const auto& s : suites) m_suiteFilter->addItem(s, s);
        m_suiteFilter->setCurrentIndex(m_filter.suite.isEmpty() ? 0 : std::max(0, m_suiteFilter->findData(m_filter.suite)));
    }
    if (m_form) m_form->setSuites(suites);
}

void CasesView::refreshList() {
    m_listRefresh->stop();
    ui::clearLayout(m_listLayout);
    const QString runningId = m_run.isRunning() ? m_run.state().caseId : QString();

    // Agrupados por suite, en el orden en que aparecen: la suite ya no se repite en cada fila.
    QStringList order;
    QHash<QString, QList<const TestCase*>> groups;
    for (const auto& c : m_store.cases()) {
        if (!m_filter.matches(c)) continue;
        if (!groups.contains(c.suite)) order << c.suite;
        groups[c.suite] << &c;
    }
    int shown = 0;
    for (const QString& suite : order) {
        const auto& cases = groups[suite];
        auto* header = ui::label(QStringLiteral("%1  ·  %2").arg(suite.isEmpty() ? tr("Sin suite") : suite).arg(cases.size()), "muted-sm");
        header->setStyleSheet(QStringLiteral("font-weight:700;"));
        header->setContentsMargins(6, shown == 0 ? 2 : 12, 6, 4);
        header->setWordWrap(true);
        m_listLayout->addWidget(header);

        for (const TestCase* tc : cases) {
            const TestCase& c = *tc;
            ++shown;
            auto* row = ui::button(QString(), "row");
            ui::setFlag(row, "running", c.id == runningId);
            ui::setFlag(row, "active", c.id == m_store.selectedId() && c.id != runningId);
            auto* v = ui::vbox(row, 0, 3);
            v->setContentsMargins(10, 8, 10, 8);

            auto* top = new QWidget;
            auto* th = ui::hbox(top, 0, 8);
            auto* title = new QLabel(c.title.isEmpty() ? tr("(sin título)") : c.title);
            title->setWordWrap(true);
            // Que un título largo se parta en líneas en vez de ensanchar la lista.
            title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            title->setStyleSheet(QStringLiteral("font-size:13px;font-weight:600;color:%1;")
                                     .arg(c.status == CaseStatus::Obsoleto ? theme::Muted : theme::Text));
            th->addWidget(title, 1);
            th->addWidget(ui::dot(lastRunColor(c.lastRun), 8), 0, Qt::AlignTop);
            v->addWidget(top);

            auto* bottom = new QWidget;
            auto* bh = ui::hbox(bottom, 0, 6);
            bh->addWidget(ui::label(c.id, "mono-muted"));
            bh->addWidget(ui::label(c.steps.size() == 1 ? tr("· 1 paso") : tr("· %1 pasos").arg(c.steps.size()), "muted-sm"));
            bh->addStretch(1);
            if (c.id == runningId) {
                auto* badge = ui::pill(tr("● EN EJECUCIÓN"), theme::Green, theme::Bg);
                badge->setStyleSheet(badge->styleSheet() + QStringLiteral("font-size:10.5px;font-weight:800;"));
                bh->addWidget(badge);
            } else if (c.status == CaseStatus::Obsoleto) {
                bh->addWidget(ui::label(label(c.status), "muted-sm"));
            }
            if (c.id != runningId) {   // en ejecución, la marca ocupa su sitio
                auto* prio = ui::label(label(c.priority));
                prio->setStyleSheet(QStringLiteral("font-size:11.5px;font-weight:700;color:%1;").arg(priorityColor(c.priority)));
                bh->addWidget(prio);
            }
            v->addWidget(bottom);

            // Lo que ya no se pinta en la fila (última ejecución, componente, etiquetas), en su tooltip.
            QStringList tip{tr("Última ejecución: %1").arg(c.lastRun.label())};
            if (!c.component.isEmpty()) tip << tr("Componente: %1").arg(c.component);
            if (!c.tags.isEmpty()) tip << tr("Etiquetas: %1").arg(c.tags.join(QStringLiteral(", ")));
            row->setToolTip(tip.join(QLatin1Char('\n')));

            for (auto* child : row->findChildren<QWidget*>()) child->setAttribute(Qt::WA_TransparentForMouseEvents);
            const QString id = c.id;
            connect(row, &QPushButton::clicked, this, [this, id]() { m_store.select(id); });
            row->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(row, &QWidget::customContextMenuRequested, this, [this, row, id](const QPoint& pos) {
                QMenu menu(row);
                menu.addAction(tr("Clonar caso"), this, [this, id]() { m_store.select(id); duplicateSelected(); });
                menu.addAction(tr("Eliminar caso…"), this, [this, id]() { m_store.select(id); removeSelected(); });
                menu.exec(row->mapToGlobal(pos));
            });
            m_listLayout->addWidget(row);
        }
    }
    if (shown == 0) {
        auto* e = ui::label(m_store.cases().isEmpty() ? tr("No hay casos. Crea uno o importa un archivo.") : tr("Ningún caso coincide con los filtros."), "muted");
        e->setWordWrap(true);
        e->setContentsMargins(8, 8, 8, 8);
        m_listLayout->addWidget(e);
    }
    m_total->setText(QString::number(m_store.cases().size()));
    m_listCount->setText(tr("%1 de %2 casos").arg(shown).arg(m_store.cases().size()));
    refreshFilterButton();
    m_listLayout->addStretch(1);
}

void CasesView::refreshFilterButton() {
    const int active = int(!m_filter.suite.isEmpty()) + int(m_filter.status.has_value()) + int(m_filter.priority.has_value())
                       + int(m_filter.outcome.has_value());
    m_filtersButton->setText(active ? tr("Filtros · %1").arg(active) : tr("Filtros"));
    ui::setFlag(m_filtersButton, "active", active > 0);
    m_listCount->parentWidget()->setVisible(!m_filter.isEmpty());
}

// ---- Editor --------------------------------------------------------------------------------

QWidget* CasesView::buildEditor() {
    QWidget* content;
    QVBoxLayout* outer;
    auto* sa = ui::scrollArea(&content, &outer);
    sa->setObjectName(QStringLiteral("caseEditorScroll"));
    sa->setMinimumWidth(380);
    // El editor se adapta al ancho; si algo aun así no cabe, se desplaza en vez de cortarse.
    sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    outer->setContentsMargins(32, 24, 32, 32);
    m_editor = new QWidget;
    m_editor->setMaximumWidth(860);
    auto* v = ui::vbox(m_editor, 0, 14);
    // Centrado con huecos a los lados y no con alineación: alineado, el layout lo mide a un ancho y lo
    // coloca a otro, y los campos que crecen con su texto se quedan sin el alto que piden.
    auto* center = new QHBoxLayout;
    center->addStretch(0);
    center->addWidget(m_editor, 1);
    center->addStretch(0);
    outer->addLayout(center);
    outer->addStretch(1);

    // Cabecera: id, que se guarda solo, y las acciones. El título es el primer campo del formulario.
    auto* head = new QWidget;
    auto* hh = ui::hbox(head, 0, 10);
    m_idLabel = ui::label(QString(), "eyebrow-mono");
    hh->addWidget(m_idLabel);
    auto* saved = ui::label(tr("✓ Guardado"), "muted-sm");
    saved->setToolTip(tr("Los cambios se guardan solos mientras escribes"));
    hh->addWidget(saved);
    hh->addStretch(1);
    auto* clone = smallButton(tr("Clonar"), "outline", tr("Crear una copia de este caso con sus pasos, sin ejecuciones (Ctrl+D)"));
    clone->setObjectName(QStringLiteral("cloneCase"));
    connect(clone, &QPushButton::clicked, this, &CasesView::duplicateSelected);
    hh->addWidget(clone);
    auto* more = smallButton(QStringLiteral("⋯"), "outline", tr("Más acciones"));
    more->setStyleSheet(QStringLiteral("padding:6px 8px;font-size:13px;border-radius:8px;"));
    more->setFixedWidth(32);
    auto* menu = new QMenu(more);
    menu->addAction(tr("Clonar caso"), this, &CasesView::duplicateSelected);
    menu->addAction(tr("Ver historial de ejecuciones"), this, [this]() { if (!m_store.selectedId().isEmpty()) emit historyRequested(m_store.selectedId()); });
    menu->addSeparator();
    menu->addAction(tr("Eliminar caso…"), this, &CasesView::removeSelected);
    more->setMenu(menu);
    hh->addWidget(more);
    v->addWidget(head);

    // Título, propiedades, precondiciones y pasos: el mismo formulario que el del caso nuevo.
    // Ni la historia de Jira ni el Test de Zephyr se enseñan aquí: el requerimiento es del issue y lo
    // que se enlaza son las ejecuciones, que se ven en el historial. El caso sólo lo guarda.
    // Las evidencias tampoco: son de cada ejecución y se ven en su ficha del historial.
    m_form = new CaseForm(m_store, CaseForm::Options{});
    v->addWidget(m_form);

    // Últimas ejecuciones
    auto* histBlock = new QWidget;
    auto* hv2 = ui::vbox(histBlock, 0, 6);
    hv2->setContentsMargins(0, 10, 0, 0);
    // En una línea si cabe y, si no (un editor estrecho, una letra más ancha), una cosa debajo de otra:
    // en una sola fila, su ancho mínimo sería la suma de todo y sacaría el editor por la derecha.
    auto* histHead = new WrapRow(8);
    auto* hhh = histHead->row();
    auto* histTitle = new QWidget;
    auto* hth = ui::hbox(histTitle, 0, 8);
    hth->addWidget(ui::label(tr("Últimas ejecuciones"), "h3"));
    m_historyCount = ui::label(QString(), "muted");
    hth->addWidget(m_historyCount);
    hhh->addWidget(histTitle);
    m_lastRun = ui::label(QString());
    m_lastRun->setWordWrap(true);
    hhh->addWidget(m_lastRun, 1);
    auto* all = ui::button(tr("Ver historial ›"), "text-link");
    connect(all, &QPushButton::clicked, this, [this]() { if (!m_store.selectedId().isEmpty()) emit historyRequested(m_store.selectedId()); });
    hhh->addWidget(all);
    hv2->addWidget(histHead);
    auto* histList = new QWidget;
    m_historyLayout = ui::vbox(histList, 0, 6);
    hv2->addWidget(histList);
    v->addWidget(histBlock);
    // Se mira el ancho visible y no el del editor: éste no baja de lo que pide su contenido.
    onBreakpoint(sa->viewport(), 680, [this, outer](bool narrow) {
        outer->setContentsMargins(narrow ? 14 : 32, 24, narrow ? 14 : 32, 32);
        m_form->setNarrow(narrow);
    });
    return sa;
}

void CasesView::loadEditor() {
    const TestCase* c = m_store.selected();
    m_editor->setVisible(c != nullptr);
    if (!c) return;
    m_idLabel->setText(c->id);
    m_form->load();
    refreshLastRun();
    refreshHistory();
}

void CasesView::refreshLastRun() {
    const TestCase* c = m_store.selected();
    if (!c) return;
    m_lastRun->setText(QStringLiteral("· %1").arg(c->lastRun.label()));
    m_lastRun->setStyleSheet(QStringLiteral("font-weight:600;color:%1;").arg(lastRunColor(c->lastRun)));
}

void CasesView::refreshHistory() {
    ui::clearLayout(m_historyLayout);
    const TestCase* c = m_store.selected();
    if (!c) return;
    const auto runs = m_history.runsForCase(c->id);
    m_historyCount->setText(QString::number(runs.size()));
    if (runs.isEmpty()) {
        // Que parta en líneas: si no, su ancho entero es el mínimo del editor y lo saca por la derecha.
        auto* none = ui::label(tr("Este caso todavía no se ha ejecutado."), "muted-sm");
        none->setWordWrap(true);
        m_historyLayout->addWidget(none);
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

        h->addStretch(1);
        const PlanRun* p = r.planRunId.isEmpty() ? nullptr : m_history.findPlan(r.planRunId);
        h->addWidget(ui::label(p ? p->name : tr("Ejecución suelta"), "muted-sm"));
        h->addWidget(ui::label(QStringLiteral("›"), "muted"));
        m_historyLayout->addWidget(row);
    }
}

void CasesView::onCaseChanged(const QString& id) {
    refreshSuites();
    // Mientras se escribe en el formulario la lista se rehace una vez al parar, no en cada tecla.
    if (m_form->isEditing()) m_listRefresh->start();
    else refreshList();
    if (id != m_store.selectedId()) return;
    // Un cambio del propio formulario no lo rehace (perdería el campo con foco); sólo lo derivado.
    if (m_form->isEditing()) refreshLastRun();
    else loadEditor();
}

// ---- Acciones ------------------------------------------------------------------------------

void CasesView::focusSearch() {
    m_search->setFocus(Qt::ShortcutFocusReason);
    m_search->selectAll();
}

void CasesView::duplicateSelected() {
    const QString src = m_store.selectedId();
    if (src.isEmpty()) return;
    const QString id = m_store.duplicateCase(src);
    if (!id.isEmpty()) emit toast(tr("%1 clonado como %2").arg(src, id), theme::Green);
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
