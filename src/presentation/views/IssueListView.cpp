#include "IssueListView.h"

#include "application/AppContext.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/Ui.h"

#include <QHeaderView>
#include <QResizeEvent>
#include <QShortcut>
#include <QTableWidget>

#include <algorithm>

namespace qaflow {

namespace {
enum Col { ColIssue, ColRequirement, ColTitle, ColProject, ColState, ColRevision, ColTracker, ColUpdated, ColFinished, ColCount };
constexpr int kProjectRole = Qt::UserRole + 1;
constexpr int kIssueRole = Qt::UserRole + 2;
constexpr int kMinTitleWidth = 260;

/// Celda que se ordena por lo que guarda en `Qt::UserRole` (una fecha, un número) y no por su texto.
class SortItem : public QTableWidgetItem {
public:
    using QTableWidgetItem::QTableWidgetItem;
    bool operator<(const QTableWidgetItem& other) const override {
        const QVariant a = data(Qt::UserRole), b = other.data(Qt::UserRole);
        if (a.isValid() && b.isValid()) return QVariant::compare(a, b) == QPartialOrdering::Less;
        return QTableWidgetItem::operator<(other);
    }
};

QString when(const QDateTime& dt) { return dt.isValid() ? dt.toString(QStringLiteral("dd/MM/yyyy HH:mm")) : QStringLiteral("—"); }

QString stateColor(IssueState s) {
    switch (s) {
        case IssueState::Pending: return theme::Muted;
        case IssueState::Preparing: return theme::Violet;
        case IssueState::Testing: return theme::Blue;
        case IssueState::Done: return theme::Green;
    }
    return theme::Muted;
}
} // namespace

IssueListView::IssueListView(const AppContext& ctx, QWidget* parent)
    : QWidget(parent), m_issues(*ctx.issues), m_directory(ctx.issueDirectory), m_projects(ctx.projects), m_projectId(ctx.projectId) {
    auto* root = ui::vbox(this, 0, 0);
    m_table = new QTableWidget(0, ColCount);
    m_table->setObjectName(QStringLiteral("issueListTable"));
    m_table->setHorizontalHeaderLabels({tr("Issue"), tr("GREQ"), tr("Título"), tr("Proyecto"), tr("Estado de QA"), tr("Revisión"),
                                        tr("Gestor"), tr("Actualizado"), tr("Finalizado")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setShowGrid(false);
    m_table->setAlternatingRowColors(true);
    m_table->setWordWrap(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(34);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColTitle, QHeaderView::Interactive);
    m_table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(ColUpdated, Qt::DescendingOrder);
    m_table->setToolTip(tr("Un clic elige el issue · doble clic (o Intro) lo abre"));
    auto entryAt = [this](int row) -> std::pair<QString, QString> {
        const QTableWidgetItem* item = m_table->item(row, ColIssue);
        return item ? std::pair{item->data(kProjectRole).toString(), item->data(kIssueRole).toString()} : std::pair<QString, QString>{};
    };
    connect(m_table, &QTableWidget::currentCellChanged, this, [this, entryAt](int row) {
        if (m_filling || row < 0) return;
        const auto [project, issue] = entryAt(row);
        m_selectedProject = project;
        m_selectedIssue = issue;
        emit issueSelected(project, issue);
    });
    auto open = [this, entryAt](int row) {
        const auto [project, issue] = entryAt(row);
        if (!issue.isEmpty()) emit openIssueRequested(project, issue);
    };
    // Doble clic o Intro. No `cellActivated`: en la mayoría de estilos también llega con el doble clic, y
    // abrir dos veces pedía dos confirmaciones de cambio de proyecto.
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [open](int row, int) { open(row); });
    for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
        auto* enter = new QShortcut(QKeySequence(key), m_table);
        enter->setContext(Qt::WidgetShortcut);
        connect(enter, &QShortcut::activated, this, [this, open]() { if (m_table->currentRow() >= 0) open(m_table->currentRow()); });
    }
    root->addWidget(m_table);
}

void IssueListView::setFilter(const IssueFilter& filter, bool allProjects) {
    m_filter = filter;
    m_allProjects = allProjects;
    refresh();
}

void IssueListView::setSelected(const QString& projectId, const QString& issueId) {
    m_selectedProject = projectId;
    m_selectedIssue = issueId;
    m_filling = true;
    m_table->clearSelection();
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const QTableWidgetItem* item = m_table->item(row, ColIssue);
        if (item && item->data(kProjectRole).toString() == projectId && item->data(kIssueRole).toString() == issueId) {
            m_table->setCurrentCell(row, ColIssue);
            break;
        }
    }
    m_filling = false;
}

void IssueListView::sortByFinished() { m_table->sortByColumn(ColFinished, Qt::DescendingOrder); }

void IssueListView::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    fitTitle();
}

void IssueListView::fitTitle() {
    const QHeaderView* header = m_table->horizontalHeader();
    int others = 0;
    for (int col = 0; col < ColCount; ++col)
        if (col != ColTitle) others += header->sectionSize(col);
    m_table->setColumnWidth(ColTitle, std::max(kMinTitleWidth, m_table->viewport()->width() - others));
}

int IssueListView::shownCount() const { return m_table->rowCount(); }

QString IssueListView::projectName(const QString& projectId) const {
    const Project* project = m_projects ? m_projects->find(projectId) : nullptr;
    return project ? project->name : projectId;
}

void IssueListView::refresh() {
    QList<IssueDirectory::Entry> all;
    for (const auto& issue : m_issues.issues()) all << IssueDirectory::Entry{m_projectId, issue};
    if (m_directory && m_allProjects) all << m_directory->issues(m_projectId);

    // Ordenar mientras se rellena movería las filas a medias: se rellena sin orden y se reordena al final.
    const int sortColumn = m_table->horizontalHeader()->sortIndicatorSection();
    const Qt::SortOrder sortOrder = m_table->horizontalHeader()->sortIndicatorOrder();
    m_filling = true;
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    int row = 0;
    for (const auto& entry : all) {
        const Issue& issue = entry.issue;
        if (!m_filter.matches(issue)) continue;
        const bool own = entry.projectId == m_projectId;
        m_table->insertRow(row);
        auto set = [&](int col, const QString& text, const QVariant& sortKey = QVariant()) {
            auto* item = new SortItem(text);
            if (sortKey.isValid()) item->setData(Qt::UserRole, sortKey);
            item->setToolTip(text);
            m_table->setItem(row, col, item);
            return item;
        };
        QTableWidgetItem* id = set(ColIssue, issue.id);
        id->setData(kProjectRole, entry.projectId);
        id->setData(kIssueRole, issue.id);
        QFont mono = id->font();
        mono.setFamily(QStringLiteral("monospace"));
        id->setFont(mono);
        set(ColRequirement, issue.isImported() ? issue.requirement.data.id : QStringLiteral("—"));
        QTableWidgetItem* title = set(ColTitle, issue.title);
        // Los del proyecto abierto resaltan, como en el tablero.
        QTableWidgetItem* projectItem = set(ColProject, projectName(entry.projectId));
        if (own) {
            QFont bold = title->font();
            bold.setBold(true);
            title->setFont(bold);
            projectItem->setFont(bold);
        } else {
            projectItem->setForeground(QColor(theme::Violet));
            title->setForeground(QColor(theme::Muted));
        }
        QTableWidgetItem* state = set(ColState, label(issue.state), static_cast<int>(issue.state));
        state->setForeground(QColor(stateColor(issue.state)));
        const int revision = issue.currentRevisionNumber();
        QString revisionText = QStringLiteral("—");
        if (revision > 0) {
            const IssueRevision* current = issue.revision(0);
            revisionText = current && !current->isOpen() ? tr("%1 · %2").arg(revision).arg(label(current->outcome))
                                                         : tr("%1 · en curso").arg(revision);
        }
        set(ColRevision, revisionText, revision);
        set(ColTracker, issue.isPublished() ? issue.publication.key : QStringLiteral("—"));
        set(ColUpdated, when(issue.updatedAt), issue.updatedAt);
        set(ColFinished, when(issue.finishedAt()), issue.finishedAt());
        ++row;
    }
    m_table->setSortingEnabled(true);
    m_table->sortByColumn(sortColumn, sortOrder);
    m_filling = false;
    setSelected(m_selectedProject, m_selectedIssue);
    fitTitle();
}

} // namespace qaflow
