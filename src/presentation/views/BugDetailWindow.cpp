#include "BugDetailWindow.h"

#include "core/models/BugReport.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/Ui.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace qaflow {

namespace {
QString when(const QDateTime& dt) { return dt.isValid() ? dt.toString(QStringLiteral("dd/MM/yyyy HH:mm")) : QStringLiteral("—"); }

QString severityColor(const QString& severity) {
    if (severity == QStringLiteral("Bloqueante") || severity == QStringLiteral("Crítica")) return theme::Red;
    if (severity == QStringLiteral("Mayor")) return theme::Amber;
    return theme::Muted;
}
} // namespace

BugDetailWindow::BugDetailWindow(const IssueLink& bug, const QString& caseTitle, const QString& stepAction, QWidget* parent)
    : QDialog(parent), m_bug(bug), m_caseTitle(caseTitle), m_stepAction(stepAction) {
    setObjectName(QStringLiteral("bugDetailWindow"));
    setWindowIcon(ui::appIcon());
    // Ventana de verdad, no un diálogo modal: se mira el bug mientras se sigue probando.
    setWindowFlag(Qt::Window);
    setModal(false);
    setMinimumWidth(480);

    auto* v = ui::vbox(this, 18, 12);
    m_pills = new QWidget;
    ui::hbox(m_pills, 0, 6);
    v->addWidget(m_pills);

    m_title = new QLabel;
    m_title->setWordWrap(true);
    m_title->setStyleSheet(QStringLiteral("font-size:16px;font-weight:800;"));
    v->addWidget(m_title);

    m_where = ui::label(QString(), "muted-sm");
    m_where->setWordWrap(true);
    v->addWidget(m_where);

    m_status = ui::label(QString(), "muted-sm");
    m_status->setWordWrap(true);
    v->addWidget(m_status);

    m_meta = ui::label(QString(), "muted-sm");
    m_meta->setWordWrap(true);
    v->addWidget(m_meta);
    v->addStretch(1);

    auto* buttons = new QWidget;
    auto* bh = ui::hbox(buttons, 0, 8);
    auto* copy = ui::button(tr("Copiar clave"), "outline");
    connect(copy, &QPushButton::clicked, this, [this]() { QApplication::clipboard()->setText(m_bug.key); });
    bh->addWidget(copy);
    bh->addStretch(1);
    auto* close = ui::button(tr("Cerrar"), "outline");
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    bh->addWidget(close);
    auto* open = ui::button(tr("Abrir en el gestor"), "primary");
    open->setObjectName(QStringLiteral("bugDetailOpen"));
    open->setEnabled(!m_bug.url.trimmed().isEmpty());
    connect(open, &QPushButton::clicked, this, [this]() { emit openUrlRequested(m_bug.url); });
    bh->addWidget(open);
    v->addWidget(buttons);

    refresh();
}

void BugDetailWindow::setBug(const IssueLink& bug) {
    m_bug = bug;
    refresh();
}

void BugDetailWindow::refresh() {
    setWindowTitle(m_bug.key.isEmpty() ? tr("Bug") : tr("Bug %1").arg(m_bug.key));
    ui::clearLayout(m_pills->layout());
    auto* h = static_cast<QHBoxLayout*>(m_pills->layout());
    if (!m_bug.key.isEmpty()) h->addWidget(ui::pill(m_bug.key, theme::tint(theme::Cyan, 30), theme::Cyan));
    const QString classification = BugReport::classificationLabel(m_bug.classification).toUpper();
    if (!classification.isEmpty()) h->addWidget(ui::pill(classification, theme::tint(theme::Blue, 26), theme::Blue));
    if (!m_bug.severity.isEmpty()) {
        const QString color = severityColor(m_bug.severity);
        h->addWidget(ui::pill(BugReport::severityLabel(m_bug.severity).toUpper(), theme::tint(color, 34), color));
    }
    const QString stateColor = m_bug.resolved ? theme::Green : theme::Amber;
    h->addWidget(ui::pill(m_bug.resolved ? tr("CERRADO") : tr("ABIERTO"), theme::tint(stateColor, 34), stateColor));
    h->addStretch(1);

    m_title->setText(m_bug.title.isEmpty() ? tr("(sin título)") : m_bug.title);
    // De dónde salió: es lo que no se puede leer en el gestor.
    QStringList where;
    if (!m_bug.caseId.isEmpty())
        where << (m_caseTitle.isEmpty() ? m_bug.caseId : tr("%1 · %2").arg(m_bug.caseId, m_caseTitle));
    if (m_bug.step > 0)
        where << (m_stepAction.isEmpty() ? tr("paso %1").arg(m_bug.step) : tr("paso %1 · %2").arg(m_bug.step).arg(m_stepAction));
    else if (!m_bug.caseId.isEmpty())
        where << tr("del caso entero");
    m_where->setText(where.join(QStringLiteral(" · ")));

    m_status->setText(m_bug.status.isEmpty()
                          ? tr("Estado en el gestor: sin consultar")
                          : tr("Estado en el gestor: %1 · consultado el %2").arg(m_bug.status, when(m_bug.statusCheckedAt)));
    m_status->setStyleSheet(QStringLiteral("color:%1;").arg(stateColor));

    QStringList meta;
    meta << tr("Reportado el %1").arg(when(m_bug.createdAt));
    if (!m_bug.tracker.isEmpty()) meta << m_bug.tracker;
    if (!m_bug.url.trimmed().isEmpty()) meta << m_bug.url;
    m_meta->setText(meta.join(QStringLiteral(" · ")));
}

} // namespace qaflow
