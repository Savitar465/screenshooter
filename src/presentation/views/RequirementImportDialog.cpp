#include "RequirementImportDialog.h"

#include "presentation/widgets/Ui.h"

#include <QListWidget>
#include <QPushButton>

namespace qaflow {

RequirementImportDialog::RequirementImportDialog(const QString& system, const QList<IssueStore::ImportCandidate>& candidates,
                                                 const QList<ExternalRequirement>& others, int missing,
                                                 std::function<QString(const QString&)> projectForSystem, QWidget* parent)
    : QDialog(parent), m_candidates(candidates), m_others(others), m_projectForSystem(std::move(projectForSystem)) {
    using Kind = IssueStore::ImportCandidate::Kind;
    setObjectName(QStringLiteral("requirementImportDialog"));
    setWindowTitle(tr("Importar requerimientos de GESREQ"));
    setWindowIcon(ui::appIcon());
    setMinimumSize(640, 460);

    auto* v = ui::vbox(this, 18, 10);
    v->addWidget(ui::label(tr("Requerimientos de %1 en tu bandeja").arg(system), "h2"));

    int fresh = 0, changed = 0, unchanged = 0;
    for (const auto& c : m_candidates) {
        if (c.kind == Kind::New) ++fresh;
        else if (c.kind == Kind::Changed) ++changed;
        else ++unchanged;
    }
    QStringList parts{tr("%1 nuevos").arg(fresh), tr("%1 con cambios").arg(changed), tr("%1 ya importados").arg(unchanged)};
    if (!m_others.isEmpty()) parts << tr("%1 de otros sistemas, que no se importan aquí").arg(m_others.size());
    if (missing > 0) parts << tr("%1 importados ya no están en la bandeja (se conservan)").arg(missing);
    auto* summary = ui::label(parts.join(QStringLiteral(" · ")), "muted-sm");
    summary->setObjectName(QStringLiteral("importSummary"));
    summary->setWordWrap(true);
    v->addWidget(summary);

    m_list = new QListWidget;
    m_list->setObjectName(QStringLiteral("importList"));
    m_list->setWordWrap(true);
    m_list->setSpacing(3);
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
        const QString text = QStringLiteral("%1 · %2\n%3%4 · %5").arg(r.id, r.summary.simplified(), r.states.join(QStringLiteral(" + ")), dates, status);
        auto* item = new QListWidgetItem(text, m_list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(c.kind == Kind::Unchanged ? Qt::Unchecked : Qt::Checked);
        item->setData(Qt::UserRole, i);
        item->setToolTip(text);
    }
    v->addWidget(m_list, 1);

    auto* note = ui::label(tr("Importar un requerimiento que ya tiene issue sólo actualiza lo que viene de GESREQ: el título, las notas, "
                              "el estado, la prioridad y los casos y planes del issue no cambian."), "muted-sm");
    note->setWordWrap(true);
    v->addWidget(note);

    if (!m_others.isEmpty()) {
        v->addWidget(ui::label(tr("EN OTROS SISTEMAS DE TU BANDEJA"), "eyebrow"));
        m_otherList = new QListWidget;
        m_otherList->setObjectName(QStringLiteral("importOtherList"));
        m_otherList->setWordWrap(true);
        m_otherList->setSpacing(3);
        m_otherList->setMaximumHeight(130);
        for (int i = 0; i < m_others.size(); ++i) {
            const ExternalRequirement& r = m_others[i];
            const QString project = m_projectForSystem ? m_projectForSystem(r.systemCode) : QString();
            const QString destination = project.isEmpty() ? tr("ningún proyecto trabaja %1").arg(r.systemCode)
                                                          : tr("se prueba en %1").arg(project);
            const QString text = QStringLiteral("%1 · %2\n%3 · %4").arg(r.id, r.summary.simplified(), r.system, destination);
            auto* item = new QListWidgetItem(text, m_otherList);
            item->setData(Qt::UserRole, i);
            item->setToolTip(text);
        }
        v->addWidget(m_otherList);
    }

    auto* buttons = new QWidget;
    auto* bh = ui::hbox(buttons, 0, 8);
    m_start = ui::button(tr("Iniciar pruebas"), "outline");
    m_start->setObjectName(QStringLiteral("importStartTesting"));
    m_start->setToolTip(tr("Abre el issue del requerimiento elegido en el proyecto que trabaja su sistema"));
    bh->addWidget(m_start);
    bh->addStretch(1);
    auto* cancel = ui::button(tr("Cancelar"), "outline");
    m_import = ui::button(tr("Importar"), "primary");
    m_import->setObjectName(QStringLiteral("importAccept"));
    m_import->setDefault(true);
    bh->addWidget(cancel);
    bh->addWidget(m_import);
    v->addWidget(buttons);

    connect(m_list, &QListWidget::itemChanged, this, &RequirementImportDialog::refreshButtons);
    connect(m_list, &QListWidget::currentItemChanged, this, [this]() { selectIn(m_list); });
    if (m_otherList) connect(m_otherList, &QListWidget::currentItemChanged, this, [this]() { selectIn(m_otherList); });
    connect(m_start, &QPushButton::clicked, this, [this]() {
        if (!m_current) return;
        emit startTestingRequested(*m_current);
        // Iniciar las pruebas no importa el resto de la bandeja: el diálogo se cierra sin importar nada.
        done(QDialog::Rejected);
    });
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_import, &QPushButton::clicked, this, &RequirementImportDialog::accept);
    refreshButtons();
}

void RequirementImportDialog::selectIn(QListWidget* list) {
    if (m_selecting) return;
    m_selecting = true;
    QListWidget* other = list == m_list ? m_otherList : m_list;
    if (other) other->setCurrentItem(nullptr);
    m_selecting = false;
    const QListWidgetItem* item = list->currentItem();
    if (!item) m_current.reset();
    else if (list == m_list) m_current = m_candidates[item->data(Qt::UserRole).toInt()].requirement;
    else m_current = m_others[item->data(Qt::UserRole).toInt()];
    refreshButtons();
}

QList<ExternalRequirement> RequirementImportDialog::selected() const {
    QList<ExternalRequirement> out;
    for (int i = 0; i < m_list->count(); ++i) {
        const QListWidgetItem* item = m_list->item(i);
        if (item->checkState() == Qt::Checked) out << m_candidates[item->data(Qt::UserRole).toInt()].requirement;
    }
    return out;
}

void RequirementImportDialog::refreshButtons() {
    const qsizetype n = selected().size();
    m_import->setText(n > 0 ? tr("Importar %1").arg(n) : tr("Importar"));
    m_import->setEnabled(n > 0);
    const QString project = m_current && m_projectForSystem ? m_projectForSystem(m_current->systemCode) : QString();
    // Que ningún proyecto trabaje ese sistema no impide empezar: antes de abrir el issue se elige o se crea.
    m_start->setText(!m_current ? tr("Iniciar pruebas")
                                : project.isEmpty() ? tr("Crear proyecto e iniciar") : tr("Iniciar pruebas en %1").arg(project));
    m_start->setEnabled(m_current.has_value());
    m_start->setToolTip(m_current && project.isEmpty()
                            ? tr("Ningún proyecto trabaja ese sistema: antes de empezar se elige uno o se crea")
                            : tr("Abre el issue del requerimiento elegido en el proyecto que trabaja su sistema"));
}

void RequirementImportDialog::accept() {
    const QList<ExternalRequirement> requirements = selected();
    if (requirements.isEmpty()) return;
    emit importRequested(requirements);
    QDialog::accept();
}

} // namespace qaflow
