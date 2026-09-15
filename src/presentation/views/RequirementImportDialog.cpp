#include "RequirementImportDialog.h"

#include "presentation/widgets/Ui.h"

#include <QListWidget>
#include <QPushButton>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>

namespace qaflow {

namespace {
constexpr int HtmlRole = Qt::UserRole + 1;
constexpr int CurrentProjectRole = Qt::UserRole + 2;

class RequirementDelegate : public QStyledItemDelegate {
public:
    explicit RequirementDelegate(QListWidget* list) : QStyledItemDelegate(list), m_list(list) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QWidget* widget = opt.widget;
        QStyle* style = widget->style();
        const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, widget).adjusted(10, 8, -10, -8);
        opt.text.clear();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);
        QTextDocument doc;
        doc.setDefaultFont(opt.font);
        doc.setHtml(index.data(HtmlRole).toString());
        doc.setTextWidth(textRect.width());
        QAbstractTextDocumentLayout::PaintContext context;
        context.palette = opt.palette;
        if (opt.state & QStyle::State_Selected)
            context.palette.setColor(QPalette::Text, opt.palette.color(QPalette::HighlightedText));
        painter->save();
        painter->setClipRect(textRect);
        painter->translate(textRect.topLeft());
        doc.documentLayout()->draw(painter, context);
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QTextDocument doc;
        doc.setDefaultFont(option.font);
        doc.setHtml(index.data(HtmlRole).toString());
        // Reserve room for row padding, also when the dialog is resized.
        doc.setTextWidth(qMax(100, m_list->viewport()->width() - 32));
        return QSize(0, qCeil(doc.size().height()) + 20);
    }

private:
    QListWidget* m_list;
};
} // namespace


RequirementImportDialog::RequirementImportDialog(const QString& system, const QList<IssueStore::ImportCandidate>& candidates,
                                                 const QList<ExternalRequirement>& others, int missing,
                                                 std::function<QString(const QString&)> projectForSystem, QWidget* parent)
    : QDialog(parent), m_candidates(candidates), m_others(others), m_projectForSystem(std::move(projectForSystem)) {
    using Kind = IssueStore::ImportCandidate::Kind;
    setObjectName(QStringLiteral("requirementImportDialog"));
    setWindowTitle(tr("Importar requerimientos de GESREQ"));
    setWindowIcon(ui::appIcon());
    setMinimumSize(720, 520);
    resize(900, 660);

    auto* v = ui::vbox(this, 18, 10);
    v->addWidget(ui::label(tr("Requerimientos asignados a ti"), "h2"));

    int fresh = 0, changed = 0, unchanged = 0;
    for (const auto& c : m_candidates) {
        if (c.kind == Kind::New) ++fresh;
        else if (c.kind == Kind::Changed) ++changed;
        else ++unchanged;
    }
    QStringList parts{tr("%1 requerimientos · %2 del proyecto actual (%3)").arg(m_candidates.size() + m_others.size()).arg(m_candidates.size()).arg(system), tr("%1 nuevos").arg(fresh), tr("%1 con cambios").arg(changed), tr("%1 ya importados").arg(unchanged)};
    if (!m_others.isEmpty()) parts << tr("%1 de otros sistemas, que no se importan aquí").arg(m_others.size());
    if (missing > 0) parts << tr("%1 importados ya no están en la bandeja (se conservan)").arg(missing);
    auto* summary = ui::label(parts.join(QStringLiteral(" · ")), "muted-sm");
    summary->setObjectName(QStringLiteral("importSummary"));
    summary->setWordWrap(true);
    v->addWidget(summary);

    m_list = new QListWidget;
    m_list->setObjectName(QStringLiteral("importList"));
    m_list->setWordWrap(true);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setSpacing(5);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setItemDelegate(new RequirementDelegate(m_list));
    const auto addRow = [this](const ExternalRequirement& r, const QString& detail, int index, bool currentProject) {
        const QString state = r.states.isEmpty() ? tr("Sin estado") : r.states.join(QStringLiteral(" + "));
        const QString systemName = r.system.isEmpty() ? r.systemCode : r.system;
        const QString marker = currentProject ? tr("Proyecto actual") : tr("Otro proyecto / sistema");
        const QString text = tr("N.º %1 · %2\n%3\nEstado: %4\n%5 · %6")
                                 .arg(r.id, marker, r.summary.simplified(), state, systemName, detail);
        auto* item = new QListWidgetItem(text, m_list);
        item->setData(Qt::UserRole, index);
        item->setData(CurrentProjectRole, currentProject);
        item->setData(HtmlRole, QStringLiteral(
            "<div><span style='font-size:17px; font-weight:700'>%1</span> &nbsp; "
            "<span style='background-color:%2; color:%3; font-weight:600'>&nbsp;%4&nbsp;</span></div>"
            "<div>%5</div><div><b>%6</b></div><div style='font-size:11px'>%7 · %8</div>")
            .arg(tr("N.º %1").arg(r.id).toHtmlEscaped(),
                 currentProject ? QStringLiteral("#dbeafe") : QStringLiteral("#e5e7eb"),
                 currentProject ? QStringLiteral("#1e40af") : QStringLiteral("#374151"),
                 marker.toHtmlEscaped(), r.summary.simplified().toHtmlEscaped(),
                 tr("Estado: %1").arg(state).toHtmlEscaped(), systemName.toHtmlEscaped(), detail.toHtmlEscaped()));
        item->setToolTip(text);
        item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
    };
    for (int i = 0; i < m_candidates.size(); ++i) {
        const auto& c = m_candidates[i];
        const ExternalRequirement& r = c.requirement;
        QString status;
        switch (c.kind) {
            case Kind::New:
                status = tr("nuevo");
                break;
            case Kind::Changed: {
                QStringList fields;
                for (const auto& change : c.changes) fields << requirementFieldLabel(change.field);
                status = tr("%1 · cambió: %2").arg(c.issueId, fields.join(QStringLiteral(", ")));
                break;
            }
            case Kind::Unchanged:
                status = tr("%1 · sin cambios").arg(c.issueId);
                break;
        }
        const QString dates = r.assignedFrom.isValid()
                                  ? tr(" · asignado %1 – %2").arg(r.assignedFrom.toString(QStringLiteral("dd/MM/yyyy")), r.assignedUntil.toString(QStringLiteral("dd/MM/yyyy")))
                                  : QString();
        addRow(r, status + dates, i, true);
    }
    for (int i = 0; i < m_others.size(); ++i) {
        const auto& r = m_others[i];
        const QString project = m_projectForSystem ? m_projectForSystem(r.systemCode) : QString();
        const QString destination = project.isEmpty() ? tr("ningún proyecto trabaja %1").arg(r.systemCode)
                                                      : tr("se prueba en %1").arg(project);
        addRow(r, destination, i, false);
    }
    v->addWidget(m_list, 1);

    auto* note = ui::label(tr("Selecciona un requerimiento para importarlo e iniciar sus pruebas en el proyecto correspondiente. "
                              "Importar un requerimiento que ya tiene issue sólo actualiza lo que viene de GESREQ: el título, las notas, "
                              "el estado, la prioridad y los casos y planes del issue no cambian."), "muted-sm");
    note->setWordWrap(true);
    v->addWidget(note);

    auto* buttons = new QWidget;
    auto* bh = ui::hbox(buttons, 0, 8);
    bh->addStretch(1);
    auto* cancel = ui::button(tr("Cancelar"), "outline");
    m_start = ui::button(tr("Importar e iniciar pruebas"), "primary");
    m_start->setObjectName(QStringLiteral("importStartTesting"));
    m_start->setDefault(true);
    bh->addWidget(cancel);
    bh->addWidget(m_start);
    v->addWidget(buttons);

    connect(m_list, &QListWidget::currentItemChanged, this, [this]() { selectCurrent(); });
    connect(m_start, &QPushButton::clicked, this, &RequirementImportDialog::accept);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    refreshButtons();
}

void RequirementImportDialog::selectCurrent() {
    const QListWidgetItem* item = m_list->currentItem();
    if (!item) m_current.reset();
    else if (item->data(CurrentProjectRole).toBool()) m_current = m_candidates[item->data(Qt::UserRole).toInt()].requirement;
    else m_current = m_others[item->data(Qt::UserRole).toInt()];
    refreshButtons();
}

void RequirementImportDialog::refreshButtons() {
    const QString project = m_current && m_projectForSystem ? m_projectForSystem(m_current->systemCode) : QString();
    // Que ningún proyecto trabaje ese sistema no impide empezar: antes de abrir el issue se elige o se crea.
    m_start->setText(!m_current ? tr("Importar e iniciar pruebas")
                                : project.isEmpty() ? tr("Crear proyecto, importar e iniciar") : tr("Importar e iniciar pruebas en %1").arg(project));
    m_start->setEnabled(m_current.has_value());
    m_start->setToolTip(m_current && project.isEmpty()
                            ? tr("Ningún proyecto trabaja ese sistema: antes de empezar se elige uno o se crea")
                            : tr("Importa o actualiza el requerimiento elegido y abre su issue en el proyecto que trabaja su sistema"));
}

void RequirementImportDialog::accept() {
    if (!m_current) return;
    emit startTestingRequested(*m_current);
    QDialog::accept();
}

} // namespace qaflow
