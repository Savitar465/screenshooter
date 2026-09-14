#include "IssuesView.h"

#include "application/AppContext.h"
#include "presentation/theme/Theme.h"
#include "presentation/views/JiraPublishDialog.h"
#include "presentation/views/ProjectSetupDialog.h"
#include "presentation/views/RequirementImportDialog.h"
#include "presentation/widgets/ChoiceDialog.h"
#include "presentation/widgets/TextArea.h"
#include "presentation/widgets/Ui.h"

#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QInputDialog>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace qaflow {

namespace {
constexpr int kMaxResults = 20;

QString stateColor(IssueState s) {
    switch (s) {
        case IssueState::Pending: return theme::Muted;
        case IssueState::Preparing: return theme::Amber;
        case IssueState::Testing: return theme::Blue;
        case IssueState::Done: return theme::Green;
    }
    return theme::Muted;
}

QString verdictColor(Verdict v) {
    switch (v) {
        case Verdict::Superado: return theme::Green;
        case Verdict::Fallido: return theme::Red;
        case Verdict::Bloqueado: return theme::Amber;
    }
    return theme::Muted;
}

QString when(const QDateTime& dt) { return dt.isValid() ? dt.toString(QStringLiteral("dd/MM/yyyy HH:mm")) : QStringLiteral("—"); }
QString day(const QDate& d) { return d.isValid() ? d.toString(QStringLiteral("dd/MM/yyyy")) : QStringLiteral("—"); }

QComboBox* filterBox(const QString& all, const QList<std::pair<QString, int>>& items) {
    auto* b = new QComboBox;
    b->addItem(all, -1);
    for (const auto& [text, value] : items) b->addItem(text, value);
    b->setStyleSheet(QStringLiteral("font-size:11.5px;padding:3px 6px;"));
    return b;
}

QPushButton* smallButton(const QString& text, const char* role, const QString& tip = QString()) {
    auto* b = ui::button(text, role);
    b->setStyleSheet(QStringLiteral("padding:5px 10px;font-size:12px;border-radius:8px;"));
    if (!tip.isEmpty()) b->setToolTip(tip);
    return b;
}

QWidget* field(const QString& title, QWidget* w) {
    auto* box = new QWidget;
    auto* v = ui::vbox(box, 0, 6);
    v->addWidget(ui::label(title.toUpper(), "eyebrow"));
    v->addWidget(w);
    return box;
}

QString chipStyle(const QString& color) {
    return QStringLiteral("background:%1;color:%2;border-radius:6px;padding:2px 8px;font-size:11px;font-weight:700;").arg(theme::tint(color, 38), color);
}

/// Tarjeta de un bloque del issue: barra de color, cabecera con el título a la izquierda y acciones a la
/// derecha, y el cuerpo debajo.
QFrame* sectionCard(const QString& accent, QLabel** header, QHBoxLayout** actions, QVBoxLayout** body) {
    auto* card = ui::card("card");
    auto* h = ui::hbox(card, 0, 0);
    h->addWidget(ui::accentBar(accent));
    auto* content = new QWidget;
    auto* v = ui::vbox(content, 0, 10);
    v->setContentsMargins(18, 14, 18, 16);
    auto* head = new QWidget;
    auto* hh = ui::hbox(head, 0, 8);
    *header = ui::label(QString(), "eyebrow");
    hh->addWidget(*header, 1);
    *actions = hh;
    v->addWidget(head);
    auto* bodyWidget = new QWidget;
    *body = ui::vbox(bodyWidget, 0, 6);
    v->addWidget(bodyWidget);
    h->addWidget(content, 1);
    return card;
}

QFrame* listRow(QHBoxLayout** layout) {
    auto* row = ui::card("card-flat");
    *layout = ui::hbox(row, 0, 10);
    (*layout)->setContentsMargins(12, 7, 8, 7);
    return row;
}

QPushButton* unlinkButton(const QString& tip) {
    auto* b = ui::button(QStringLiteral("×"), "icon");
    b->setToolTip(tip);
    b->setFixedSize(26, 22);
    return b;
}
} // namespace

IssuesView::IssuesView(const AppContext& ctx, QWidget* parent)
    : QWidget(parent), m_issues(*ctx.issues), m_cases(*ctx.cases), m_plans(*ctx.plan), m_history(*ctx.history),
      m_requirements(ctx.requirements), m_publish(ctx.issuePublish), m_bugs(ctx.bugs), m_projects(ctx.projects),
      m_projectId(ctx.projectId) {
    auto* root = ui::hbox(this, 0, 0);
    buildListPane(root);
    buildDetail(root);

    m_notesTimer = new QTimer(this);
    m_notesTimer->setSingleShot(true);
    m_notesTimer->setInterval(600);
    connect(m_notesTimer, &QTimer::timeout, this, &IssuesView::commitNotes);

    connect(&m_issues, &IssueStore::issuesChanged, this, [this]() { refreshList(); loadDetail(); });
    connect(&m_issues, &IssueStore::selectionChanged, this, [this]() {
        commitNotes();   // las notas a medio escribir son del issue anterior
        refreshList();
        loadDetail();
    });
    // Casos, planes y ejecuciones cambian a menudo mientras se trabaja en otras pantallas: sólo se
    // redibuja si la pantalla se ve, y al volver a ella se refresca entera.
    auto refreshIfVisible = [this]() { if (isVisible()) loadDetail(); };
    connect(&m_cases, &TestCaseStore::casesChanged, this, refreshIfVisible);
    connect(&m_cases, &TestCaseStore::caseChanged, this, refreshIfVisible);
    connect(&m_plans, &PlanStore::plansChanged, this, refreshIfVisible);
    connect(&m_plans, &PlanStore::planChanged, this, refreshIfVisible);
    connect(&m_history, &RunHistoryStore::historyChanged, this, refreshIfVisible);
    if (m_projects) connect(m_projects, &ProjectStore::projectsChanged, this, &IssuesView::refreshList);
    if (ctx.settings) connect(ctx.settings, &SettingsStore::trackerChanged, this, refreshIfVisible);
    refreshList();
    loadDetail();
}

IssuesView::~IssuesView() { commitNotes(); }

void IssuesView::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    refreshList();
    loadDetail();
}

void IssuesView::hideEvent(QHideEvent* e) {
    commitNotes();
    QWidget::hideEvent(e);
}

const Issue* IssuesView::selected() const { return m_issues.find(m_issues.selectedId()); }

QString IssuesView::linkedSystem() const {
    const Project* project = m_projects ? m_projects->find(m_projectId) : nullptr;
    return project ? project->requirementSystem : QString();
}

void IssuesView::focusSearch() {
    m_search->setFocus();
    m_search->selectAll();
}

// ---- Lista ---------------------------------------------------------------------------------------

void IssuesView::buildListPane(QHBoxLayout* root) {
    auto* pane = ui::card("list-pane");
    pane->setMinimumWidth(280);
    pane->setMaximumWidth(340);
    pane->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* v = ui::vbox(pane, 0, 0);

    auto* head = new QWidget;
    auto* hv = ui::vbox(head, 16, 10);
    hv->setContentsMargins(16, 18, 16, 12);
    auto* titleRow = new QWidget;
    auto* th = ui::hbox(titleRow, 0, 6);
    th->addWidget(ui::label(tr("Issues"), "h1-sm"), 1);
    auto* create = smallButton(tr("+ Nuevo"), "primary", tr("Issue creado a mano, sin requerimiento de GESREQ"));
    create->setObjectName(QStringLiteral("issuesNew"));
    connect(create, &QPushButton::clicked, this, [this]() {
        m_issues.createIssue(tr("Nuevo issue"));
        m_title->setFocus();
        m_title->selectAll();
    });
    th->addWidget(create);
    hv->addWidget(titleRow);

    m_consult = smallButton(tr("Consultar GESREQ"), "outline");
    m_consult->setObjectName(QStringLiteral("issuesConsult"));
    m_consult->setVisible(m_requirements != nullptr);
    connect(m_consult, &QPushButton::clicked, this, &IssuesView::consultRequirements);
    hv->addWidget(m_consult);

    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("issueSearch"));
    m_search->setPlaceholderText(tr("Buscar por título, número de GREQ, sistema, solicitante…"));
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& t) { m_filter.text = t; refreshList(); });
    hv->addWidget(m_search);

    auto* combos = new QWidget;
    auto* ch = ui::hbox(combos, 0, 6);
    // Cada opción lleva el valor del enum como dato: el texto se traduce, el filtro no.
    m_stateFilter = filterBox(tr("Estado"), {{label(IssueState::Pending), static_cast<int>(IssueState::Pending)},
                                             {label(IssueState::Preparing), static_cast<int>(IssueState::Preparing)},
                                             {label(IssueState::Testing), static_cast<int>(IssueState::Testing)},
                                             {label(IssueState::Done), static_cast<int>(IssueState::Done)}});
    m_priorityFilter = filterBox(tr("Prioridad"), {{label(Priority::Alta), static_cast<int>(Priority::Alta)},
                                                   {label(Priority::Media), static_cast<int>(Priority::Media)},
                                                   {label(Priority::Baja), static_cast<int>(Priority::Baja)}});
    m_jiraFilter = filterBox(tr("Jira"), {{tr("Publicados"), 1}, {tr("Sin publicar"), 0}});
    m_stateFilter->setObjectName(QStringLiteral("issueStateFilter"));
    m_jiraFilter->setObjectName(QStringLiteral("issueJiraFilter"));
    connect(m_stateFilter, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_filter.state = i <= 0 ? std::nullopt : std::optional<IssueState>(static_cast<IssueState>(m_stateFilter->currentData().toInt()));
        refreshList();
    });
    connect(m_priorityFilter, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_filter.priority = i <= 0 ? std::nullopt : std::optional<Priority>(static_cast<Priority>(m_priorityFilter->currentData().toInt()));
        refreshList();
    });
    connect(m_jiraFilter, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_filter.published = i <= 0 ? std::nullopt : std::optional<bool>(m_jiraFilter->currentData().toInt() == 1);
        refreshList();
    });
    ch->addWidget(m_stateFilter, 1);
    ch->addWidget(m_priorityFilter, 1);
    ch->addWidget(m_jiraFilter, 1);
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

void IssuesView::refreshList() {
    const QString system = linkedSystem();
    m_consult->setToolTip(system.isEmpty() ? tr("Vincula antes un sistema de GESREQ a este proyecto en Ajustes")
                                           : tr("Leer la bandeja de control de calidad e importar los requerimientos de %1").arg(system));
    ui::clearLayout(m_listLayout);
    int shown = 0;
    for (const auto& issue : m_issues.issues()) {
        if (!m_filter.matches(issue)) continue;
        ++shown;
        auto* row = ui::button(QString(), "row");
        row->setObjectName(QStringLiteral("issueRow-%1").arg(issue.id));
        ui::setFlag(row, "active", issue.id == m_issues.selectedId());
        auto* v = ui::vbox(row, 0, 4);
        v->setContentsMargins(12, 10, 12, 10);

        auto* top = new QWidget;
        auto* th = ui::hbox(top, 0, 8);
        th->addWidget(ui::label(issue.id, "mono-muted"));
        if (issue.isImported()) th->addWidget(ui::label(QStringLiteral("· GREQ %1").arg(issue.requirement.data.id), "mono-muted"));
        th->addStretch(1);
        const auto pill = theme::priorityPill(toString(issue.priority));
        th->addWidget(ui::pill(label(issue.priority), pill.bg, pill.fg));
        v->addWidget(top);

        auto* title = new QLabel(issue.title);
        title->setWordWrap(true);
        title->setStyleSheet(QStringLiteral("font-size:13.5px;font-weight:600;color:%1;").arg(theme::Text));
        v->addWidget(title);

        auto* bottom = new QWidget;
        auto* bh = ui::hbox(bottom, 0, 6);
        auto* state = new QLabel(label(issue.state));
        state->setStyleSheet(QStringLiteral("font-size:11.5px;font-weight:600;color:%1;").arg(stateColor(issue.state)));
        bh->addWidget(state);
        bh->addStretch(1);
        if (!issue.requirement.changes.isEmpty()) bh->addWidget(ui::pill(tr("CAMBIOS"), theme::tint(theme::Amber, 46), theme::AmberSoft));
        if (issue.requirement.missing) bh->addWidget(ui::pill(tr("FUERA DE LA BANDEJA"), theme::tint(theme::Muted, 38), theme::Muted));
        if (issue.isPublished()) bh->addWidget(ui::label(issue.publication.key, "mono-muted"));
        v->addWidget(bottom);

        if (issue.isImported()) {
            const QString where = issue.requirement.data.systemCode + QStringLiteral(" · ") + issue.requirement.data.states.join(QStringLiteral(" + "));
            auto* meta = ui::label(ui::elide(where, 48), "muted-sm");
            meta->setStyleSheet(QStringLiteral("font-size:11px;"));
            v->addWidget(meta);
        }

        for (auto* child : row->findChildren<QWidget*>()) child->setAttribute(Qt::WA_TransparentForMouseEvents);
        connect(row, &QPushButton::clicked, this, [this, id = issue.id]() { m_issues.select(id); });
        m_listLayout->addWidget(row);
    }
    if (shown == 0) {
        auto* e = ui::label(m_issues.issues().isEmpty() ? tr("Todavía no hay issues. Consulta GESREQ para importar tus requerimientos o crea uno a mano.")
                                                        : tr("Ningún issue coincide con los filtros."),
                            "muted");
        e->setWordWrap(true);
        e->setContentsMargins(8, 8, 8, 8);
        m_listLayout->addWidget(e);
    }
    m_listCount->setText(m_filter.isEmpty() ? tr("%1 issues").arg(shown) : tr("%1 de %2 issues").arg(shown).arg(m_issues.issues().size()));
    m_listLayout->addStretch(1);
}

// ---- Detalle -------------------------------------------------------------------------------------

void IssuesView::buildDetail(QHBoxLayout* root) {
    QWidget* content;
    QVBoxLayout* outer;
    auto* sa = ui::scrollArea(&content, &outer);
    outer->setContentsMargins(28, 24, 28, 28);
    auto* page = new QWidget;
    page->setMaximumWidth(920);
    auto* pv = ui::vbox(page, 0, 0);
    outer->addWidget(page, 0, Qt::AlignTop);
    root->addWidget(sa, 1);

    auto* empty = ui::label(tr("Selecciona un issue de la lista, consulta GESREQ para importar los requerimientos de tu bandeja o crea uno a mano."), "muted");
    empty->setWordWrap(true);
    m_empty = empty;
    pv->addWidget(m_empty);

    m_detail = new QWidget;
    auto* v = ui::vbox(m_detail, 0, 16);
    pv->addWidget(m_detail);

    // Cabecera: identidad local, origen y representación en Jira.
    auto* head = new QWidget;
    auto* hh = ui::hbox(head, 0, 8);
    m_idLabel = ui::label(QString(), "eyebrow-mono");
    hh->addWidget(m_idLabel);
    m_sourceChip = new QLabel;
    hh->addWidget(m_sourceChip);
    m_jiraChip = new QLabel;
    m_jiraChip->setObjectName(QStringLiteral("issueJira"));
    hh->addWidget(m_jiraChip);
    hh->addStretch(1);
    auto* remove = smallButton(tr("Eliminar…"), "ghost", tr("Borra el issue; sus casos, planes y resultados se conservan"));
    remove->setObjectName(QStringLiteral("issueRemove"));
    connect(remove, &QPushButton::clicked, this, &IssuesView::removeSelected);
    hh->addWidget(remove);
    v->addWidget(head);

    m_title = new QLineEdit;
    m_title->setObjectName(QStringLiteral("issueTitle"));
    m_title->setProperty("role", QStringLiteral("title"));
    connect(m_title, &QLineEdit::editingFinished, this, [this]() {
        const Issue* issue = selected();
        if (!issue) return;
        const QString text = m_title->text().trimmed();
        if (text.isEmpty()) { m_title->setText(issue->title); return; }
        if (text != issue->title) editSelected([&text](Issue& i) { i.title = text; });
    });
    v->addWidget(m_title);

    auto* meta = new QWidget;
    auto* mg = new QGridLayout(meta);
    mg->setContentsMargins(0, 0, 0, 0);
    mg->setHorizontalSpacing(12);
    m_state = new QComboBox;
    m_state->setObjectName(QStringLiteral("issueState"));
    for (const auto s : {IssueState::Pending, IssueState::Preparing, IssueState::Testing, IssueState::Done}) m_state->addItem(label(s), static_cast<int>(s));
    m_state->setToolTip(tr("Estado del trabajo de QA en QAflow: no cambia con el estado del requerimiento en GESREQ ni con el de Jira"));
    m_priority = new QComboBox;
    m_priority->setObjectName(QStringLiteral("issuePriority"));
    for (const auto p : {Priority::Alta, Priority::Media, Priority::Baja}) m_priority->addItem(label(p), static_cast<int>(p));
    connect(m_state, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_loadingDetail) return;
        const auto s = static_cast<IssueState>(m_state->currentData().toInt());
        editSelected([s](Issue& i) { i.state = s; });
    });
    connect(m_priority, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_loadingDetail) return;
        const auto p = static_cast<Priority>(m_priority->currentData().toInt());
        editSelected([p](Issue& i) { i.priority = p; });
    });
    mg->addWidget(field(tr("Estado de QA"), m_state), 0, 0);
    mg->addWidget(field(tr("Prioridad"), m_priority), 0, 1);
    mg->setColumnStretch(0, 1);
    mg->setColumnStretch(1, 1);
    v->addWidget(meta);

    // Requerimiento de GESREQ
    QLabel* requirementHeader;
    QHBoxLayout* requirementActions;
    QVBoxLayout* requirementBody;
    auto* requirementCard = sectionCard(theme::Cyan, &requirementHeader, &requirementActions, &requirementBody);
    requirementCard->setObjectName(QStringLiteral("issueRequirement"));
    m_requirementCard = requirementCard;
    requirementHeader->setText(tr("REQUERIMIENTO DE GESREQ"));
    m_loadDetail = smallButton(tr("Cargar ficha"), "outline", tr("Leer de GESREQ la ficha completa: alcance, secciones y adjuntos"));
    m_loadDetail->setObjectName(QStringLiteral("issueLoadDetail"));
    connect(m_loadDetail, &QPushButton::clicked, this, &IssuesView::loadRequirementDetail);
    m_openRequirement = smallButton(tr("Abrir en GESREQ"), "outline", tr("Abre la ficha en el navegador (hace falta haber entrado en GESREQ)"));
    connect(m_openRequirement, &QPushButton::clicked, this, [this]() {
        if (const Issue* issue = selected()) emit openUrlRequested(issue->requirement.data.detailUrl);
    });
    requirementActions->addWidget(m_loadDetail);
    requirementActions->addWidget(m_openRequirement);

    auto* changes = ui::card("card-flat");
    changes->setObjectName(QStringLiteral("issueChanges"));
    m_changes = changes;
    auto* chh = ui::hbox(changes, 0, 10);
    chh->setContentsMargins(12, 10, 10, 10);
    m_changesText = new QLabel;
    m_changesText->setWordWrap(true);
    m_changesText->setTextFormat(Qt::RichText);
    m_changesText->setStyleSheet(QStringLiteral("color:%1;").arg(theme::AmberSoft));
    chh->addWidget(m_changesText, 1);
    auto* acknowledge = smallButton(tr("Marcar como revisado"), "outline");
    acknowledge->setObjectName(QStringLiteral("issueAcknowledge"));
    connect(acknowledge, &QPushButton::clicked, this, [this]() {
        if (const Issue* issue = selected()) m_issues.acknowledgeChanges(issue->id);
    });
    chh->addWidget(acknowledge, 0, Qt::AlignTop);
    requirementBody->addWidget(changes);

    auto* missing = ui::card("card-flat");
    missing->setObjectName(QStringLiteral("issueMissing"));
    m_missing = missing;
    auto* mh = ui::hbox(missing, 0, 10);
    mh->setContentsMargins(12, 10, 10, 10);
    m_missingText = ui::label(QString(), "muted-sm");
    m_missingText->setWordWrap(true);
    mh->addWidget(m_missingText, 1);
    requirementBody->addWidget(missing);

    m_requirementInfo = new QLabel;
    m_requirementInfo->setObjectName(QStringLiteral("issueRequirementInfo"));
    m_requirementInfo->setWordWrap(true);
    m_requirementInfo->setTextFormat(Qt::RichText);
    m_requirementInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    requirementBody->addWidget(m_requirementInfo);
    m_detailInfo = new QLabel;
    m_detailInfo->setObjectName(QStringLiteral("issueRequirementDetail"));
    m_detailInfo->setWordWrap(true);
    m_detailInfo->setTextFormat(Qt::RichText);
    m_detailInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    requirementBody->addWidget(m_detailInfo);
    auto* attachments = new QWidget;
    m_attachments = ui::vbox(attachments, 0, 2);
    requirementBody->addWidget(attachments);
    v->addWidget(requirementCard);

    // Publicación en el gestor
    QLabel* jiraHeader;
    QHBoxLayout* jiraActions;
    QVBoxLayout* jiraBody;
    auto* jiraCard = sectionCard(theme::Blue, &jiraHeader, &jiraActions, &jiraBody);
    jiraCard->setObjectName(QStringLiteral("issueJiraCard"));
    m_jiraCard = jiraCard;
    jiraHeader->setText(tr("PUBLICACIÓN EN EL GESTOR"));
    m_publishButton = smallButton(tr("Publicar…"), "outline", tr("Crea el issue en el gestor a partir de este, revisándolo antes"));
    m_publishButton->setObjectName(QStringLiteral("issuePublish"));
    connect(m_publishButton, &QPushButton::clicked, this, &IssuesView::publishToJira);
    m_linkJiraButton = smallButton(tr("Vincular issue…"), "outline", tr("Enlaza uno que ya existe en el gestor en vez de crear otro"));
    m_linkJiraButton->setObjectName(QStringLiteral("issueLinkJira"));
    connect(m_linkJiraButton, &QPushButton::clicked, this, &IssuesView::linkJira);
    m_openJiraButton = smallButton(tr("Abrir"), "outline", tr("Abrir el issue del gestor en el navegador"));
    m_openJiraButton->setObjectName(QStringLiteral("issueOpenJira"));
    connect(m_openJiraButton, &QPushButton::clicked, this, [this]() {
        if (const Issue* issue = selected()) emit openUrlRequested(issue->publication.url);
    });
    m_refreshJiraButton = smallButton(tr("Actualizar estado"), "outline");
    m_refreshJiraButton->setObjectName(QStringLiteral("issueRefreshJira"));
    connect(m_refreshJiraButton, &QPushButton::clicked, this, &IssuesView::refreshJiraStatus);
    m_unlinkJiraButton = smallButton(tr("Desvincular"), "ghost", tr("QAflow olvida la representación; en el gestor no se borra nada"));
    m_unlinkJiraButton->setObjectName(QStringLiteral("issueUnlinkJira"));
    connect(m_unlinkJiraButton, &QPushButton::clicked, this, &IssuesView::unlinkJira);
    for (auto* b : {m_publishButton, m_linkJiraButton, m_openJiraButton, m_refreshJiraButton, m_unlinkJiraButton}) jiraActions->addWidget(b);

    auto* pending = ui::card("card-flat");
    pending->setObjectName(QStringLiteral("issueJiraPending"));
    m_jiraPending = pending;
    auto* ph = ui::hbox(pending, 0, 10);
    ph->setContentsMargins(12, 10, 10, 10);
    m_jiraPendingText = ui::label(QString(), "muted-sm");
    m_jiraPendingText->setWordWrap(true);
    m_jiraPendingText->setStyleSheet(QStringLiteral("color:%1;").arg(theme::AmberSoft));
    ph->addWidget(m_jiraPendingText, 1);
    m_updateJiraButton = smallButton(tr("Actualizar en el gestor…"), "outline");
    m_updateJiraButton->setObjectName(QStringLiteral("issueUpdateJira"));
    connect(m_updateJiraButton, &QPushButton::clicked, this, [this]() { openPublishDialog(true); });
    ph->addWidget(m_updateJiraButton, 0, Qt::AlignTop);
    jiraBody->addWidget(pending);

    auto* uncertain = ui::card("card-flat");
    uncertain->setObjectName(QStringLiteral("issueJiraUncertain"));
    m_jiraUncertain = uncertain;
    auto* uh = ui::hbox(uncertain, 0, 10);
    uh->setContentsMargins(12, 10, 10, 10);
    m_jiraUncertainText = ui::label(QString(), "muted-sm");
    m_jiraUncertainText->setWordWrap(true);
    m_jiraUncertainText->setStyleSheet(QStringLiteral("color:%1;").arg(theme::AmberSoft));
    uh->addWidget(m_jiraUncertainText, 1);
    jiraBody->addWidget(uncertain);

    m_jiraInfo = new QLabel;
    m_jiraInfo->setObjectName(QStringLiteral("issueJiraInfo"));
    m_jiraInfo->setWordWrap(true);
    m_jiraInfo->setTextFormat(Qt::RichText);
    m_jiraInfo->setTextInteractionFlags(Qt::TextSelectableByMouse);
    jiraBody->addWidget(m_jiraInfo);
    v->addWidget(jiraCard);

    // Notas de QA
    QLabel* notesHeader;
    QHBoxLayout* notesActions;
    QVBoxLayout* notesBody;
    v->addWidget(sectionCard(theme::Muted, &notesHeader, &notesActions, &notesBody));
    notesHeader->setText(tr("NOTAS DE QA"));
    m_notes = new TextArea(4);
    m_notes->setObjectName(QStringLiteral("issueNotes"));
    m_notes->setPlaceholderText(tr("Ambiente, datos de prueba, acuerdos… Son de QAflow: consultar GESREQ no las cambia."));
    connect(m_notes, &TextArea::edited, this, [this]() {
        m_notesIssueId = m_issues.selectedId();
        m_notesTimer->start();
    });
    notesBody->addWidget(m_notes);

    // Casos
    QHBoxLayout* casesActions;
    v->addWidget(sectionCard(theme::Violet, &m_casesHeader, &casesActions, &m_casesList));
    m_casesHeader->setObjectName(QStringLiteral("issueCasesHeader"));
    auto* newCase = smallButton(tr("+ Nuevo caso"), "outline", tr("Crea un caso con el título del issue, lo vincula y lo abre para escribir sus pasos"));
    newCase->setObjectName(QStringLiteral("issueNewCase"));
    connect(newCase, &QPushButton::clicked, this, &IssuesView::createCase);
    auto* linkCase = smallButton(tr("Vincular caso…"), "outline", tr("Un caso puede validar varios issues"));
    linkCase->setObjectName(QStringLiteral("issueLinkCase"));
    connect(linkCase, &QPushButton::clicked, this, &IssuesView::pickCase);
    casesActions->addWidget(newCase);
    casesActions->addWidget(linkCase);

    // Planes
    QHBoxLayout* plansActions;
    v->addWidget(sectionCard(theme::Amber, &m_plansHeader, &plansActions, &m_plansList));
    m_plansHeader->setObjectName(QStringLiteral("issuePlansHeader"));
    auto* newPlan = smallButton(tr("+ Plan con sus casos"), "outline", tr("Crea un plan con los casos del issue y lo abre para ajustarlo"));
    newPlan->setObjectName(QStringLiteral("issueNewPlan"));
    connect(newPlan, &QPushButton::clicked, this, &IssuesView::createPlan);
    auto* linkPlan = smallButton(tr("Vincular plan…"), "outline");
    linkPlan->setObjectName(QStringLiteral("issueLinkPlan"));
    connect(linkPlan, &QPushButton::clicked, this, &IssuesView::pickPlan);
    plansActions->addWidget(newPlan);
    plansActions->addWidget(linkPlan);

    // Resultados
    QHBoxLayout* resultsActions;
    v->addWidget(sectionCard(theme::Blue, &m_resultsHeader, &resultsActions, &m_resultsList));
    m_resultsHeader->setObjectName(QStringLiteral("issueResultsHeader"));
}

void IssuesView::loadDetail() {
    const Issue* found = selected();
    m_empty->setVisible(!found);
    m_detail->setVisible(found != nullptr);
    if (!found) return;
    const Issue issue = *found;   // copia: los refrescos no deben depender de un puntero a la lista

    m_idLabel->setText(issue.id);
    if (issue.isImported()) {
        m_sourceChip->setText(tr("GESREQ %1 · %2").arg(issue.requirement.data.id, issue.requirement.data.systemCode));
        m_sourceChip->setStyleSheet(chipStyle(theme::Cyan));
    } else {
        m_sourceChip->setText(tr("Creado a mano"));
        m_sourceChip->setStyleSheet(chipStyle(theme::Muted));
    }
    m_jiraChip->setText(issue.isPublished() ? issue.publication.key : tr("Sin publicar en Jira"));
    m_jiraChip->setStyleSheet(chipStyle(issue.isPublished() ? theme::Blue : theme::Muted));

    if (!m_selfEdit) {
        m_loadingDetail = true;
        m_title->setText(issue.title);
        m_state->setCurrentIndex(std::max(0, m_state->findData(static_cast<int>(issue.state))));
        m_priority->setCurrentIndex(std::max(0, m_priority->findData(static_cast<int>(issue.priority))));
        // Las notas que se están escribiendo de este issue no se pisan con lo guardado.
        if (m_notesIssueId != issue.id) m_notes->setTextSilently(issue.notes);
        m_loadingDetail = false;
    }
    refreshRequirement(issue);
    refreshJira(issue);
    refreshCases(issue);
    refreshPlans(issue);
    refreshResults(issue);
}

void IssuesView::refreshRequirement(const Issue& issue) {
    m_requirementCard->setVisible(issue.isImported());
    ui::clearLayout(m_attachments);
    if (!issue.isImported()) return;
    const RequirementLink& link = issue.requirement;
    const ExternalRequirement& r = link.data;

    m_changes->setVisible(!link.changes.isEmpty());
    if (!link.changes.isEmpty()) {
        QStringList lines;
        for (const auto& c : link.changes)
            lines << QStringLiteral("<b>%1</b>: %2 → %3").arg(requirementFieldLabel(c.field).toHtmlEscaped(),
                                                             c.before.isEmpty() ? tr("(vacío)") : c.before.toHtmlEscaped(),
                                                             c.after.isEmpty() ? tr("(vacío)") : c.after.toHtmlEscaped());
        m_changesText->setText(tr("⚠ Cambió en GESREQ desde la última revisión:") + QStringLiteral("<br>") + lines.join(QStringLiteral("<br>")));
    }
    m_missing->setVisible(link.missing);
    m_missingText->setText(tr("Ya no está en tu bandeja de control de calidad (se vio por última vez el %1). El issue y sus pruebas se conservan.")
                               .arg(when(link.fetchedAt)));

    QStringList rows;
    auto row = [&rows](const QString& name, const QString& value) {
        if (value.trimmed().isEmpty()) return;
        rows << QStringLiteral("<tr><td style='color:%1;padding-right:14px;'>%2</td><td>%3</td></tr>").arg(theme::Muted, name.toHtmlEscaped(), value.toHtmlEscaped());
    };
    QStringList requester;
    for (const QString& part : {r.requester, r.requestingUnit})
        if (!part.trimmed().isEmpty()) requester << part;
    row(tr("Sistema"), r.system);
    row(tr("Descripción corta"), r.summary);
    row(tr("Estado en GESREQ"), r.states.join(QStringLiteral(" + ")));
    row(tr("Prioridad en GESREQ"), r.priority);
    row(tr("Asignado a QA"), r.assignedFrom.isValid() ? QStringLiteral("%1 – %2").arg(day(r.assignedFrom), day(r.assignedUntil)) : QString());
    row(tr("Solicitado"), r.requestedOn.isValid() ? day(r.requestedOn) : QString());
    row(tr("Solicitante"), requester.join(QStringLiteral(" · ")));
    row(tr("Usuario"), r.user);
    row(tr("Importado"), tr("%1 · última lectura %2").arg(when(link.importedAt), when(link.fetchedAt)));
    m_requirementInfo->setText(QStringLiteral("<table cellspacing='0' cellpadding='2'>%1</table>").arg(rows.join(QString())));

    const RequirementDetail& d = link.detail;
    const bool hasDetail = !d.id.isEmpty();
    m_loadDetail->setVisible(m_requirements != nullptr);
    if (!m_readingRequirement) m_loadDetail->setText(hasDetail ? tr("Actualizar ficha") : tr("Cargar ficha"));
    m_openRequirement->setEnabled(!r.detailUrl.isEmpty());
    m_detailInfo->setVisible(hasDetail);
    if (!hasDetail) return;
    auto multiline = [](const QString& text) { return text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")); };
    QString html = QStringLiteral("<p style='color:%1;'>%2</p>").arg(theme::Muted, tr("Ficha leída el %1").arg(when(link.detailFetchedAt)));
    if (!d.requestType.isEmpty() || !d.reference.isEmpty())
        html += QStringLiteral("<p><b>%1</b> %2</p>").arg(d.requestType.toHtmlEscaped(), d.reference.toHtmlEscaped());
    if (!d.description.isEmpty()) html += QStringLiteral("<p>%1</p>").arg(multiline(d.description));
    for (const auto& section : d.sections) {
        QStringList fields;
        for (const auto& f : section.fields)
            if (!f.value.trimmed().isEmpty())
                fields << QStringLiteral("<span style='color:%1;'>%2:</span> %3").arg(theme::Muted, f.label.toHtmlEscaped(), multiline(f.value));
        if (!fields.isEmpty()) html += QStringLiteral("<p><b>%1</b><br>%2</p>").arg(section.title.toHtmlEscaped(), fields.join(QStringLiteral("<br>")));
    }
    m_detailInfo->setText(html);
    for (const auto& a : d.attachments) {
        auto* b = smallButton(QStringLiteral("📎 %1 · %2").arg(a.label, a.fileName), "ghost", tr("Abrir en el navegador"));
        connect(b, &QPushButton::clicked, this, [this, url = a.url]() { emit openUrlRequested(url); });
        m_attachments->addWidget(b, 0, Qt::AlignLeft);
    }
}

void IssuesView::refreshJira(const Issue& issue) {
    m_jiraCard->setVisible(m_publish != nullptr);
    if (!m_publish) return;
    const IssuePublication& p = issue.publication;
    const bool published = issue.isPublished();
    const bool configured = m_publish->canPublish();
    const bool pending = m_publish->needsUpdate(issue);
    m_publishButton->setVisible(!published);
    m_publishButton->setEnabled(configured && !m_publishing);
    m_linkJiraButton->setVisible(!published);
    m_linkJiraButton->setEnabled(configured && !m_publishing);
    m_openJiraButton->setVisible(published);
    m_refreshJiraButton->setVisible(published);
    m_refreshJiraButton->setEnabled(!m_publishing);
    m_unlinkJiraButton->setVisible(published);
    m_jiraPending->setVisible(pending);
    m_jiraUncertain->setVisible(!published && p.uncertain);
    m_jiraPendingText->setText(tr("⚠ El título o la descripción cambiaron desde lo último que se publicó. En el gestor sigue lo anterior."));
    m_jiraUncertainText->setText(tr("⚠ El último envío se cortó sin respuesta (%1): puede haberse creado igualmente. Búscalo en %2 por la etiqueta %3 "
                                    "y vincúlalo; si no está, vuelve a publicar.")
                                     .arg(p.lastError, m_publish->destination(), issue.id));

    QStringList lines;
    if (published) {
        const QString where = p.project.isEmpty() ? p.tracker : QStringLiteral("%1 · %2").arg(p.tracker, p.project);
        lines << QStringLiteral("<b>%1</b> · %2").arg(p.key.toHtmlEscaped(), where.toHtmlEscaped());
        if (!p.issueType.isEmpty()) lines << tr("Tipo: %1").arg(p.issueType.toHtmlEscaped());
        lines << (p.linked ? tr("Vinculado el %1 · lo creó otra persona en el gestor").arg(when(p.publishedAt))
                           : tr("Publicado desde QAflow el %1").arg(when(p.publishedAt)));
        lines << (p.status.isEmpty() ? tr("Estado del gestor sin consultar")
                                     : tr("Estado en el gestor: %1 · consultado el %2").arg(p.status.toHtmlEscaped(), when(p.statusCheckedAt)));
    } else if (!configured) {
        lines << tr("Configura la conexión con el gestor y su proyecto en Ajustes para publicar este issue.");
    } else {
        lines << tr("Sin publicar. Se crearía en %1.").arg(m_publish->destination().toHtmlEscaped());
    }
    m_jiraInfo->setText(lines.join(QStringLiteral("<br>")));
}

void IssuesView::openPublishDialog(bool update) {
    const Issue* issue = selected();
    if (!issue || !m_publish) return;
    const QString issueId = issue->id;
    const IssueDraft draft = m_publish->draftFor(*issue);
    const QString destination = m_publish->destination();
    QPointer<IssuesView> self(this);
    // Los tipos de incidencia salen del proyecto de destino; si no se pueden leer, se escribe el tipo a mano.
    m_publish->fetchIssueTypes([self, issueId, draft, destination, update](const QStringList& types) {
        if (!self) return;
        auto* dialog = new JiraPublishDialog(update ? JiraPublishDialog::Mode::Update : JiraPublishDialog::Mode::Create,
                                             destination, draft, types, self.data());
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(dialog, &JiraPublishDialog::confirmed, self.data(), [self, issueId, update](const IssueDraft& confirmed) {
            if (!self || self->m_publishing) return;
            self->m_publishing = true;
            auto done = [self, update](const IssuePublishService::Result& r) {
                if (!self) return;
                self->m_publishing = false;
                self->loadDetail();
                if (r.ok) {
                    emit self->toast(update ? tr("%1 actualizado en el gestor").arg(r.key) : tr("%1 creado en el gestor").arg(r.key), theme::Green);
                    return;
                }
                if (r.uncertain) {
                    emit self->toast(tr("El envío se cortó sin respuesta: comprueba en el gestor si se creó antes de reintentar · %1").arg(r.error), theme::Amber);
                    return;
                }
                emit self->toast(update ? tr("No se pudo actualizar en el gestor · %1").arg(r.error) : tr("No se pudo crear en el gestor · %1").arg(r.error), theme::Red);
            };
            if (update) self->m_publish->update(issueId, confirmed, done);
            else self->m_publish->publish(issueId, confirmed, done);
        });
        dialog->open();
    });
}

void IssuesView::publishToJira() {
    const Issue* issue = selected();
    if (!issue || !m_publish) return;
    if (!m_publish->canPublish()) {
        emit toast(tr("Configura la conexión con el gestor y su proyecto en Ajustes"), theme::Amber);
        emit settingsRequested();
        return;
    }
    // Un envío que se cortó pudo crear el issue igualmente: no se crea otro sin haberlo comprobado.
    if (issue->publication.uncertain
        && QMessageBox::question(this, tr("Envío sin confirmar"),
                                 tr("El último envío de %1 se cortó sin respuesta y puede haberse creado en %2. Búscalo allí por la etiqueta %1.\n\n"
                                    "¿Crear otro de todos modos?")
                                     .arg(issue->id, m_publish->destination()))
               != QMessageBox::Yes)
        return;
    openPublishDialog(false);
}

void IssuesView::linkJira() {
    const Issue* issue = selected();
    if (!issue || !m_publish) return;
    if (!m_publish->canPublish()) {
        emit toast(tr("Configura la conexión con el gestor y su proyecto en Ajustes"), theme::Amber);
        emit settingsRequested();
        return;
    }
    bool ok = false;
    const QString key = QInputDialog::getText(this, tr("Vincular issue del gestor"),
                                              tr("Clave del issue que ya existe en %1:").arg(m_publish->destination()), QLineEdit::Normal, {}, &ok);
    if (!ok || key.trimmed().isEmpty()) return;
    const QString issueId = issue->id;
    QPointer<IssuesView> self(this);
    m_publish->link(issueId, key, [self](const IssuePublishService::Result& r) {
        if (!self) return;
        self->loadDetail();
        if (r.ok) emit self->toast(tr("%1 vinculado a este issue").arg(r.key), theme::Green);
        else emit self->toast(tr("No se pudo vincular · %1").arg(r.error), theme::Red);
    });
}

void IssuesView::unlinkJira() {
    const Issue* issue = selected();
    if (!issue || !m_publish || !issue->isPublished()) return;
    if (QMessageBox::question(this, tr("Desvincular"),
                              tr("¿Olvidar la relación con %1? En el gestor no se borra nada.").arg(issue->publication.key)) != QMessageBox::Yes)
        return;
    m_publish->unlink(issue->id);
}

void IssuesView::refreshJiraStatus() {
    const Issue* issue = selected();
    if (!issue || !m_publish || !issue->isPublished() || m_publishing) return;
    m_publishing = true;
    m_refreshJiraButton->setEnabled(false);
    QPointer<IssuesView> self(this);
    m_publish->refreshStatus(issue->id, [self](const IssuePublishService::Result& r) {
        if (!self) return;
        self->m_publishing = false;
        self->loadDetail();
        if (!r.ok) emit self->toast(tr("No se pudo consultar el estado en el gestor · %1").arg(r.error), theme::Red);
    });
}

void IssuesView::refreshCases(const Issue& issue) {
    ui::clearLayout(m_casesList);
    m_casesHeader->setText(tr("CASOS DE PRUEBA · %1").arg(issue.caseIds.size()));
    if (issue.caseIds.isEmpty()) {
        m_casesList->addWidget(ui::label(tr("Sin casos. Crea uno o vincula los que ya validan este requerimiento."), "muted-sm"));
        return;
    }
    for (const auto& caseId : issue.caseIds) {
        const TestCase* c = m_cases.find(caseId);
        QHBoxLayout* h;
        auto* row = listRow(&h);
        h->addWidget(ui::label(caseId, "mono-muted"));
        auto* title = new QLabel(c ? (c->title.isEmpty() ? tr("(sin título)") : c->title) : tr("Ya no existe en los casos del proyecto"));
        title->setWordWrap(true);
        h->addWidget(title, 1);
        if (c) {
            h->addWidget(ui::label(QStringLiteral("%1 · %2").arg(label(c->status), c->lastRun.label()), "muted-sm"));
            auto* open = smallButton(tr("Abrir"), "ghost");
            connect(open, &QPushButton::clicked, this, [this, caseId]() { emit openCaseRequested(caseId); });
            h->addWidget(open);
        }
        auto* unlink = unlinkButton(tr("Desvincular el caso del issue (el caso no se borra)"));
        unlink->setObjectName(QStringLiteral("issueUnlinkCase-%1").arg(caseId));
        connect(unlink, &QPushButton::clicked, this, [this, issueId = issue.id, caseId]() { m_issues.unlinkCase(issueId, caseId); });
        h->addWidget(unlink);
        m_casesList->addWidget(row);
    }
}

void IssuesView::refreshPlans(const Issue& issue) {
    ui::clearLayout(m_plansList);
    m_plansHeader->setText(tr("PLANES · %1").arg(issue.planIds.size()));
    if (issue.planIds.isEmpty()) {
        m_plansList->addWidget(ui::label(tr("Sin planes. Crea uno con los casos del issue o vincula uno existente."), "muted-sm"));
        return;
    }
    for (const auto& planId : issue.planIds) {
        const TestPlan* plan = m_plans.find(planId);
        QHBoxLayout* h;
        auto* row = listRow(&h);
        h->addWidget(ui::label(planId, "mono-muted"));
        auto* name = new QLabel(plan ? (plan->archived ? tr("%1 (archivado)").arg(plan->name) : plan->name) : tr("Ya no existe en los planes del proyecto"));
        name->setWordWrap(true);
        h->addWidget(name, 1);
        if (plan) {
            QString summary = tr("%1 casos").arg(m_plans.orderedCaseIds(planId).size());
            if (const auto cycle = m_plans.latestCycle(planId))
                summary += tr(" · último ciclo: %1 de %2 ejecutados, %3 superados").arg(cycle->executed).arg(cycle->total()).arg(cycle->passed);
            h->addWidget(ui::label(summary, "muted-sm"));
            auto* open = smallButton(tr("Abrir"), "ghost");
            connect(open, &QPushButton::clicked, this, [this, planId]() { emit openPlanRequested(planId); });
            h->addWidget(open);
        }
        auto* unlink = unlinkButton(tr("Desvincular el plan del issue (el plan no se borra)"));
        connect(unlink, &QPushButton::clicked, this, [this, issueId = issue.id, planId]() { m_issues.unlinkPlan(issueId, planId); });
        h->addWidget(unlink);
        m_plansList->addWidget(row);
    }
}

void IssuesView::refreshResults(const Issue& issue) {
    ui::clearLayout(m_resultsList);
    const QList<RunRecord> runs = IssueStore::runsOf(issue, m_history);
    m_resultsHeader->setText(tr("RESULTADOS · %1 EJECUCIONES").arg(runs.size()));
    if (runs.isEmpty()) {
        m_resultsList->addWidget(ui::label(tr("Todavía no se ha ejecutado ningún caso del issue."), "muted-sm"));
        return;
    }
    const qsizetype shown = std::min<qsizetype>(runs.size(), kMaxResults);
    for (qsizetype i = 0; i < shown; ++i) {
        const RunRecord& run = runs[i];
        QHBoxLayout* h;
        auto* row = listRow(&h);
        const QString color = verdictColor(run.verdict);
        h->addWidget(ui::pill(label(run.verdict).toUpper(), theme::tint(color, 46), color));
        h->addWidget(ui::label(run.caseId, "mono-muted"));
        auto* title = new QLabel(run.caseTitle);
        title->setWordWrap(true);
        h->addWidget(title, 1);
        const PlanRun* plan = m_history.findPlan(run.planRunId);
        QString where = plan ? tr("%1 · ciclo del %2").arg(plan->name, plan->startedAt.toString(QStringLiteral("dd/MM/yyyy"))) : tr("Ejecución suelta");
        if (plan && issue.planIds.contains(plan->planId)) where += tr(" · plan del issue");
        h->addWidget(ui::label(where, "muted-sm"));
        h->addWidget(ui::label(when(run.finishedAt), "muted-sm"));
        auto* open = smallButton(tr("Ver"), "ghost", tr("Abrir la ejecución en el historial"));
        connect(open, &QPushButton::clicked, this, [this, runId = run.id]() { emit openRunRequested(runId); });
        h->addWidget(open);
        m_resultsList->addWidget(row);
    }
    if (runs.size() > shown) m_resultsList->addWidget(ui::label(tr("y %1 más en el historial").arg(runs.size() - shown), "muted-sm"));
}

// ---- Acciones ------------------------------------------------------------------------------------

void IssuesView::editSelected(const std::function<void(Issue&)>& mutate) {
    const QString id = m_issues.selectedId();
    if (id.isEmpty()) return;
    m_selfEdit = true;
    m_issues.updateIssue(id, mutate);
    m_selfEdit = false;
}

void IssuesView::commitNotes() {
    if (m_notesIssueId.isEmpty()) return;
    m_notesTimer->stop();
    const QString id = std::exchange(m_notesIssueId, QString());
    const Issue* issue = m_issues.find(id);
    const QString text = m_notes->toPlainText();
    if (!issue || issue->notes == text) return;
    m_selfEdit = true;
    m_issues.updateIssue(id, [&text](Issue& i) { i.notes = text; });
    m_selfEdit = false;
}

void IssuesView::consultRequirements() {
    if (!m_requirements || m_consulting) return;
    const QString system = linkedSystem();
    if (system.isEmpty()) {
        emit toast(tr("Vincula un sistema de GESREQ a este proyecto en Ajustes → Configuración del proyecto"), theme::Amber);
        emit settingsRequested();
        return;
    }
    m_consulting = true;
    m_consult->setEnabled(false);
    m_consult->setText(tr("Consultando…"));
    QPointer<IssuesView> self(this);
    m_requirements->fetchInbox([self, system](const RequirementInboxResult& r) {
        if (!self) return;
        self->m_consulting = false;
        self->m_consult->setEnabled(true);
        self->m_consult->setText(tr("Consultar GESREQ"));
        if (!r.ok) {
            emit self->toast(tr("No se pudo consultar GESREQ · %1").arg(r.error), theme::Red);
            return;
        }
        const QString connection = self->m_requirements->connection();
        const QDateTime fetchedAt = r.fetchedAt.isValid() ? r.fetchedAt : QDateTime::currentDateTime();
        QList<ExternalRequirement> mine, others;
        for (const auto& requirement : r.requirements) {
            if (requirement.systemCode.compare(system, Qt::CaseInsensitive) == 0) mine << requirement;
            else others << requirement;
        }
        // La bandeja se lee entera: lo importado que ya no está en ella se marca, lo elija o no el usuario.
        const int missing = self->m_issues.markInboxRead(r.requirements, connection, fetchedAt);
        const QList<IssueStore::ImportCandidate> candidates = self->m_issues.previewImport(mine, connection);
        if (candidates.isEmpty() && others.isEmpty()) {
            emit self->toast(tr("Tu bandeja de control de calidad no tiene requerimientos"), theme::Amber);
            return;
        }
        // Los de otros sistemas no se importan aquí, pero desde ellos se pueden iniciar las pruebas en el
        // proyecto que los trabaja: consultar la bandeja nunca cambia de proyecto por su cuenta.
        auto* dialog = new RequirementImportDialog(system, candidates, others, missing,
                                                   [self](const QString& code) { return self ? self->projectNameForSystem(code) : QString(); },
                                                   self.data());
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        connect(dialog, &RequirementImportDialog::startTestingRequested, self.data(), [self, connection, fetchedAt](const ExternalRequirement& requirement) {
            if (self) self->startTesting(requirement, connection, fetchedAt);
        });
        connect(dialog, &RequirementImportDialog::importRequested, self.data(), [self, connection, fetchedAt](const QList<ExternalRequirement>& selected) {
            if (!self) return;
            const IssueStore::ImportResult result = self->m_issues.importRequirements(selected, connection, fetchedAt);
            emit self->toast(tr("%1 issues creados · %2 actualizados").arg(result.created.size()).arg(result.updated.size()), theme::Green);
        });
        dialog->open();
    });
}

QString IssuesView::projectNameForSystem(const QString& systemCode) const {
    if (!m_projects) return {};
    const Project* project = m_projects->find(m_projects->projectForRequirementSystem(systemCode));
    return project ? project->name : QString();
}

void IssuesView::startTesting(const ExternalRequirement& requirement, const QString& connection, const QDateTime& fetchedAt) {
    const QString target = m_projects ? m_projects->projectForRequirementSystem(requirement.systemCode) : QString();
    if (target.isEmpty()) {
        askForProject(requirement, connection, fetchedAt);
        return;
    }
    // Ya se está en el proyecto del requerimiento: se abre su issue sin cambiar de proyecto.
    if (target == m_projectId) {
        openRequirement(requirement, connection, fetchedAt);
        return;
    }
    emit startTestingRequested(target, requirement, connection, fetchedAt);
}

void IssuesView::askForProject(const ExternalRequirement& requirement, const QString& connection, const QDateTime& fetchedAt) {
    if (!m_projects) {
        emit toast(tr("Ningún proyecto trabaja los requerimientos de %1: vincúlale ese sistema a uno en Ajustes → Configuración del proyecto")
                       .arg(requirement.systemCode), theme::Amber);
        emit settingsRequested();
        return;
    }
    // Se elige o se crea el proyecto y, ya vinculado el sistema, se empieza como en cualquier otro caso.
    auto* dialog = new ProjectSetupDialog(*m_projects, requirement.systemCode, true, m_bugs, m_requirements, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::accepted, this, [this, dialog, requirement, connection, fetchedAt]() {
        const QString id = dialog->projectId();
        if (id.isEmpty()) return;
        emit projectJiraKeyRequested(id, dialog->jiraProject());
        if (id == m_projectId) openRequirement(requirement, connection, fetchedAt);
        else emit startTestingRequested(id, requirement, connection, fetchedAt);
    });
    dialog->open();
}

void IssuesView::openRequirement(const ExternalRequirement& requirement, const QString& connection, const QDateTime& fetchedAt) {
    const QString id = m_issues.openForRequirement(requirement, connection, fetchedAt);
    if (id.isEmpty()) return;
    emit toast(tr("%1 · pruebas del requerimiento %2").arg(id, requirement.id), theme::Green);
}

void IssuesView::createCase() {
    const Issue* issue = selected();
    if (!issue) return;
    const QString issueId = issue->id;
    const QString title = issue->title;
    const QString caseId = m_cases.createCase();
    m_cases.updateCase(caseId, [&title](TestCase& c) { c.title = title; });
    m_issues.linkCase(issueId, caseId);
    emit toast(tr("%1 creado y vinculado a %2").arg(caseId, issueId), theme::Green);
    emit openCaseRequested(caseId);
}

void IssuesView::pickCase() {
    const Issue* issue = selected();
    if (!issue) return;
    const QString issueId = issue->id;
    QList<Choice> choices;
    for (const auto& c : m_cases.cases())
        if (!issue->caseIds.contains(c.id)) choices << Choice{c.id, c.title, c.suite};
    auto* dialog = new ChoiceDialog(tr("Vincular caso a %1").arg(issueId), QString(),
                                    [choices](const ChoiceDialog::Loaded& done) { done(choices, {}); }, QString(), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &ChoiceDialog::chosen, this, [this, issueId](const QString& caseId) { m_issues.linkCase(issueId, caseId); });
    dialog->open();
}

void IssuesView::createPlan() {
    const Issue* issue = selected();
    if (!issue) return;
    const QString issueId = issue->id;
    const QStringList caseIds = issue->caseIds;
    const QString planId = m_plans.createPlan(QStringLiteral("%1 · %2").arg(issueId, issue->title));
    // El plan recién creado queda como activo: los casos se añaden a él, sin los que ya no existen.
    int added = 0;
    for (const auto& caseId : caseIds) {
        if (!m_cases.find(caseId)) continue;
        m_plans.toggle(caseId);
        ++added;
    }
    m_issues.linkPlan(issueId, planId);
    emit toast(tr("%1 creado con %2 casos de %3").arg(planId).arg(added).arg(issueId), theme::Green);
    emit openPlanRequested(planId);
}

void IssuesView::pickPlan() {
    const Issue* issue = selected();
    if (!issue) return;
    const QString issueId = issue->id;
    QList<Choice> choices;
    for (const auto& p : m_plans.plans())
        if (!p.archived && !issue->planIds.contains(p.id)) choices << Choice{p.id, p.name, tr("%1 casos").arg(m_plans.orderedCaseIds(p.id).size())};
    auto* dialog = new ChoiceDialog(tr("Vincular plan a %1").arg(issueId), QString(),
                                    [choices](const ChoiceDialog::Loaded& done) { done(choices, {}); }, QString(), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &ChoiceDialog::chosen, this, [this, issueId](const QString& planId) { m_issues.linkPlan(issueId, planId); });
    dialog->open();
}

void IssuesView::loadRequirementDetail() {
    const Issue* issue = selected();
    if (!issue || !issue->isImported() || !m_requirements || m_readingRequirement) return;
    const QString issueId = issue->id;
    m_readingRequirement = true;
    m_loadDetail->setEnabled(false);
    m_loadDetail->setText(tr("Leyendo…"));
    QPointer<IssuesView> self(this);
    m_requirements->fetchDetail(issue->requirement.data.id, [self, issueId](const RequirementDetailResult& r) {
        if (!self) return;
        self->m_readingRequirement = false;
        self->m_loadDetail->setEnabled(true);
        if (!r.ok) {
            emit self->toast(tr("No se pudo leer la ficha de GESREQ · %1").arg(r.error), theme::Red);
            self->loadDetail();
            return;
        }
        self->m_issues.setRequirementDetail(issueId, r.detail, r.fetchedAt.isValid() ? r.fetchedAt : QDateTime::currentDateTime());
        emit self->toast(tr("Ficha del requerimiento %1 actualizada").arg(r.detail.id), theme::Green);
    });
}

void IssuesView::removeSelected() {
    const Issue* issue = selected();
    if (!issue) return;
    const QString id = issue->id;
    if (QMessageBox::question(this, tr("Eliminar issue"), tr("¿Eliminar %1? Sus casos, planes y resultados no se borran.").arg(id)) != QMessageBox::Yes) return;
    m_issues.removeIssue(id);
}

} // namespace qaflow
