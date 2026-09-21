#include "IssuesView.h"

#include "application/AppContext.h"
#include "core/Text.h"
#include "core/models/BugReport.h"
#include "presentation/theme/Theme.h"
#include "presentation/views/JiraPublishDialog.h"
#include "presentation/views/ProjectSetupDialog.h"
#include "presentation/views/QualityRecordDialog.h"
#include "presentation/views/RevisionPublishDialog.h"
#include "presentation/views/RequirementImportDialog.h"
#include "presentation/widgets/ChoiceDialog.h"
#include "presentation/widgets/Ui.h"

#include <QComboBox>
#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <QSet>

#include <utility>

namespace qaflow {

namespace {
constexpr int kMaxResults = 20;
/// Tipo MIME con el que viaja el id del issue al arrastrar su tarjeta.
constexpr char kIssueMime[] = "application/x-qaflow-issue";
/// Largo máximo del título del issue dentro del nombre del plan que se crea para él: el nombre se lee
/// en listas y selectores, así que lo que no cabe se acorta con «…», igual que el título del gestor.
constexpr int kMaxPlanTitle = 60;

/// Nombre del plan con el que se prueba un issue: su número y su título, acortado si es largo.
QString planNameFor(const Issue& issue) {
    return QStringLiteral("%1 · %2").arg(issue.id, elideTitle(issue.title, kMaxPlanTitle));
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

/// El mismo chip, pero pinchable: sin borde y sin la flecha que Qt le pone a un botón con menú (el
/// «▾» va en el texto, que así se acompasa con el resto del chip).
QString chipButtonStyle(const QString& color) {
    return QStringLiteral("QPushButton{%1border:none;}QPushButton::menu-indicator{width:0;height:0;}").arg(chipStyle(color));
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

/// Nombre corto del destino para el chip del historial: cabe al lado de los demás.
QString destinationTag(RevisionPublishService::Destination destination) {
    switch (destination) {
        case RevisionPublishService::Destination::Zephyr: return QStringLiteral("ZEPHYR");
        case RevisionPublishService::Destination::Tracker: return QStringLiteral("GESTOR");
        case RevisionPublishService::Destination::Requirement: return QStringLiteral("GESREQ");
        case RevisionPublishService::Destination::Close: return QStringLiteral("CERRADO");
    }
    return {};
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
      m_requirements(ctx.requirements), m_publish(ctx.issuePublish), m_revisionPublish(ctx.revisionPublish),
      m_bugs(ctx.bugs), m_bugLedger(ctx.bugLedger), m_records(ctx.records),
      m_projects(ctx.projects),
      m_projectId(ctx.projectId) {
    auto* root = ui::vbox(this, 0, 0);
    m_pages = new QStackedWidget;
    root->addWidget(m_pages);

    auto* board = new QWidget;
    auto* bv = ui::vbox(board, 0, 0);
    buildBoard(bv);
    m_pages->addWidget(board);

    auto* detail = new QWidget;
    auto* dv = ui::vbox(detail, 0, 0);
    buildDetail(dv);
    m_pages->addWidget(detail);

    connect(&m_issues, &IssueStore::issuesChanged, this, [this]() { refreshList(); loadDetail(); });
    connect(&m_issues, &IssueStore::selectionChanged, this, [this]() {
        if (!selected()) showDetail(false);
        refreshList();
        loadDetail();
    });
    // Casos, planes y ejecuciones cambian a menudo mientras se trabaja en otras pantallas: sólo se
    // redibuja si la pantalla se ve, y al volver a ella se refresca entera.
    auto refreshIfVisible = [this]() {
        if (!isVisible()) return;
        refreshList();
        loadDetail();
    };
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

IssuesView::~IssuesView() = default;

void IssuesView::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    refreshList();
    loadDetail();
}

void IssuesView::hideEvent(QHideEvent* e) { QWidget::hideEvent(e); }

bool IssuesView::eventFilter(QObject* watched, QEvent* event) {
    if (const QString id = watched->property("issueId").toString(); !id.isEmpty()) {
        auto* card = qobject_cast<QPushButton*>(watched);
        switch (event->type()) {
            case QEvent::MouseButtonDblClick:   // doble clic: se abre el issue entero
                m_issues.select(id);
                showDetail(true);
                return true;
            case QEvent::MouseButtonPress:
                if (static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton)
                    m_dragStart = static_cast<QMouseEvent*>(event)->position().toPoint();
                break;
            case QEvent::MouseMove: {
                const auto* e = static_cast<QMouseEvent*>(event);
                if ((e->buttons() & Qt::LeftButton) && card &&
                    (e->position().toPoint() - m_dragStart).manhattanLength() >= QApplication::startDragDistance()) {
                    startCardDrag(card);
                    return true;
                }
                break;
            }
            case QEvent::Enter:
            case QEvent::Leave: {
                // Con el ratón encima, la tarjeta enseña sus acciones; la elegida las enseña siempre.
                const bool show = event->type() == QEvent::Enter || id == m_issues.selectedId();
                if (auto* idle = watched->findChild<QWidget*>(QStringLiteral("cardIdle"))) idle->setVisible(!show);
                if (auto* actions = watched->findChild<QWidget*>(QStringLiteral("cardActions"))) actions->setVisible(show);
                break;
            }
            default: break;
        }
        return QWidget::eventFilter(watched, event);
    }
    if (const QVariant column = watched->property("boardColumn"); column.isValid()) {
        const int i = column.toInt();
        switch (event->type()) {
            case QEvent::DragEnter:
            case QEvent::DragMove: {
                auto* e = static_cast<QDropEvent*>(event);
                const QString id = e->mimeData() ? QString::fromUtf8(e->mimeData()->data(kIssueMime)) : QString();
                const Issue* issue = m_issues.find(id);
                const bool accepts = issue && static_cast<Column>(i) != Column::Broken &&
                                     static_cast<Column>(i) != columnOf(*issue, snapshotOf(*issue));
                if (accepts) e->acceptProposedAction();
                else e->ignore();
                styleWell(i, accepts);
                return true;
            }
            case QEvent::DragLeave:
                styleWell(i, false);
                return true;
            case QEvent::Drop: {
                auto* e = static_cast<QDropEvent*>(event);
                styleWell(i, false);
                const QString id = e->mimeData() ? QString::fromUtf8(e->mimeData()->data(kIssueMime)) : QString();
                if (id.isEmpty() || static_cast<Column>(i) == Column::Broken) { e->ignore(); return true; }
                e->acceptProposedAction();
                // Después de la entrega: cambiar el estado rehace el tablero, y con él la columna y la tarjeta.
                QTimer::singleShot(0, this, [this, id, i]() { moveToColumn(id, static_cast<Column>(i)); });
                return true;
            }
            default: break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

const Issue* IssuesView::selected() const { return m_issues.find(m_issues.selectedId()); }

QString IssuesView::linkedSystem() const {
    const Project* project = m_projects ? m_projects->find(m_projectId) : nullptr;
    return project ? project->requirementSystem : QString();
}

void IssuesView::focusSearch() {
    showDetail(false);
    m_search->setFocus();
    m_search->selectAll();
}

void IssuesView::showDetail(bool on) {
    m_pages->setCurrentIndex(on && selected() ? 1 : 0);
}

IssuesView::RevisionSnapshot IssuesView::snapshotOf(const Issue& issue) const {
    RevisionSnapshot s;
    if (m_records) s.progress = m_records->progressFor(issue.id);
    const IssueRevision* open = issue.currentRevision();
    const IssueRevision* last = issue.revisions.isEmpty() ? nullptr : &issue.revisions.last();
    s.open = open != nullptr;
    s.closed = !open && last != nullptr;
    s.number = last ? last->number : 1;
    s.hasPlan = !issue.planIds.isEmpty() && !IssueStore::caseIdsOf(issue, m_plans).isEmpty();
    s.executed = s.progress.executed > 0;
    const QList<IssueLink> bugs = bugsOf(issue);
    s.openBugs = int(std::count_if(bugs.cbegin(), bugs.cend(), [](const IssueLink& b) { return !b.resolved; }));
    s.hasRecord = last && last->hasDocument();
    s.published = last && (!last->jira.isEmpty() || !last->gesreq.isEmpty());
    s.continuable = continuableCycle(issue, s.number);
    const bool done[] = {s.hasPlan, s.executed, s.openBugs == 0, s.hasRecord, s.closed, s.published};
    s.nextStep = 7;
    for (int i = 0; i < 6; ++i)
        if (!done[i]) { s.nextStep = i + 1; break; }
    return s;
}

IssuesView::Column IssuesView::columnOf(const Issue& issue, const RevisionSnapshot& s) const {
    switch (issue.state) {
        case IssueState::Done: return Column::Done;
        case IssueState::Pending: return Column::Pending;
        case IssueState::Preparing:
        case IssueState::Testing: break;
    }
    // Lo último que se sabe de cada caso de la ronda: con alguno fallido o bloqueado, el issue espera a
    // que se repita. Sin el servicio del acta (contexto mínimo), lo dice el ciclo que se puede continuar.
    const bool broken = m_records ? s.progress.failed + s.progress.blocked > 0 : !s.continuable.isEmpty();
    if (broken) return Column::Broken;
    return issue.state == IssueState::Preparing ? Column::Preparing : Column::Testing;
}

// ---- Tablero -------------------------------------------------------------------------------------

namespace {
struct ColumnInfo {
    QString name;
    QString hint;
    QString color;
};

ColumnInfo columnInfo(IssuesView::Column c) {
    switch (c) {
        case IssuesView::Column::Pending: return {IssuesView::tr("PENDIENTE"), IssuesView::tr("Sin plan todavía"), theme::Muted};
        case IssuesView::Column::Preparing: return {IssuesView::tr("EN PREPARACIÓN"), IssuesView::tr("Plan en composición"), theme::Violet};
        case IssuesView::Column::Testing: return {IssuesView::tr("EN PRUEBAS"), IssuesView::tr("Ejecutándose, sin fallos"), theme::Blue};
        case IssuesView::Column::Broken:
            return {IssuesView::tr("FALLIDO / BLOQUEADO"), IssuesView::tr("Quedaron casos rotos: continuar lo fallado"), theme::Red};
        case IssuesView::Column::Done: return {IssuesView::tr("FINALIZADO"), IssuesView::tr("Revisión cerrada"), theme::Green};
    }
    return {};
}

/// Barra fina con lo que salió de los casos de la ronda: superados, fallidos, bloqueados y el resto.
QWidget* resultBar(const IssueProgress& p) {
    auto* bar = new QWidget;
    bar->setFixedHeight(4);
    auto* h = ui::hbox(bar, 0, 2);
    auto segment = [h](int stretch, const QString& color) {
        if (stretch <= 0) return;
        auto* f = new QFrame;
        f->setStyleSheet(QStringLiteral("background:%1;border-radius:2px;").arg(color));
        h->addWidget(f, stretch);
    };
    segment(p.passed, theme::Green);
    segment(p.failed, theme::Red);
    segment(p.blocked, theme::Amber);
    segment(std::max(p.cases - p.passed - p.failed - p.blocked, p.cases > 0 ? 0 : 1), theme::Border);
    return bar;
}
} // namespace

void IssuesView::buildBoard(QVBoxLayout* root) {
    auto* head = new QWidget;
    auto* hh = ui::hbox(head, 0, 10);
    hh->setContentsMargins(22, 16, 22, 14);
    hh->addWidget(ui::label(tr("Issues"), "h1-sm"));
    m_listCount = ui::label(QString(), "muted-sm");
    hh->addWidget(m_listCount);
    hh->addStretch(1);

    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("issueSearch"));
    m_search->setPlaceholderText(tr("Buscar por título, número de GREQ, sistema, solicitante…"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumWidth(240);
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString& t) { m_filter.text = t; refreshList(); });
    hh->addWidget(m_search);

    // Cada opción lleva el valor del enum como dato: el texto se traduce, el filtro no. El estado no se
    // filtra: es la columna.
    m_priorityFilter = filterBox(tr("Prioridad"), {{label(Priority::Alta), static_cast<int>(Priority::Alta)},
                                                   {label(Priority::Media), static_cast<int>(Priority::Media)},
                                                   {label(Priority::Baja), static_cast<int>(Priority::Baja)}});
    m_jiraFilter = filterBox(tr("Jira"), {{tr("Publicados"), 1}, {tr("Sin publicar"), 0}});
    m_jiraFilter->setObjectName(QStringLiteral("issueJiraFilter"));
    connect(m_priorityFilter, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_filter.priority = i <= 0 ? std::nullopt : std::optional<Priority>(static_cast<Priority>(m_priorityFilter->currentData().toInt()));
        refreshList();
    });
    connect(m_jiraFilter, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_filter.published = i <= 0 ? std::nullopt : std::optional<bool>(m_jiraFilter->currentData().toInt() == 1);
        refreshList();
    });
    hh->addWidget(m_priorityFilter);
    hh->addWidget(m_jiraFilter);

    m_consult = smallButton(tr("Consultar GESREQ"), "outline");
    m_consult->setObjectName(QStringLiteral("issuesConsult"));
    m_consult->setVisible(m_requirements != nullptr);
    connect(m_consult, &QPushButton::clicked, this, &IssuesView::consultRequirements);
    hh->addWidget(m_consult);
    auto* create = smallButton(tr("+ Nuevo"), "primary", tr("Issue creado a mano, sin requerimiento de GESREQ"));
    create->setObjectName(QStringLiteral("issuesNew"));
    connect(create, &QPushButton::clicked, this, [this]() {
        m_issues.createIssue(tr("Nuevo issue"));
        showDetail(true);
        m_title->setFocus();
        m_title->selectAll();
    });
    hh->addWidget(create);
    root->addWidget(head);
    auto* divider = new QFrame;
    divider->setFixedHeight(1);
    divider->setStyleSheet(QStringLiteral("background:%1;").arg(theme::Border));
    root->addWidget(divider);

    auto* body = new QWidget;
    auto* bh = ui::hbox(body, 0, 0);
    root->addWidget(body, 1);

    auto* boardArea = new QWidget;
    auto* av = ui::vbox(boardArea, 0, 8);
    av->setContentsMargins(12, 14, 12, 14);
    m_boardEmpty = ui::label(QString(), "muted");
    m_boardEmpty->setObjectName(QStringLiteral("issueBoardEmpty"));
    m_boardEmpty->setWordWrap(true);
    av->addWidget(m_boardEmpty);
    auto* columns = new QWidget;
    auto* ch = ui::hbox(columns, 0, 8);
    for (int i = 0; i < kColumns; ++i) {
        const ColumnInfo info = columnInfo(static_cast<Column>(i));
        auto* column = new QWidget;
        column->setMinimumWidth(170);
        auto* cv = ui::vbox(column, 0, 6);
        auto* top = new QWidget;
        auto* th = ui::hbox(top, 0, 8);
        th->setContentsMargins(4, 0, 4, 0);
        th->addWidget(ui::dot(info.color, 8));
        auto* name = ui::label(info.name, "eyebrow");
        name->setStyleSheet(QStringLiteral("color:%1;letter-spacing:0.3px;").arg(theme::TextSoft));
        th->addWidget(name);
        m_columnCounts[i] = ui::label(QString(), "muted-sm");
        th->addWidget(m_columnCounts[i], 1);
        cv->addWidget(top);
        // Dos líneas para todas: así las columnas empiezan a la misma altura aunque una explique más.
        auto* hint = ui::label(info.hint, "muted-sm");
        hint->setWordWrap(true);
        hint->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        hint->setContentsMargins(4, 0, 4, 0);
        hint->setFixedHeight(2 * hint->fontMetrics().lineSpacing() + 2);
        cv->addWidget(hint);

        auto* well = new QFrame;
        well->setObjectName(QStringLiteral("issueBoardColumn-%1").arg(i));
        // Las tarjetas se sueltan aquí para cambiar de estado (menos en la de fallidos, que es calculada).
        well->setProperty("boardColumn", i);
        well->setAcceptDrops(true);
        well->installEventFilter(this);
        m_wells[i] = well;
        styleWell(i, false);
        auto* wv = ui::vbox(well, 0, 0);
        QWidget* content;
        auto* sa = ui::scrollArea(&content, &m_columns[i]);
        sa->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"));
        content->setStyleSheet(QStringLiteral("background:transparent;"));
        m_columns[i]->setContentsMargins(6, 6, 6, 6);
        m_columns[i]->setSpacing(6);
        wv->addWidget(sa);
        cv->addWidget(well, 1);
        ch->addWidget(column, 1);
    }
    av->addWidget(columns, 1);
    bh->addWidget(boardArea, 1);
    buildDrawer(bh);
}

void IssuesView::buildDrawer(QHBoxLayout* root) {
    auto* pane = ui::card("list-pane");
    pane->setObjectName(QStringLiteral("issueDrawer"));
    pane->setFixedWidth(320);
    pane->setStyleSheet(QStringLiteral("QFrame#issueDrawer{border-right:none;border-left:1px solid %1;}").arg(theme::Border));
    m_drawer = pane;
    auto* v = ui::vbox(pane, 0, 0);
    QWidget* content;
    auto* sa = ui::scrollArea(&content, &m_drawerLayout);
    m_drawerLayout->setContentsMargins(20, 18, 20, 18);
    m_drawerLayout->setSpacing(14);
    v->addWidget(sa);
    root->addWidget(pane);
}

QWidget* IssuesView::boardCard(const Issue& issue, const RevisionSnapshot& s, Column column) {
    auto* card = ui::button(QString(), "board-card");
    card->setObjectName(QStringLiteral("issueRow-%1").arg(issue.id));
    card->setProperty("issueId", issue.id);
    card->installEventFilter(this);
    card->setToolTip(tr("Doble clic para abrir el issue · arrástralo a otra columna para cambiar su estado · clic derecho para más"));
    // La tarjeta toma el ancho de su columna: su contenido se ajusta a él, no al revés.
    card->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    ui::setFlag(card, "active", issue.id == m_issues.selectedId());
    auto* v = ui::vbox(card, 0, 7);
    v->setContentsMargins(11, 10, 11, 10);

    auto* top = new QWidget;
    auto* th = ui::hbox(top, 0, 6);
    th->addWidget(ui::label(issue.isImported() ? issue.requirement.data.id : issue.id, "mono-muted"));
    if (issue.isImported() && !issue.requirement.data.systemCode.isEmpty())
        th->addWidget(ui::pill(issue.requirement.data.systemCode, theme::tint(theme::Muted, 26), theme::Muted));
    th->addStretch(1);
    if (!issue.revisions.isEmpty()) {
        auto* rev = ui::label(tr("REV %1").arg(s.number), "muted-sm");
        rev->setStyleSheet(QStringLiteral("color:%1;font-weight:700;font-size:10.5px;").arg(theme::Cyan));
        th->addWidget(rev);
    }
    v->addWidget(top);

    auto* title = new QLabel(issue.title);
    title->setWordWrap(true);
    title->setStyleSheet(QStringLiteral("font-size:13px;font-weight:600;color:%1;").arg(theme::Text));
    v->addWidget(title);

    if (s.progress.cases > 0) v->addWidget(resultBar(s.progress));

    // Lo que avisa: casos rotos, cambios en GESREQ o que el requerimiento salió de la bandeja.
    QList<QLabel*> flags;
    if (s.progress.failed > 0)
        flags << ui::pill(tr("%n FALLIDO(S)", nullptr, s.progress.failed), theme::tint(theme::Red, 46), theme::RedSoft);
    if (s.progress.blocked > 0)
        flags << ui::pill(tr("%n BLOQUEADO(S)", nullptr, s.progress.blocked), theme::tint(theme::Amber, 46), theme::AmberSoft);
    if (!issue.requirement.changes.isEmpty()) flags << ui::pill(tr("CAMBIOS"), theme::tint(theme::Amber, 46), theme::AmberSoft);
    if (issue.requirement.missing) flags << ui::pill(tr("FUERA DE LA BANDEJA"), theme::tint(theme::Muted, 38), theme::Muted);
    if (!flags.isEmpty()) {
        auto* row = new QWidget;
        auto* fh = ui::hbox(row, 0, 4);
        for (auto* f : flags) fh->addWidget(f);
        fh->addStretch(1);
        v->addWidget(row);
    }

    // La última fila dice qué le toca y su clave en el gestor; en la tarjeta elegida o con el ratón
    // encima, lo cambia por el botón que lo hace y el «⋯» de su menú. Las dos ocupan lo mismo, así que
    // la tarjeta no salta al pasar por encima.
    const NextAction next = nextActionOf(issue, s, column);
    const bool active = issue.id == m_issues.selectedId();
    auto* bottom = new QWidget;
    bottom->setFixedHeight(26);
    auto* bs = ui::hbox(bottom, 0, 0);
    auto* idle = new QWidget;
    idle->setObjectName(QStringLiteral("cardIdle"));
    auto* ih = ui::hbox(idle, 0, 6);
    auto* nextLabel = ui::label(next.hint, "muted-sm");
    nextLabel->setObjectName(QStringLiteral("issueCardNext-%1").arg(issue.id));
    ih->addWidget(nextLabel, 1);
    if (issue.isPublished()) ih->addWidget(ui::label(issue.publication.key, "mono-muted"));
    idle->setVisible(!active);
    bs->addWidget(idle, 1);
    v->addWidget(bottom);

    auto* actions = new QWidget;
    actions->setObjectName(QStringLiteral("cardActions"));
    auto* ah = ui::hbox(actions, 0, 6);
    if (next.run) {
        auto* go = smallButton(next.shortButton, "primary", next.title);
        go->setObjectName(QStringLiteral("issueCardAction-%1").arg(issue.id));
        // Estilo propio y completo: dentro de otro botón (la tarjeta) el del rol no llega a pintar el fondo.
        go->setStyleSheet(QStringLiteral("QPushButton{background:%1;color:%2;border:none;border-radius:7px;padding:3px 9px;"
                                         "font-size:11.5px;font-weight:700;}QPushButton:hover{background:%3;}")
                              .arg(theme::Blue, theme::OnAccent, theme::TextSoft));
        connect(go, &QPushButton::clicked, this, [go, run = next.run]() { run(go); });
        ah->addWidget(go);
    } else {
        ah->addWidget(ui::label(next.hint, "muted-sm"));
    }
    ah->addStretch(1);
    auto* more = smallButton(QStringLiteral("⋯"), "ghost", tr("Más acciones"));
    more->setObjectName(QStringLiteral("issueCardMore-%1").arg(issue.id));
    more->setFixedWidth(30);
    connect(more, &QPushButton::clicked, this, [this, more, id = issue.id]() {
        showCardMenu(id, more->mapToGlobal(QPoint(0, more->height())));
    });
    ah->addWidget(more);
    actions->setVisible(active);
    bs->addWidget(actions, 1);

    // El resto no atiende al ratón: el clic, el doble clic y el arrastre son de la tarjeta. Los botones sí,
    // y por eso ni ellos ni la fila que los contiene se tocan: un widget transparente al ratón lo es con
    // todos sus hijos. Esa fila deja pasar a la tarjeta los clics que no caen en un botón.
    for (auto* child : card->findChildren<QWidget*>())
        if (child != bottom && child != actions && !actions->isAncestorOf(child)) child->setAttribute(Qt::WA_TransparentForMouseEvents);

    card->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(card, &QWidget::customContextMenuRequested, this, [this, card, id = issue.id](const QPoint& pos) {
        showCardMenu(id, card->mapToGlobal(pos));
    });
    connect(card, &QPushButton::clicked, this, [this, id = issue.id]() { m_issues.select(id); });
    return card;
}

IssuesView::NextAction IssuesView::nextActionOf(const Issue& issue, const RevisionSnapshot& s, Column column) {
    NextAction a;
    const QString issueId = issue.id;
    if (column == Column::Broken && !s.continuable.isEmpty()) {
        const int broken = int(m_history.report(s.continuable).brokenCaseIds().size());
        a.title = tr("Continuar lo fallado");
        a.why = tr("%1 dejó %2 fallido(s) y %3 bloqueado(s): se repiten desde el paso que se rompió, en la misma revisión.")
                    .arg(s.continuable).arg(s.progress.failed).arg(s.progress.blocked);
        a.hint = tr("Continuar lo fallado");
        a.button = tr("▶ Continuar lo fallado (%1)").arg(broken);
        a.shortButton = tr("▶ Continuar (%1)").arg(broken);
        a.run = [this, cycle = s.continuable](QWidget*) { emit continueCycleRequested(cycle); };
    } else switch (s.nextStep) {
        case 1:
            a.title = tr("Preparar el plan de pruebas");
            a.why = issue.planIds.isEmpty() ? tr("El requerimiento todavía no tiene plan") : tr("El plan todavía no tiene casos: ábrelo y añádeselos.");
            a.hint = issue.planIds.isEmpty() ? tr("Crear el plan") : tr("Añadir casos al plan");
            a.button = a.shortButton = issue.planIds.isEmpty() ? tr("Crear plan") : tr("Abrir plan");
            if (issue.planIds.isEmpty()) a.run = [this](QWidget*) { createPlan(); };
            else a.run = [this, planId = issue.planIds.first()](QWidget*) { emit openPlanRequested(planId); };
            break;
        case 2:
            a.title = tr("Ejecutar el plan");
            a.why = tr("%1 de %2 casos ejecutados en esta revisión").arg(s.progress.executed).arg(s.progress.cases);
            a.hint = s.progress.notRun() > 0 ? tr("%n caso(s) sin ejecutar", nullptr, s.progress.notRun()) : tr("Ejecutar el plan");
            a.button = tr("▶ Ejecutar plan…");
            a.shortButton = tr("▶ Ejecutar");
            a.run = [this](QWidget* anchor) { runPlan(anchor); };
            break;
        case 3:
            a.title = tr("Revisar los bugs reportados");
            a.why = tr("%n bug(s) abierto(s): con alguno abierto el requerimiento no queda conforme.", nullptr, s.openBugs);
            a.hint = tr("%n bug(s) abierto(s)", nullptr, s.openBugs);
            a.button = tr("Ver los bugs");
            a.shortButton = tr("Ver bugs");
            a.run = [this](QWidget*) { showDetail(true); };
            break;
        case 4:
            a.title = tr("Generar el acta (R-213)");
            a.why = tr("Con lo del requerimiento, la ejecución elegida y los bugs de la revisión");
            a.hint = tr("Generar el acta");
            a.button = tr("Generar acta…");
            a.shortButton = tr("Generar acta");
            a.run = [this](QWidget*) { generateRecord(); };
            break;
        case 5:
            a.title = tr("Cerrar la revisión");
            a.why = tr("Se propone «%1»").arg(label(s.progress.suggested));
            a.hint = tr("Cerrar la revisión");
            a.button = tr("Cerrar revisión…");
            a.shortButton = tr("Cerrar");
            a.run = [this](QWidget*) { closeRevision(); };
            break;
        case 6:
            a.title = tr("Publicar el resultado");
            a.why = tr("Los ciclos a Zephyr, el resultado y el acta al gestor y el registro en GESREQ");
            a.hint = tr("Publicar el resultado");
            a.button = tr("Publicar…");
            a.shortButton = tr("Publicar");
            if (m_revisionPublish) a.run = [this](QWidget*) { publishRevision(); };
            break;
        default:
            a.title = issue.lastOutcome() == QaOutcome::Observado ? tr("Volver a probar") : tr("Nada pendiente");
            a.why = tr("Revisión %1 cerrada como %2").arg(s.number).arg(label(issue.lastOutcome()));
            a.hint = tr("%1 · publicado").arg(label(issue.lastOutcome()));
            a.button = a.shortButton = tr("Nueva revisión");
            a.run = [this](QWidget*) { openRevision(); };
            break;
    }
    // Lo que lanza la acción trabaja sobre el issue elegido: desde una tarjeta, primero se elige el suyo.
    if (a.run) a.run = [this, issueId, run = a.run](QWidget* anchor) {
        m_issues.select(issueId);
        run(anchor);
    };
    return a;
}

void IssuesView::showCardMenu(const QString& issueId, const QPoint& globalPos) {
    m_issues.select(issueId);
    const Issue* found = m_issues.find(issueId);
    if (!found) return;
    const Issue issue = *found;
    const RevisionSnapshot s = snapshotOf(issue);
    const Column column = columnOf(issue, s);
    const NextAction next = nextActionOf(issue, s, column);

    auto* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("issueCardMenu"));
    menu->setAttribute(Qt::WA_DeleteOnClose);
    auto add = [menu](const QString& text, const char* name, const std::function<void()>& f, bool enabled = true) {
        QAction* a = menu->addAction(text, f);
        a->setObjectName(QString::fromLatin1(name));
        a->setEnabled(enabled);
        return a;
    };
    add(tr("Abrir el issue"), "issueMenuOpen", [this]() { showDetail(true); });
    if (next.run) add(tr("Lo siguiente: %1").arg(next.title), "issueMenuNext", [run = next.run]() { run(nullptr); });
    menu->addSeparator();
    add(tr("Ejecutar plan…"), "issueMenuRun", [this]() { runPlan(nullptr); }, !runnablePlans(issue).isEmpty());
    if (!s.continuable.isEmpty())
        add(tr("Continuar lo fallado…"), "issueMenuContinue", [this, cycle = s.continuable]() { emit continueCycleRequested(cycle); });
    if (!issue.planIds.isEmpty())
        add(tr("Ir al plan"), "issueMenuPlan", [this, planId = issue.planIds.first()]() { emit openPlanRequested(planId); });
    else
        add(tr("Crear plan"), "issueMenuPlan", [this]() { createPlan(); });

    // El estado de QA, como en el detalle: a mano manda sobre el avance automático.
    QMenu* states = menu->addMenu(tr("Estado de QA"));
    for (const auto state : {IssueState::Pending, IssueState::Preparing, IssueState::Testing, IssueState::Done}) {
        QAction* a = states->addAction(label(state), this, [this, issueId, state]() {
            m_issues.updateIssue(issueId, [state](Issue& i) { i.state = state; });
        });
        a->setObjectName(QStringLiteral("issueMenuState-%1").arg(static_cast<int>(state)));
        a->setCheckable(true);
        a->setChecked(issue.state == state);
    }
    menu->addSeparator();
    if (issue.isPublished() && !issue.publication.url.isEmpty())
        add(tr("Abrir %1 en el gestor").arg(issue.publication.key), "issueMenuJira",
            [this, url = issue.publication.url]() { emit openUrlRequested(url); });
    if (issue.isImported() && !issue.requirement.data.detailUrl.isEmpty())
        add(tr("Abrir en GESREQ"), "issueMenuGesreq", [this, url = issue.requirement.data.detailUrl]() { emit openUrlRequested(url); });
    menu->addSeparator();
    add(tr("Eliminar…"), "issueMenuRemove", [this]() { removeSelected(); });
    menu->popup(globalPos);
}

void IssuesView::moveToColumn(const QString& issueId, Column column) {
    const Issue* issue = m_issues.find(issueId);
    if (!issue || column == Column::Broken) return;
    IssueState state = IssueState::Pending;
    switch (column) {
        case Column::Pending: state = IssueState::Pending; break;
        case Column::Preparing: state = IssueState::Preparing; break;
        case Column::Testing: case Column::Broken: state = IssueState::Testing; break;
        case Column::Done: state = IssueState::Done; break;
    }
    m_issues.select(issueId);
    if (issue->state != state) m_issues.updateIssue(issueId, [state](Issue& i) { i.state = state; });
    // Con casos rotos en la ronda, el issue se queda en fallidos aunque cambie su estado: se avisa, para
    // que no parezca que el arrastre no hizo nada.
    if (const Issue* now = m_issues.find(issueId); now && columnOf(*now, snapshotOf(*now)) == Column::Broken)
        emit toast(tr("%1 sigue en «Fallido / bloqueado»: su revisión tiene casos rotos. Continúa lo fallado para sacarlo.").arg(issueId),
                   theme::Amber);
    else
        emit toast(tr("%1 · %2").arg(issueId, label(state)), theme::Green);
}

void IssuesView::startCardDrag(QPushButton* card) {
    const QString id = card->property("issueId").toString();
    auto* mime = new QMimeData;
    mime->setData(kIssueMime, id.toUtf8());
    mime->setText(id);
    auto* drag = new QDrag(card);
    drag->setMimeData(mime);
    const QPixmap pixmap = card->grab();
    drag->setPixmap(pixmap);
    drag->setHotSpot(m_dragStart);
    QPointer<QPushButton> guard(card);
    drag->exec(Qt::MoveAction);
    // El botón se queda hundido si el arrastre se tragó el soltar el ratón.
    if (guard) guard->setDown(false);
}

void IssuesView::styleWell(int column, bool hot) {
    QFrame* well = m_wells[column];
    const bool broken = static_cast<Column>(column) == Column::Broken;
    QString bg = broken ? theme::tint(theme::Red, 14) : theme::tint(theme::Border, 70);
    QString border = broken ? theme::tint(theme::Red, 60) : QStringLiteral("transparent");
    if (hot) {
        bg = theme::tint(theme::Blue, 22);
        border = theme::Blue;
    }
    well->setStyleSheet(QStringLiteral("QFrame#%1{background:%2;border:1px solid %3;border-radius:12px;}").arg(well->objectName(), bg, border));
}

void IssuesView::refreshList() {
    const QString system = linkedSystem();
    m_consult->setToolTip(system.isEmpty() ? tr("Vincula antes un sistema de GESREQ a este proyecto en Ajustes")
                                           : tr("Leer la bandeja de control de calidad e importar los requerimientos de %1").arg(system));
    int counts[kColumns] = {};
    for (int i = 0; i < kColumns; ++i) ui::clearLayout(m_columns[i]);
    int shown = 0;
    for (const auto& issue : m_issues.issues()) {
        if (!m_filter.matches(issue)) continue;
        ++shown;
        const RevisionSnapshot s = snapshotOf(issue);
        const Column column = columnOf(issue, s);
        const int i = static_cast<int>(column);
        ++counts[i];
        m_columns[i]->addWidget(boardCard(issue, s, column));
    }
    for (int i = 0; i < kColumns; ++i) {
        m_columnCounts[i]->setText(QString::number(counts[i]));
        m_columns[i]->addStretch(1);
    }
    m_boardEmpty->setText(m_issues.issues().isEmpty() ? tr("Todavía no hay issues. Consulta GESREQ para importar tus requerimientos o crea uno a mano.")
                                                      : tr("Ningún issue coincide con los filtros."));
    m_boardEmpty->setVisible(shown == 0);
    m_listCount->setText(m_filter.isEmpty() ? tr("%1 issues").arg(shown) : tr("%1 de %2 issues").arg(shown).arg(m_issues.issues().size()));
    refreshDrawer();
}

void IssuesView::refreshDrawer() {
    ui::clearLayout(m_drawerLayout);
    const Issue* found = selected();
    m_drawer->setVisible(found != nullptr);
    if (!found) return;
    const Issue issue = *found;
    const RevisionSnapshot s = snapshotOf(issue);
    const Column column = columnOf(issue, s);
    const ColumnInfo info = columnInfo(column);

    auto* top = new QWidget;
    auto* th = ui::hbox(top, 0, 8);
    // Se ajusta al ancho del panel en vez de ensancharlo: al lado va el botón del menú.
    auto* identity = ui::label(issue.isImported() ? QStringLiteral("%1 · GREQ %2 · %3").arg(issue.id, issue.requirement.data.id,
                                                                                          issue.requirement.data.systemCode)
                                                  : issue.id,
                               "eyebrow-mono");
    identity->setWordWrap(true);
    identity->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    th->addWidget(identity, 1);
    // El mismo menú que la tarjeta: todo lo que se puede hacer con el issue, a mano desde el panel.
    auto* more = smallButton(QStringLiteral("⋯"), "ghost", tr("Más acciones"));
    more->setObjectName(QStringLiteral("issueDrawerMore"));
    more->setFixedWidth(34);
    connect(more, &QPushButton::clicked, this, [this, more, id = issue.id]() {
        showCardMenu(id, more->mapToGlobal(QPoint(0, more->height())));
    });
    th->addWidget(more, 0, Qt::AlignTop);
    m_drawerLayout->addWidget(top);

    auto* title = new QLabel(issue.title);
    title->setObjectName(QStringLiteral("issueDrawerTitle"));
    title->setWordWrap(true);
    title->setStyleSheet(QStringLiteral("font-size:18px;font-weight:700;color:%1;").arg(theme::Text));
    m_drawerLayout->addWidget(title);

    auto* chips = new QWidget;
    auto* chh = ui::hbox(chips, 0, 6);
    chh->addWidget(ui::pill(info.name, theme::tint(info.color, 46), info.color));
    if (!issue.revisions.isEmpty()) {
        QString rev = tr("REV %1").arg(s.number);
        QStringList environments;
        for (const auto& cycle : IssueStore::cyclesOfRevision(issue, m_history, s.number))
            if (const QString env = cycle.environment.trimmed(); !env.isEmpty() && !environments.contains(env)) environments << env;
        if (!environments.isEmpty()) rev += QStringLiteral(" · ") + environments.join(QStringLiteral(", ")).toUpper();
        chh->addWidget(ui::pill(rev, theme::tint(theme::Cyan, 38), theme::Cyan));
    }
    if (issue.isPublished()) {
        const auto& p = issue.publication;
        chh->addWidget(ui::pill(p.status.isEmpty() ? p.key : QStringLiteral("%1 · %2").arg(p.key, p.status), theme::tint(theme::Blue, 30), theme::Blue));
    }
    chh->addStretch(1);
    m_drawerLayout->addWidget(chips);

    // Lo siguiente: el paso que toca de la revisión, con su acción a mano.
    auto* next = ui::card("card-flat");
    next->setObjectName(QStringLiteral("issueNext"));
    auto* nv = ui::vbox(next, 0, 8);
    nv->setContentsMargins(14, 12, 14, 14);
    nv->addWidget(ui::label(tr("LO SIGUIENTE"), "eyebrow"));
    const NextAction action = nextActionOf(issue, s, column);
    const QString what = action.title;
    const QString why = action.why;
    auto* whatLabel = ui::label(what);
    whatLabel->setObjectName(QStringLiteral("issueNextTitle"));
    whatLabel->setWordWrap(true);
    whatLabel->setStyleSheet(QStringLiteral("font-size:14.5px;font-weight:600;"));
    nv->addWidget(whatLabel);
    auto* whyLabel = ui::label(why, "muted-sm");
    whyLabel->setWordWrap(true);
    nv->addWidget(whyLabel);
    auto* buttons = new QWidget;
    auto* bh = ui::hbox(buttons, 0, 8);
    if (action.run) {
        auto* go = smallButton(action.button, "primary");
        go->setObjectName(QStringLiteral("issueNextAction"));
        connect(go, &QPushButton::clicked, this, [go, run = action.run]() { run(go); });
        bh->addWidget(go);
    }
    auto* openButton = smallButton(tr("Abrir el issue"), "outline");
    openButton->setObjectName(QStringLiteral("issueOpenDetail"));
    connect(openButton, &QPushButton::clicked, this, [this]() { showDetail(true); });
    bh->addWidget(openButton);
    bh->addStretch(1);
    nv->addWidget(buttons);
    m_drawerLayout->addWidget(next);

    // Los pasos de la revisión, de un vistazo.
    m_drawerLayout->addWidget(ui::label(issue.revisions.isEmpty() ? tr("REVISIÓN") : tr("REVISIÓN %1").arg(s.number), "eyebrow"));
    const QStringList caseIds = IssueStore::caseIdsOf(issue, m_plans);
    const QString names[] = {tr("Preparar el plan"), tr("Ejecutar el plan"), tr("Revisar los bugs"), tr("Generar el acta"),
                             tr("Cerrar la revisión"), tr("Publicar"), tr("Volver a probar")};
    const QString notes[] = {caseIds.isEmpty() ? QString() : tr("%n caso(s)", nullptr, int(caseIds.size())),
                             s.progress.cases > 0 ? QStringLiteral("%1/%2").arg(s.progress.executed).arg(s.progress.cases) : QString(),
                             s.openBugs > 0 ? tr("%n abierto(s)", nullptr, s.openBugs) : QString(),
                             QString(), QString(), QString(), QString()};
    const int steps = s.open ? 6 : 7;
    auto* list = new QWidget;
    auto* lv = ui::vbox(list, 0, 6);
    for (int i = 0; i < steps; ++i) {
        const bool done = i + 1 < s.nextStep && i < 6;
        const bool current = i + 1 == s.nextStep;
        const QString color = done ? theme::Green : (current ? (column == Column::Broken ? theme::Red : theme::Amber) : theme::Muted);
        auto* row = new QWidget;
        auto* rh = ui::hbox(row, 0, 10);
        auto* mark = ui::label(done ? QStringLiteral("✓") : QString::number(i + 1));
        mark->setAlignment(Qt::AlignCenter);
        mark->setFixedSize(20, 20);
        mark->setStyleSheet(QStringLiteral("background:%1;color:%2;border-radius:10px;font-size:10.5px;font-weight:700;")
                                .arg(theme::tint(color, done || current ? 46 : 20), color));
        rh->addWidget(mark);
        auto* name = ui::label(names[i]);
        name->setStyleSheet(current ? QStringLiteral("font-weight:700;") : (done ? QString() : QStringLiteral("color:%1;").arg(theme::Muted)));
        rh->addWidget(name, 1);
        rh->addWidget(ui::label(notes[i], "muted-sm"));
        lv->addWidget(row);
    }
    m_drawerLayout->addWidget(list);

    // Cómo va cada destino del resultado de la ronda.
    if (m_revisionPublish && !issue.revisions.isEmpty()) {
        auto* dest = new QWidget;
        auto* dg = new QGridLayout(dest);
        dg->setContentsMargins(0, 0, 0, 0);
        dg->setSpacing(6);
        int col = 0;
        for (const auto& step : m_revisionPublish->stepsFor(issue.id, 0)) {
            // El cierre en el gestor no es un destino más del resultado: sólo se enseña hecho.
            if (step.destination == RevisionPublishService::Destination::Close && !step.done) continue;
            auto* box = ui::card("card-flat");
            auto* bv = ui::vbox(box, 0, 3);
            bv->setContentsMargins(10, 8, 10, 8);
            const QString color = step.done ? theme::Green : (step.available ? theme::Amber : theme::Muted);
            auto* tag = ui::label(destinationTag(step.destination) + (step.done ? QStringLiteral(" ✓") : QString()), "eyebrow");
            tag->setStyleSheet(QStringLiteral("color:%1;").arg(color));
            bv->addWidget(tag);
            const QString text = step.done ? tr("Hecho") : (step.available ? tr("Pendiente") : tr("No disponible"));
            bv->addWidget(ui::label(text, "muted-sm"));
            box->setToolTip(step.blocked.isEmpty() ? step.detail : step.blocked);
            dg->addWidget(box, 0, col++);
        }
        m_drawerLayout->addWidget(dest);
    }
    m_drawerLayout->addStretch(1);
}

// ---- Detalle -------------------------------------------------------------------------------------

void IssuesView::buildDetail(QVBoxLayout* root) {
    // Del detalle se vuelve al tablero: el issue sigue elegido y su panel, abierto.
    auto* bar = new QWidget;
    auto* barh = ui::hbox(bar, 0, 8);
    barh->setContentsMargins(20, 12, 20, 0);
    auto* back = ui::button(tr("← Tablero"), "back");
    back->setObjectName(QStringLiteral("issuesBack"));
    connect(back, &QPushButton::clicked, this, [this]() { showDetail(false); });
    barh->addWidget(back);
    barh->addStretch(1);
    root->addWidget(bar);

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
    // La publicación en el gestor no tiene tarjeta: es un tag con su clave y su estado, y de él cuelgan
    // sus acciones (publicar, vincular, abrir, consultar el estado o desvincular).
    m_jiraChip = new QPushButton;
    m_jiraChip->setObjectName(QStringLiteral("issueJira"));
    m_jiraChip->setCursor(Qt::PointingHandCursor);
    m_jiraChip->setMenu(buildJiraMenu());
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

    // De la publicación en el gestor sólo pide hacer algo lo que está pendiente o sin confirmar: eso va
    // aquí, debajo del tag. Lo demás (clave, proyecto, tipo, estado y fechas) se lee en el propio tag.
    auto* jiraPending = ui::card("card-flat");
    jiraPending->setObjectName(QStringLiteral("issueJiraPending"));
    m_jiraPending = jiraPending;
    auto* ph = ui::hbox(jiraPending, 0, 10);
    ph->setContentsMargins(12, 10, 10, 10);
    m_jiraPendingText = ui::label(QString(), "muted-sm");
    m_jiraPendingText->setWordWrap(true);
    m_jiraPendingText->setStyleSheet(QStringLiteral("color:%1;").arg(theme::AmberSoft));
    ph->addWidget(m_jiraPendingText, 1);
    m_updateJiraButton = smallButton(tr("Actualizar en el gestor…"), "outline");
    m_updateJiraButton->setObjectName(QStringLiteral("issueUpdateJira"));
    connect(m_updateJiraButton, &QPushButton::clicked, this, [this]() { openPublishDialog(true); });
    ph->addWidget(m_updateJiraButton, 0, Qt::AlignTop);
    v->addWidget(jiraPending);

    auto* jiraUncertain = ui::card("card-flat");
    jiraUncertain->setObjectName(QStringLiteral("issueJiraUncertain"));
    m_jiraUncertain = jiraUncertain;
    auto* uh = ui::hbox(jiraUncertain, 0, 10);
    uh->setContentsMargins(12, 10, 10, 10);
    m_jiraUncertainText = ui::label(QString(), "muted-sm");
    m_jiraUncertainText->setWordWrap(true);
    m_jiraUncertainText->setStyleSheet(QStringLiteral("color:%1;").arg(theme::AmberSoft));
    uh->addWidget(m_jiraUncertainText, 1);
    v->addWidget(jiraUncertain);

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

    // Revisión: el control de calidad paso a paso, que es el trabajo del issue.
    QHBoxLayout* revisionActions;
    QVBoxLayout* revisionBody;
    auto* revisionCard = sectionCard(theme::Amber, &m_revisionHeader, &revisionActions, &revisionBody);
    revisionCard->setObjectName(QStringLiteral("issueRevisionCard"));
    m_revisionCard = revisionCard;
    m_revisionHeader->setObjectName(QStringLiteral("issueRevisionHeader"));
    m_revisionProgress = ui::label(QString(), "muted-sm");
    m_revisionProgress->setObjectName(QStringLiteral("issueRevisionProgress"));
    m_revisionProgress->setWordWrap(true);
    revisionBody->addWidget(m_revisionProgress);
    auto* steps = new QWidget;
    m_revisionSteps = ui::vbox(steps, 0, 2);
    revisionBody->addWidget(steps);
    v->addWidget(revisionCard);

    // Las rondas ya cerradas, que son el historial del control de calidad del requerimiento.
    QLabel* historyHeader;
    QHBoxLayout* historyActions;
    m_historyCard = sectionCard(theme::Muted, &historyHeader, &historyActions, &m_revisionsList);
    m_historyCard->setObjectName(QStringLiteral("issueHistoryCard"));
    historyHeader->setText(tr("REVISIONES ANTERIORES"));
    v->addWidget(m_historyCard);
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
    if (!m_selfEdit) {
        m_loadingDetail = true;
        m_title->setText(issue.title);
        m_state->setCurrentIndex(std::max(0, m_state->findData(static_cast<int>(issue.state))));
        m_priority->setCurrentIndex(std::max(0, m_priority->findData(static_cast<int>(issue.priority))));
        m_loadingDetail = false;
    }
    refreshRequirement(issue);
    refreshJira(issue);
    refreshRevision(issue);   // con ella, el plan, las ejecuciones y los bugs: cada uno en su paso
}

namespace {
/// Color con el que se enseña el resultado de una revisión.
QString outcomeColor(QaOutcome outcome) {
    switch (outcome) {
        case QaOutcome::Conforme: return theme::Green;
        case QaOutcome::Observado: return theme::Amber;
        case QaOutcome::Pendiente: return theme::Muted;
    }
    return theme::Muted;
}
} // namespace

namespace {
/// Una fila de la lista de pasos de la revisión: el número (o un visto si ya está hecho), qué es el
/// paso, cómo va y su acción. Los pasos hechos se apagan y el que toca queda destacado.
struct StepRow {
    QWidget* widget = nullptr;
    QHBoxLayout* actions = nullptr;
    QVBoxLayout* body = nullptr;   // lo que el paso tiene debajo: el plan con sus casos, los ciclos, los bugs
};

StepRow stepRow(int number, bool done, bool current, const QString& title, const QString& detail, const QString& detailName) {
    const QString color = done ? theme::Green : (current ? theme::Amber : theme::Muted);
    auto* row = ui::card(current ? "card" : "card-flat");
    // El paso son dos filas: la suya (número, qué es, cómo va y sus acciones) y, debajo y a lo ancho, lo
    // que cuelga de él. Así lo que cuelga no compite en anchura con los botones del paso.
    auto* rows = ui::vbox(row, 0, 0);
    rows->setContentsMargins(12, 10, 10, 10);
    auto* headWidget = new QWidget;
    auto* h = ui::hbox(headWidget, 0, 12);
    rows->addWidget(headWidget);

    auto* badge = ui::label(done ? QStringLiteral("✓") : QString::number(number), "eyebrow");
    badge->setAlignment(Qt::AlignCenter);
    badge->setFixedSize(24, 24);
    badge->setStyleSheet(QStringLiteral("background:%1;color:%2;border-radius:12px;font-weight:700;font-size:12px;")
                             .arg(theme::tint(color, done || current ? 46 : 22), color));
    h->addWidget(badge, 0, Qt::AlignTop);

    auto* text = new QWidget;
    auto* tv = ui::vbox(text, 0, 2);
    auto* name = ui::label(title);
    name->setWordWrap(true);
    if (current) name->setStyleSheet(QStringLiteral("font-weight:700;"));
    else if (done) name->setStyleSheet(QStringLiteral("color:%1;").arg(theme::Muted));
    tv->addWidget(name);
    auto* hint = ui::label(detail, "muted-sm");
    hint->setWordWrap(true);
    if (!detailName.isEmpty()) hint->setObjectName(detailName);
    tv->addWidget(hint);
    h->addWidget(text, 1);

    auto* body = new QWidget;
    auto* bv = ui::vbox(body, 0, 2);
    bv->setContentsMargins(24, 8, 0, 0);
    rows->addWidget(body);

    StepRow out;
    out.widget = row;
    out.actions = h;
    out.body = bv;
    return out;
}
} // namespace

void IssuesView::refreshRevision(const Issue& issue) {
    ui::clearLayout(m_revisionSteps);
    ui::clearLayout(m_revisionsList);
    // Sin el servicio (tests con un contexto mínimo) la tarjeta no tiene nada que contar.
    m_revisionCard->setVisible(m_records != nullptr);
    m_historyCard->setVisible(false);
    if (!m_records) return;

    // Lo mismo que cuentan la tarjeta y el panel del tablero, para que los tres digan igual qué toca.
    const RevisionSnapshot snapshot = snapshotOf(issue);
    const IssueProgress& progress = snapshot.progress;
    const IssueRevision* open = issue.currentRevision();
    const IssueRevision* last = issue.revisions.isEmpty() ? nullptr : &issue.revisions.last();
    const bool closed = snapshot.closed;
    const int number = snapshot.number;
    const QaOutcome outcome = closed ? issue.lastOutcome() : progress.suggested;

    m_revisionHeader->setText(issue.revisions.isEmpty() ? tr("REVISIÓN") : tr("REVISIÓN %1 · %2").arg(number).arg(label(issue.state).toUpper()));
    const QString blockers = progress.blockers.isEmpty() ? tr("nada pendiente") : progress.blockers.join(QStringLiteral(" · "));
    m_revisionProgress->setText(closed ? tr("Cerrada como %1 · %2 de %3 casos ejecutados · %4 bugs (%5 abiertos)")
                                             .arg(label(outcome))
                                             .arg(progress.executed)
                                             .arg(progress.cases)
                                             .arg(progress.bugs)
                                             .arg(progress.openBugs)
                                       : tr("%1 de %2 casos ejecutados · %3 superados · %4 fallidos · %5 bloqueados · %6 bugs (%7 abiertos) · "
                                            "resultado propuesto: %8 (%9)")
                                             .arg(progress.executed)
                                             .arg(progress.cases)
                                             .arg(progress.passed)
                                             .arg(progress.failed)
                                             .arg(progress.blocked)
                                             .arg(progress.bugs)
                                             .arg(progress.openBugs)
                                             .arg(label(outcome), blockers));
    m_revisionProgress->setStyleSheet(QStringLiteral("color:%1;").arg(outcomeColor(outcome)));

    // Los pasos del control de calidad, en el orden en que se hacen. El primero sin terminar es el que toca.
    const QStringList caseIds = IssueStore::caseIdsOf(issue, m_plans);
    const bool hasPlan = snapshot.hasPlan;
    const bool executed = snapshot.executed;
    const bool hasRecord = snapshot.hasRecord;
    const bool published = snapshot.published;
    int number_ = 0;
    bool currentTaken = false;
    auto step = [&](bool done, const QString& title, const QString& detail, const QString& detailName = QString()) {
        const bool current = !done && !currentTaken;
        currentTaken = currentTaken || current;
        const StepRow row = stepRow(++number_, done, current, title, detail, detailName);
        m_revisionSteps->addWidget(row.widget);
        return row;
    };

    // 1 · El plan con el que se prueba el requerimiento.
    {
        const QString planName = issue.planIds.isEmpty() ? QString() : [&] {
            const TestPlan* plan = m_plans.find(issue.planIds.first());
            return plan ? plan->name : issue.planIds.first();
        }();
        const StepRow row = step(hasPlan, tr("Preparar el plan de pruebas"),
                                 issue.planIds.isEmpty() ? tr("El requerimiento todavía no tiene plan")
                                                         : tr("%1 · %2 caso(s)").arg(planName).arg(caseIds.size()),
                                 QStringLiteral("issueStepPlanDetail"));
        auto* open = smallButton(issue.planIds.isEmpty() ? tr("Crear plan") : tr("Abrir plan"), hasPlan ? "ghost" : "outline");
        open->setObjectName(QStringLiteral("issueStepPlan"));
        connect(open, &QPushButton::clicked, this, [this]() {
            const Issue* issue = selected();
            if (!issue) return;
            if (issue->planIds.isEmpty()) createPlan();
            else emit openPlanRequested(issue->planIds.first());
        });
        row.actions->addWidget(open);
        auto* another = smallButton(tr("+ Otro plan"), "ghost", tr("Crea otro plan para este requerimiento y lo abre para componerlo"));
        another->setObjectName(QStringLiteral("issueNewPlan"));
        another->setVisible(!issue.planIds.isEmpty());
        connect(another, &QPushButton::clicked, this, &IssuesView::createPlan);
        row.actions->addWidget(another);
        auto* link = smallButton(tr("Vincular plan…"), "ghost", tr("Enlaza al issue un plan que ya existe en el proyecto"));
        link->setObjectName(QStringLiteral("issueLinkPlan"));
        connect(link, &QPushButton::clicked, this, &IssuesView::pickPlan);
        row.actions->addWidget(link);
        fillPlans(issue, row.body);
    }

    // 2 · Ejecutarlo: el ciclo del plan es lo que abre la revisión y da sus resultados.
    {
        // En qué ambientes se ha probado esta ronda: cada ciclo lo dice desde que se arranca, y es
        // parte de la identidad de sus resultados (va con ellos a Zephyr).
        QStringList environments;
        for (const auto& cycle : IssueStore::cyclesOfRevision(issue, m_history, number))
            if (const QString env = cycle.environment.trimmed(); !env.isEmpty() && !environments.contains(env))
                environments << env;
        const QList<PlanRun> cycles = IssueStore::cyclesOf(issue, m_history);
        QString detail = tr("%1 ejecución(es) de sus planes").arg(cycles.size());
        detail += executed ? tr(" · %1 de %2 casos ejecutados en esta revisión").arg(progress.executed).arg(progress.cases)
                           : tr(" · arrancar un ciclo del plan abre la revisión y deja el issue en pruebas");
        if (!environments.isEmpty()) detail += tr(" · ambiente: %1").arg(environments.join(tr(", ")));
        const StepRow row = step(executed, tr("Ejecutar el plan"), detail, QStringLiteral("issueStepRunDetail"));
        auto* actions = row.actions;
        auto* open = smallButton(tr("Ir al plan"), "ghost", tr("Abrir el plan para componerlo antes de ejecutarlo"));
        open->setObjectName(QStringLiteral("issueStepOpenPlan"));
        open->setEnabled(hasPlan);
        connect(open, &QPushButton::clicked, this, [this]() {
            const Issue* issue = selected();
            if (issue && !issue->planIds.isEmpty()) emit openPlanRequested(issue->planIds.first());
        });
        actions->addWidget(open);
        // El ciclo se arranca desde aquí: es el paso siguiente del issue y no hay por qué salir a buscarlo.
        auto* run = smallButton(tr("Ejecutar plan…"), executed ? "ghost" : "outline",
                                tr("Arranca un ciclo del plan: pregunta el ambiente y lleva a la ejecución"));
        run->setObjectName(QStringLiteral("issueStepRun"));
        run->setEnabled(!runnablePlans(issue).isEmpty());
        connect(run, &QPushButton::clicked, this, [this, run]() { runPlan(run); });
        actions->addWidget(run);
        // Lo que quedó roto no obliga a repetir el plan entero: se continúa la ronda por donde se quedó.
        if (const QString pending = snapshot.continuable; !pending.isEmpty()) {
            const PlanReport report = m_history.report(pending);
            auto* proceed = smallButton(tr("Continuar lo fallado…"), "outline",
                                        tr("Vuelve a ejecutar los %1 caso(s) fallado(s) o bloqueado(s) del ciclo %2, "
                                           "cada uno desde el paso que se rompió")
                                            .arg(report.brokenCaseIds().size())
                                            .arg(pending));
            proceed->setObjectName(QStringLiteral("issueStepContinue"));
            connect(proceed, &QPushButton::clicked, this, [this, pending]() { emit continueCycleRequested(pending); });
            actions->addWidget(proceed);
        }
        fillResults(issue, cycles, row.body);
    }

    // 3 · Los bugs que salieron de esas ejecuciones: con alguno abierto, el requerimiento no queda conforme.
    {
        const QList<IssueLink> bugs = bugsOf(issue);
        const int openBugs = int(std::count_if(bugs.cbegin(), bugs.cend(), [](const IssueLink& b) { return !b.resolved; }));
        const StepRow row = step(openBugs == 0, tr("Revisar los bugs reportados"),
                                 bugs.isEmpty() ? tr("Los que se reporten en sus ejecuciones salen aquí, cuentan en el acta y se "
                                                     "enlazan al publicar")
                                                : tr("%1 bug(s) · %2 abierto(s)").arg(bugs.size()).arg(openBugs),
                                 QStringLiteral("issueStepBugsDetail"));
        fillBugs(issue, bugs, row.body);
    }

    // 4 · El acta del control de calidad.
    {
        auto* actions = step(hasRecord, tr("Generar el acta (R-213)"),
                             hasRecord ? tr("%1 · generada el %2").arg(QFileInfo(last->documentPath).fileName(), when(last->documentAt))
                                       : tr("Con lo del requerimiento, la ejecución elegida y los bugs de la revisión")).actions;
        if (hasRecord) {
            auto* openDoc = smallButton(tr("Abrir acta"), "ghost");
            connect(openDoc, &QPushButton::clicked, this, [this, path = last->documentPath]() {
                emit openUrlRequested(QUrl::fromLocalFile(path).toString());
            });
            actions->addWidget(openDoc);
        }
        auto* record = smallButton(hasRecord ? tr("Regenerar…") : tr("Generar acta…"), hasRecord ? "ghost" : "outline",
                                   tr("Arma el acta de control de calidad (R-213) con lo que hay en el issue y la guarda como .docx"));
        record->setObjectName(QStringLiteral("issueGenerateRecord"));
        record->setEnabled(!issue.revisions.isEmpty() || progress.executed > 0);
        connect(record, &QPushButton::clicked, this, [this]() { generateRecord(); });
        actions->addWidget(record);
    }

    // 5 · Cerrar la revisión con su resultado.
    {
        auto* actions = step(closed, tr("Cerrar la revisión"),
                             closed ? tr("Cerrada el %1 como %2").arg(when(last->closedAt), label(outcome))
                                    : tr("Se cierra con el resultado del control: conforme u observado")).actions;
        if (open) {
            auto* close = smallButton(tr("Cerrar revisión…"), "outline",
                                      tr("Deja la revisión cerrada con su resultado: conforme u observado"));
            close->setObjectName(QStringLiteral("issueCloseRevision"));
            connect(close, &QPushButton::clicked, this, &IssuesView::closeRevision);
            actions->addWidget(close);
        }
    }

    // 6 · Publicar el resultado donde toca.
    {
        QStringList where;
        if (last && !last->jira.isEmpty() && !last->jira.uncertain) where << tr("gestor");
        if (last && !last->gesreq.isEmpty() && !last->gesreq.uncertain) where << tr("GESREQ");
        auto* actions = step(published, tr("Publicar el resultado"),
                             where.isEmpty() ? tr("Los ciclos a Zephyr, el resultado y el acta al gestor y el registro en GESREQ")
                                             : tr("Publicado en %1").arg(where.join(tr(" y ")))).actions;
        auto* publish = smallButton(published ? tr("Publicar…") : tr("Publicar…"), "primary",
                                    tr("Publica los planes con sus casos en Zephyr, deja el resultado y el acta en el gestor "
                                       "y registra el control de calidad en GESREQ"));
        publish->setObjectName(QStringLiteral("issuePublishRevision"));
        publish->setEnabled(m_revisionPublish != nullptr && closed);
        if (!closed) publish->setToolTip(tr("Cierra antes la revisión: se publica el resultado de una revisión terminada"));
        connect(publish, &QPushButton::clicked, this, [this]() { publishRevision(); });
        actions->addWidget(publish);
    }

    // Y, cerrada la ronda, la siguiente: un requerimiento observado vuelve a pruebas.
    if (!open) {
        auto* actions = step(false, tr("Volver a probar"),
                             closed && outcome == QaOutcome::Observado
                                     ? tr("El requerimiento quedó observado: al corregirlo se abre la revisión %1").arg(number + 1)
                                     : tr("Abre otra ronda de pruebas del requerimiento")).actions;
        auto* next = smallButton(closed ? tr("Nueva revisión") : tr("Abrir revisión"), "outline",
                                 tr("El requerimiento vuelve a pruebas: abre la ronda siguiente del acta"));
        next->setObjectName(QStringLiteral("issueNewRevision"));
        connect(next, &QPushButton::clicked, this, &IssuesView::openRevision);
        actions->addWidget(next);
    }

    // Revisiones ya cerradas, de la más reciente a la más antigua: son el historial del requerimiento.
    int closedRevisions = 0;
    for (auto it = issue.revisions.crbegin(); it != issue.revisions.crend(); ++it) {
        if (it->isOpen()) continue;
        ++closedRevisions;
        QHBoxLayout* h;
        auto* row = listRow(&h);
        const QString color = outcomeColor(it->outcome);
        h->addWidget(ui::pill(tr("REV %1").arg(it->number), theme::tint(theme::Muted, 30), theme::Muted));
        h->addWidget(ui::pill(label(it->outcome).toUpper(), theme::tint(color, 46), color));
        h->addWidget(ui::label(it->closedAt.toString(QStringLiteral("dd/MM/yyyy")), "muted-sm"), 1);
        // Dónde llegó el resultado de esa ronda y qué le falta: cada destino, con lo que dice de él la
        // propia publicación, así el historial y el diálogo cuentan lo mismo.
        const int number = it->number;
        if (m_revisionPublish)
            for (const auto& step : m_revisionPublish->stepsFor(issue.id, number)) {
                if (!step.done && !step.available) continue;
                // Cerrar el issue sólo le toca a una ronda conforme; en las observadas no es algo pendiente.
                if (step.destination == RevisionPublishService::Destination::Close && !step.done && it->outcome != QaOutcome::Conforme)
                    continue;
                const QString color = step.done ? theme::Green : theme::Amber;
                auto* chip = ui::pill(QStringLiteral("%1 %2").arg(destinationTag(step.destination), step.done ? QStringLiteral("✓")
                                                                                                             : QStringLiteral("—")),
                                      theme::tint(color, 40), color);
                chip->setToolTip(step.detail);
                h->addWidget(chip);
            }
        if (it->hasDocument()) {
            auto* openRecord = smallButton(tr("Abrir acta"), "ghost");
            connect(openRecord, &QPushButton::clicked, this, [this, path = it->documentPath]() {
                emit openUrlRequested(QUrl::fromLocalFile(path).toString());
            });
            h->addWidget(openRecord);
        } else if (m_records) {
            // Sin acta no hay nada que adjuntar ni que registrar en GESREQ: se puede levantar ahora.
            auto* record = smallButton(tr("Generar acta…"), "ghost",
                                       tr("Levanta el acta de la revisión %1 con lo que se probó en ella").arg(number));
            record->setObjectName(QStringLiteral("issueRevisionRecord-%1").arg(number));
            connect(record, &QPushButton::clicked, this, [this, number]() { generateRecord(number); });
            h->addWidget(record);
        }
        // Una ronda que se quedó a medias (se abrió la siguiente antes de publicarla) se termina desde aquí.
        if (m_revisionPublish)
            if (const QList<RevisionPublishService::Destination> pending = m_revisionPublish->pendingFor(issue.id, number);
                !pending.isEmpty()) {
                QStringList names;
                for (const auto destination : pending) names << RevisionPublishService::label(destination);
                auto* publish = smallButton(tr("Completar publicación…"), "outline",
                                            tr("Falta publicar el resultado de la revisión %1 en %2").arg(number).arg(names.join(tr(" y "))));
                publish->setObjectName(QStringLiteral("issueRevisionPublish-%1").arg(number));
                connect(publish, &QPushButton::clicked, this, [this, number]() { publishRevision(number); });
                h->addWidget(publish);
            }
        m_revisionsList->addWidget(row);
    }
    m_historyCard->setVisible(closedRevisions > 0);
}

void IssuesView::generateRecord(int revision) {
    const Issue* issue = selected();
    if (!issue || !m_records) return;
    const QString issueId = issue->id;
    // De la ronda en curso se propone lo que dicen sus pruebas; de una ya cerrada, el resultado con el
    // que se cerró: lo que quedó pendiente entonces ya no es una decisión de ahora.
    const IssueRevision* round = issue->revision(revision);
    const bool closed = round && !round->isOpen();
    const IssueProgress progress = closed ? IssueProgress{} : m_records->progressFor(issueId);
    const QaOutcome suggested = closed ? round->outcome : progress.suggested;

    // Con qué ejecución se levanta el acta: si la revisión tuvo varias, se elige aquí y el acta se
    // rehace con la que se escoja.
    QList<QualityRecordDialog::CycleChoice> choices;
    for (const auto& cycle : m_records->cyclesFor(issueId, revision))
        choices << QualityRecordDialog::CycleChoice{
            cycle.plan.id,
            tr("%1 · %2 · %3 de %4 ejecutados, %5 superados")
                .arg(cycle.plan.name, when(cycle.plan.startedAt))
                .arg(cycle.executed)
                .arg(cycle.total())
                .arg(cycle.passed)};
    const QString current = m_records->recordCycleFor(issueId, revision);

    QualityRecordDialog dialog(m_records->draftFor(issueId, current, revision), suggested, progress.blockers, choices, current,
                               [this, issueId, revision](const QString& planRunId) {
                                   return m_records->draftFor(issueId, planRunId, revision);
                               },
                               this);
    if (dialog.exec() != QDialog::Accepted) return;

    const QString path = QFileDialog::getSaveFileName(this, tr("Guardar el acta"), m_records->suggestedFileName(issueId, revision),
                                                      tr("Documentos de Word (*.docx)"));
    if (path.isEmpty()) return;
    const auto result = m_records->generate(issueId, dialog.record(), path, dialog.planRunId(), revision);
    if (!result.ok) {
        emit toast(tr("No se pudo generar el acta · %1").arg(result.error), theme::Red);
        return;
    }
    emit toast(tr("Acta generada en %1").arg(QFileInfo(result.path).fileName()), theme::Green);
    loadDetail();
}

void IssuesView::publishRevision(int revision) {
    const Issue* issue = selected();
    if (!issue || !m_records || !m_revisionPublish) return;
    const IssueRevision* round = issue->revision(revision);
    if (!round) return;
    const QString issueId = issue->id;
    const int number = round->number;
    const QaOutcome outcome = round->isOpen() ? m_records->progressFor(issueId).suggested : round->outcome;
    const QaOutcome proposed = outcome == QaOutcome::Pendiente ? QaOutcome::Observado : outcome;
    const QString comment = m_records->summaryFor(issueId, round->record, proposed, round->planRunId, number);

    auto* dialog = new RevisionPublishDialog(*m_revisionPublish, issueId, proposed, comment, round->documentPath, number, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &RevisionPublishDialog::published, this, [this](bool ok) {
        loadDetail();
        emit toast(ok ? tr("Resultado publicado") : tr("La publicación terminó con avisos: mira el detalle de cada destino"),
                   ok ? theme::Green : theme::Amber);
    });
    dialog->open();
}

void IssuesView::closeRevision() {
    const Issue* issue = selected();
    if (!issue || !issue->currentRevision() || !m_records) return;
    const QString issueId = issue->id;
    const IssueProgress progress = m_records->progressFor(issueId);

    QMessageBox box(this);
    box.setWindowTitle(tr("Cerrar la revisión"));
    box.setIcon(QMessageBox::Question);
    box.setText(tr("¿Con qué resultado se cierra la revisión del requerimiento?"));
    box.setInformativeText(progress.blockers.isEmpty()
                               ? tr("Se propone «%1»: no queda nada pendiente.").arg(label(progress.suggested))
                               : tr("Se propone «%1»: %2.").arg(label(progress.suggested), progress.blockers.join(QStringLiteral(", "))));
    auto* conforme = box.addButton(tr("Conforme"), QMessageBox::AcceptRole);
    auto* observado = box.addButton(tr("Observado"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(progress.suggested == QaOutcome::Conforme ? conforme : observado);
    box.exec();
    if (box.clickedButton() != conforme && box.clickedButton() != observado) return;

    const QaOutcome outcome = box.clickedButton() == conforme ? QaOutcome::Conforme : QaOutcome::Observado;
    m_issues.closeRevision(issueId, outcome);
    emit toast(outcome == QaOutcome::Conforme ? tr("Revisión cerrada como conforme")
                                              : tr("Revisión cerrada con observaciones: al volver a probar se abre la siguiente"),
               outcome == QaOutcome::Conforme ? theme::Green : theme::Amber);
}

void IssuesView::openRevision() {
    const Issue* issue = selected();
    if (!issue) return;
    const int number = m_issues.openRevision(issue->id);
    if (number > 0) emit toast(tr("Revisión %1 abierta: el issue vuelve a pruebas").arg(number), theme::Blue);
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

QMenu* IssuesView::buildJiraMenu() {
    auto* menu = new QMenu(this);
    auto action = [this, menu](const QString& text, const char* name, void (IssuesView::*slot)()) {
        auto* a = menu->addAction(text);
        a->setObjectName(QString::fromLatin1(name));
        connect(a, &QAction::triggered, this, slot);
        return a;
    };
    m_publishAction = action(tr("Publicar en el gestor…"), "issuePublish", &IssuesView::publishToJira);
    m_publishAction->setToolTip(tr("Crea el issue en el gestor a partir de este, revisándolo antes"));
    m_linkJiraAction = action(tr("Vincular issue…"), "issueLinkJira", &IssuesView::linkJira);
    m_linkJiraAction->setToolTip(tr("Enlaza uno que ya existe en el gestor en vez de crear otro"));
    m_openJiraAction = menu->addAction(tr("Abrir en el gestor"));
    m_openJiraAction->setObjectName(QStringLiteral("issueOpenJira"));
    connect(m_openJiraAction, &QAction::triggered, this, [this]() {
        if (const Issue* issue = selected()) emit openUrlRequested(issue->publication.url);
    });
    m_refreshJiraAction = action(tr("Actualizar estado"), "issueRefreshJira", &IssuesView::refreshJiraStatus);
    menu->addSeparator();
    m_unlinkJiraAction = action(tr("Desvincular"), "issueUnlinkJira", &IssuesView::unlinkJira);
    m_unlinkJiraAction->setToolTip(tr("QAflow olvida la representación; en el gestor no se borra nada"));
    return menu;
}

void IssuesView::refreshJira(const Issue& issue) {
    // Sin el servicio (tests con un contexto mínimo) no hay gestor del que hablar.
    m_jiraChip->setVisible(m_publish != nullptr);
    if (!m_publish) {
        m_jiraPending->setVisible(false);
        m_jiraUncertain->setVisible(false);
        return;
    }
    const IssuePublication& p = issue.publication;
    const bool published = issue.isPublished();
    const bool configured = m_publish->canPublish();
    const bool pending = m_publish->needsUpdate(issue);
    m_publishAction->setVisible(!published);
    m_publishAction->setEnabled(configured && !m_publishing);
    m_linkJiraAction->setVisible(!published);
    m_linkJiraAction->setEnabled(configured && !m_publishing);
    m_openJiraAction->setVisible(published);
    m_openJiraAction->setEnabled(!p.url.isEmpty());
    m_refreshJiraAction->setVisible(published);
    m_refreshJiraAction->setEnabled(!m_publishing);
    m_unlinkJiraAction->setVisible(published);
    m_jiraPending->setVisible(pending);
    m_jiraUncertain->setVisible(!published && p.uncertain);
    m_jiraPendingText->setText(tr("⚠ El título o la descripción cambiaron desde lo último que se publicó. En el gestor sigue lo anterior."));
    m_jiraUncertainText->setText(tr("⚠ El último envío se cortó sin respuesta (%1): puede haberse creado igualmente. Búscalo en %2 por la etiqueta %3 "
                                    "y vincúlalo; si no está, vuelve a publicar.")
                                     .arg(p.lastError, m_publish->destination(), issue.id));

    // El tag dice lo que hay que saber de un vistazo —la clave y en qué estado está en el gestor— y el
    // resto (instancia, proyecto, tipo y fechas) se lee al pasar por encima.
    QString text;
    QString color = theme::Muted;
    QStringList lines;
    if (published) {
        text = p.status.isEmpty() ? p.key : QStringLiteral("%1 · %2").arg(p.key, p.status);
        color = pending ? theme::Amber : (p.resolved ? theme::Green : theme::Blue);
        const QString where = p.project.isEmpty() ? p.tracker : QStringLiteral("%1 · %2").arg(p.tracker, p.project);
        lines << QStringLiteral("%1 · %2").arg(p.key, where);
        if (!p.issueType.isEmpty()) lines << tr("Tipo: %1").arg(p.issueType);
        lines << (p.linked ? tr("Vinculado el %1 · lo creó otra persona en el gestor").arg(when(p.publishedAt))
                           : tr("Publicado desde QAflow el %1").arg(when(p.publishedAt)));
        lines << (p.status.isEmpty() ? tr("Estado del gestor sin consultar")
                                     : tr("Estado en el gestor: %1 · consultado el %2").arg(p.status, when(p.statusCheckedAt)));
    } else if (!configured) {
        text = tr("Sin publicar en Jira");
        lines << tr("Configura la conexión con el gestor y su proyecto en Ajustes para publicar este issue.");
    } else {
        text = tr("Sin publicar en Jira");
        color = p.uncertain ? theme::Amber : theme::Muted;
        lines << tr("Sin publicar. Se crearía en %1.").arg(m_publish->destination());
    }
    m_jiraChip->setText(text + QStringLiteral("  ▾"));
    m_jiraChip->setStyleSheet(chipButtonStyle(color));
    m_jiraChip->setToolTip(lines.join(QLatin1Char('\n')));
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
                    if (!r.warning.isEmpty()) emit self->toast(tr("%1 creado en el gestor · %2").arg(r.key, r.warning), theme::Amber);
                    else emit self->toast(update ? tr("%1 actualizado en el gestor").arg(r.key) : tr("%1 creado en el gestor").arg(r.key), theme::Green);
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
    m_refreshJiraAction->setEnabled(false);
    QPointer<IssuesView> self(this);
    m_publish->refreshStatus(issue->id, [self](const IssuePublishService::Result& r) {
        if (!self) return;
        self->m_publishing = false;
        self->loadDetail();
        if (!r.ok) emit self->toast(tr("No se pudo consultar el estado en el gestor · %1").arg(r.error), theme::Red);
    });
}

void IssuesView::fillPlans(const Issue& issue, QVBoxLayout* into) {
    if (issue.planIds.isEmpty()) {
        into->addWidget(ui::label(tr("Sin plan todavía. Crea el plan con el que se prueba el requerimiento: sus casos son los "
                                     "casos del issue y sus ejecuciones, sus resultados."),
                                  "muted-sm"));
        return;
    }
    for (const auto& planId : issue.planIds) {
        const TestPlan* plan = m_plans.find(planId);
        QHBoxLayout* h;
        auto* row = listRow(&h);
        h->addWidget(ui::label(planId, "mono-muted"));
        auto* name = new QLabel(plan ? (plan->archived ? tr("%1 (archivado)").arg(plan->name) : plan->name)
                                     : tr("Ya no existe en los planes del proyecto"));
        name->setWordWrap(true);
        h->addWidget(name, 1);
        const QStringList caseIds = plan ? m_plans.orderedCaseIds(planId) : QStringList{};
        if (plan) {
            QString summary = tr("%1 casos").arg(caseIds.size());
            const int cycles = m_plans.cycleCount(planId);
            if (cycles > 0) summary += tr(" · %1 ejecución(es)").arg(cycles);
            h->addWidget(ui::label(summary, "muted-sm"));
            auto* open = smallButton(tr("Abrir plan"), "ghost");
            connect(open, &QPushButton::clicked, this, [this, planId]() { emit openPlanRequested(planId); });
            h->addWidget(open);
            auto* run = smallButton(tr("Ejecutar"), "ghost", tr("Arrancar un ciclo de este plan"));
            run->setObjectName(QStringLiteral("issuePlanRun-%1").arg(planId));
            run->setEnabled(!plan->archived && !caseIds.isEmpty());
            connect(run, &QPushButton::clicked, this, [this, planId]() { emit runPlanRequested(planId); });
            h->addWidget(run);
        }
        auto* unlink = unlinkButton(tr("Desvincular el plan del issue (el plan no se borra)"));
        connect(unlink, &QPushButton::clicked, this, [this, issueId = issue.id, planId]() { m_issues.unlinkPlan(issueId, planId); });
        h->addWidget(unlink);
        into->addWidget(row);

        // Los casos del plan son los casos del issue: se ven aquí, con lo que dio su última ejecución.
        if (caseIds.isEmpty()) {
            if (plan) into->addWidget(ui::label(tr("    El plan todavía no tiene casos: ábrelo y añádeselos."), "muted-sm"));
            continue;
        }
        for (const auto& caseId : caseIds) {
            const TestCase* c = m_cases.find(caseId);
            QHBoxLayout* ch;
            auto* line = listRow(&ch);
            ch->setContentsMargins(28, 5, 8, 5);
            ch->addWidget(ui::label(caseId, "mono-muted"));
            auto* title = new QLabel(c ? (c->title.isEmpty() ? tr("(sin título)") : c->title)
                                       : tr("Ya no existe en los casos del proyecto"));
            title->setWordWrap(true);
            ch->addWidget(title, 1);
            if (c) {
                ch->addWidget(ui::label(QStringLiteral("%1 · %2").arg(label(c->status), c->lastRun.label()), "muted-sm"));
                auto* open = smallButton(tr("Abrir"), "ghost");
                connect(open, &QPushButton::clicked, this, [this, caseId]() { emit openCaseRequested(caseId); });
                ch->addWidget(open);
            }
            into->addWidget(line);
        }
    }
}

void IssuesView::fillResults(const Issue& issue, const QList<PlanRun>& cycles, QVBoxLayout* into) {
    // Lo que se enseña son las ejecuciones de los planes del issue, no todas las de sus casos: un caso
    // puede estar en otros planes y esas ejecuciones no son resultados de este requerimiento.
    if (cycles.isEmpty()) {
        into->addWidget(ui::label(issue.planIds.isEmpty()
                                          ? tr("Sin planes: los resultados del issue son los de los ciclos de sus planes.")
                                          : tr("Todavía no se ha ejecutado ningún plan del issue."),
                                  "muted-sm"));
        return;
    }
    const QDateTime revisionStart = issue.revisions.isEmpty() ? QDateTime() : issue.revisions.last().startedAt;
    const int currentRevision = issue.revisions.isEmpty() ? 0 : issue.revisions.last().number;
    int shownRuns = 0;
    for (const auto& cycle : cycles) {
        const PlanReport report = m_history.report(cycle.id);
        QHBoxLayout* ch;
        auto* head = listRow(&ch);
        const QString color = verdictColor(report.verdict());
        ch->addWidget(ui::pill(label(report.verdict()).toUpper(), theme::tint(color, 46), color));
        // El nombre del ciclo arriba y sus cifras debajo: la fila cuelga de un paso, así que no hay
        // anchura para ponerlo todo en línea.
        auto* text = new QWidget;
        auto* tv = ui::vbox(text, 0, 2);
        auto* name = new QLabel(cycle.name);
        name->setWordWrap(true);
        tv->addWidget(name);
        auto* counters = ui::label(tr("%1 de %2 ejecutados · %3 superados · %4 fallidos · %5 bloqueados · %6")
                                       .arg(report.executed)
                                       .arg(report.total())
                                       .arg(report.passed)
                                       .arg(report.failed)
                                       .arg(report.blocked)
                                       .arg(when(cycle.startedAt)),
                                   "muted-sm");
        counters->setWordWrap(true);
        tv->addWidget(counters);
        ch->addWidget(text, 1);
        // De qué ronda es el ciclo y dónde se probó: es lo que lo distingue de los demás ciclos del
        // mismo requerimiento, y es lo que viaja con él a Zephyr.
        if (cycle.revision > 0)
            ch->addWidget(ui::pill(tr("REV %1").arg(cycle.revision), theme::tint(theme::Muted, 30), theme::Muted));
        if (!cycle.environment.trimmed().isEmpty())
            ch->addWidget(ui::pill(cycle.environment.trimmed().toUpper(), theme::tint(theme::Blue, 26), theme::Blue));
        // Los ciclos dicen de qué ronda son; los anteriores a eso, por cuándo empezaron.
        const bool ofThisRound = cycle.revision > 0 ? cycle.revision == currentRevision
                                                    : (revisionStart.isValid() && cycle.startedAt >= revisionStart);
        if (ofThisRound) ch->addWidget(ui::pill(tr("REVISIÓN EN CURSO"), theme::tint(theme::Blue, 30), theme::Blue));
        if (cycle.isContinuation())
            ch->addWidget(ui::pill(tr("CONTINÚA %1").arg(cycle.continuesCycleId), theme::tint(theme::Amber, 30), theme::Amber));
        if (cycle.isPublished()) ch->addWidget(ui::pill(tr("EN ZEPHYR"), theme::tint(theme::Green, 30), theme::Green));
        if (report.canContinue()) {
            auto* proceed = smallButton(tr("Continuar"), "ghost",
                                        tr("Volver a ejecutar los %1 caso(s) fallado(s) o bloqueado(s) de este ciclo")
                                            .arg(report.brokenCaseIds().size()));
            proceed->setObjectName(QStringLiteral("issueCycleContinue-%1").arg(cycle.id));
            connect(proceed, &QPushButton::clicked, this, [this, id = cycle.id]() { emit continueCycleRequested(id); });
            ch->addWidget(proceed);
        }
        auto* openPlan = smallButton(tr("Ver plan"), "ghost", tr("Abrir el plan de esta ejecución"));
        const QString planId = cycle.planId;
        openPlan->setEnabled(!planId.isEmpty());
        connect(openPlan, &QPushButton::clicked, this, [this, planId]() { emit openPlanRequested(planId); });
        ch->addWidget(openPlan);
        into->addWidget(head);

        // Los casos de ese ciclo, hasta donde cabe sin convertir la tarjeta en el historial entero.
        for (const auto& row : report.rows) {
            if (!row.executed) continue;
            if (shownRuns >= kMaxResults) break;
            ++shownRuns;
            QHBoxLayout* h;
            auto* line = listRow(&h);
            h->setContentsMargins(28, 5, 8, 5);
            const QString runColor = verdictColor(row.run.verdict);
            h->addWidget(ui::pill(label(row.run.verdict).toUpper(), theme::tint(runColor, 46), runColor));
            h->addWidget(ui::label(row.caseId, "mono-muted"));
            auto* title = new QLabel(row.title);
            title->setWordWrap(true);
            h->addWidget(title, 1);
            if (!row.testKey.isEmpty()) h->addWidget(ui::label(row.testKey, "mono-muted"));
            h->addWidget(ui::label(when(row.run.finishedAt), "muted-sm"));
            auto* open = smallButton(tr("Ver"), "ghost", tr("Abrir la ejecución en el historial"));
            connect(open, &QPushButton::clicked, this, [this, runId = row.run.id]() { emit openRunRequested(runId); });
            h->addWidget(open);
            into->addWidget(line);
        }
        if (shownRuns >= kMaxResults) {
            into->addWidget(ui::label(tr("…el resto de las ejecuciones está en el historial"), "muted-sm"));
            break;
        }
    }
}

QList<IssueLink> IssuesView::bugsOf(const Issue& issue) const {
    // Los bugs del issue son los que se encontraron ejecutando sus ciclos, del más reciente al
    // primero. Los reportados antes de que el bug anotara su ejecución no lo saben: de ésos se
    // cuentan los de los casos del issue, como se hacía entonces.
    if (!m_bugLedger) return {};
    QSet<QString> cycleIds;
    for (const auto& cycle : IssueStore::cyclesOf(issue, m_history)) cycleIds.insert(cycle.id);
    const QStringList caseIds = IssueStore::caseIdsOf(issue, m_plans);
    QList<IssueLink> bugs;
    for (const auto& bug : m_bugLedger->issues()) {
        const bool linked = !bug.planRunId.trimmed().isEmpty();
        if (linked ? cycleIds.contains(bug.planRunId) : caseIds.contains(bug.caseId)) bugs << bug;
    }
    std::sort(bugs.begin(), bugs.end(), [](const IssueLink& a, const IssueLink& b) { return a.createdAt > b.createdAt; });
    return bugs;
}

void IssuesView::fillBugs(const Issue& issue, const QList<IssueLink>& bugs, QVBoxLayout* into) {
    if (bugs.isEmpty()) return;
    const QDateTime since = issue.revisions.isEmpty() ? QDateTime() : issue.revisions.last().startedAt;
    for (const auto& bug : bugs) {
        QHBoxLayout* h;
        auto* row = listRow(&h);
        const QString color = bug.resolved ? theme::Green : theme::Red;
        h->addWidget(ui::pill(bug.classification.toUpper(), theme::tint(color, 46), color));
        h->addWidget(ui::label(bug.key, "mono-muted"));
        auto* title = new QLabel(bug.title.isEmpty() ? tr("(sin título)") : bug.title);
        title->setWordWrap(true);
        h->addWidget(title, 1);
        // De dónde salió: su caso y, si se sabe, el paso que falló.
        h->addWidget(ui::label(bug.step > 0 ? tr("%1 · paso %2").arg(bug.caseId).arg(bug.step) : bug.caseId, "muted-sm"));
        if (since.isValid() && bug.createdAt.isValid() && bug.createdAt >= since)
            h->addWidget(ui::pill(tr("ESTA REVISIÓN"), theme::tint(theme::Blue, 30), theme::Blue));
        h->addWidget(ui::label(bug.status.isEmpty() ? BugReport::severityLabel(bug.severity) : bug.status, "muted-sm"));
        h->addWidget(ui::label(when(bug.createdAt), "muted-sm"));
        if (!bug.url.isEmpty()) {
            auto* open = smallButton(tr("Abrir"), "ghost", tr("Abrir el bug en el gestor"));
            connect(open, &QPushButton::clicked, this, [this, url = bug.url]() { emit openUrlRequested(url); });
            h->addWidget(open);
        }
        into->addWidget(row);
    }
}

// ---- Acciones ------------------------------------------------------------------------------------

void IssuesView::editSelected(const std::function<void(Issue&)>& mutate) {
    const QString id = m_issues.selectedId();
    if (id.isEmpty()) return;
    m_selfEdit = true;
    m_issues.updateIssue(id, mutate);
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
    showDetail(true);
    // El requerimiento llega con lo suyo listo: su issue en el gestor y el plan con el que se prueba.
    const QString planId = ensurePlan(id);
    emit toast(planId.isEmpty() ? tr("%1 · pruebas del requerimiento %2").arg(id, requirement.id)
                                : tr("%1 · pruebas del requerimiento %2 · plan %3 listo").arg(id, requirement.id, planId),
               theme::Green);
    publishImported(id);
}

QString IssuesView::ensurePlan(const QString& issueId) {
    const Issue* issue = m_issues.find(issueId);
    if (!issue) return {};
    // Un plan que ya esté vinculado (y siga existiendo) es el suyo: no se crea otro.
    for (const auto& planId : issue->planIds)
        if (m_plans.find(planId)) return {};
    const QString planId = m_plans.createPlan(planNameFor(*issue));
    m_issues.linkPlan(issueId, planId);
    return planId;
}

void IssuesView::publishImported(const QString& issueId) {
    const Issue* issue = m_issues.find(issueId);
    if (!issue || issue->isPublished() || !m_publish || !m_publish->canPublish()) return;
    // Un envío anterior sin confirmar no se repite solo: puede haber creado ya el issue en el gestor.
    if (issue->publication.uncertain) return;
    QPointer<IssuesView> self(this);
    // El tipo de incidencia sale del proyecto de destino; si no se puede leer, se usa el de siempre.
    m_publish->fetchIssueTypes([self, issueId](const QStringList&) {
        if (!self) return;
        const Issue* issue = self->m_issues.find(issueId);
        if (!issue || issue->isPublished()) return;
        self->m_publish->publish(issueId, self->m_publish->draftFor(*issue), [self, issueId](const IssuePublishService::Result& r) {
            if (!self) return;
            self->loadDetail();
            if (r.ok) {
                // Creado queda igual aunque no se haya podido asignar: se dice, para asignarlo a mano.
                if (!r.warning.isEmpty()) emit self->toast(tr("%1 creado en el gestor para %2 · %3").arg(r.key, issueId, r.warning), theme::Amber);
                else emit self->toast(tr("%1 creado en el gestor para %2 y asignado a tu usuario").arg(r.key, issueId), theme::Green);
                return;
            }
            if (r.uncertain) {
                emit self->toast(tr("El envío al gestor se cortó sin respuesta: comprueba si %1 se creó antes de publicarlo otra vez · %2")
                                     .arg(issueId, r.error),
                                 theme::Amber);
                return;
            }
            emit self->toast(tr("%1 no se pudo crear en el gestor · %2 · publícalo desde la tarjeta del issue").arg(issueId, r.error),
                             theme::Amber);
        });
    });
}

void IssuesView::createPlan() {
    const Issue* issue = selected();
    if (!issue) return;
    const QString issueId = issue->id;
    const QString planId = m_plans.createPlan(planNameFor(*issue));
    m_issues.linkPlan(issueId, planId);
    emit toast(tr("%1 creado y vinculado a %2: añádele sus casos").arg(planId, issueId), theme::Green);
    emit openPlanRequested(planId);
}

QStringList IssuesView::runnablePlans(const Issue& issue) const {
    QStringList out;
    for (const auto& planId : issue.planIds) {
        const TestPlan* plan = m_plans.find(planId);
        if (plan && !plan->archived && !m_plans.orderedCaseIds(planId).isEmpty()) out << planId;
    }
    return out;
}

QString IssuesView::continuableCycle(const Issue& issue, int revision) const {
    // Los de la ronda vienen del más reciente al primero: el que se continúa es el último que dejó
    // casos rotos, porque es donde se quedaron las pruebas.
    for (const auto& cycle : IssueStore::cyclesOfRevision(issue, m_history, revision))
        if (m_history.report(cycle.id).canContinue()) return cycle.id;
    return {};
}

void IssuesView::runPlan(QWidget* anchor) {
    const Issue* issue = selected();
    if (!issue) return;
    const QStringList runnable = runnablePlans(*issue);
    if (runnable.isEmpty()) {
        emit toast(issue->planIds.isEmpty() ? tr("El issue todavía no tiene plan: créalo antes de probar")
                                            : tr("El plan del issue no tiene casos que ejecutar: ábrelo y añádeselos"),
                   theme::Amber);
        return;
    }
    if (runnable.size() == 1) { emit runPlanRequested(runnable.first()); return; }
    // Con varios planes no se adivina cuál toca: se elige, con los casos que tiene cada uno a la vista.
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    for (const auto& planId : runnable) {
        const TestPlan* plan = m_plans.find(planId);
        menu->addAction(tr("%1 · %2 caso(s)").arg(plan->name).arg(m_plans.orderedCaseIds(planId).size()), this,
                        [this, planId]() { emit runPlanRequested(planId); });
    }
    menu->popup(anchor ? anchor->mapToGlobal(QPoint(0, anchor->height())) : QCursor::pos());
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
    showDetail(false);
}

} // namespace qaflow
