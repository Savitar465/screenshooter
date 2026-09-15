#include "RevisionResultDialog.h"

#include "presentation/theme/Theme.h"
#include "presentation/widgets/TextArea.h"
#include "presentation/widgets/Ui.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>

namespace qaflow {

namespace {
QWidget* field(const QString& title, QWidget* w) {
    auto* box = new QWidget;
    auto* v = ui::vbox(box, 0, 6);
    v->addWidget(ui::label(title.toUpper(), "eyebrow"));
    v->addWidget(w);
    return box;
}
} // namespace

RevisionResultDialog::RevisionResultDialog(Target target, const QString& destination, QaOutcome outcome, const QString& comment,
                                           const QString& documentPath, QWidget* parent)
    : QDialog(parent), m_outcome(outcome), m_documentPath(documentPath) {
    const bool toRequirement = target == Target::Requirement;
    setObjectName(QStringLiteral("revisionResultDialog"));
    setWindowTitle(toRequirement ? tr("Registrar el resultado en GESREQ") : tr("Enviar el resultado al gestor"));
    setWindowIcon(ui::appIcon());
    setMinimumSize(620, 520);

    auto* v = ui::vbox(this, 18, 10);
    v->addWidget(ui::label(toRequirement ? tr("Registrar en %1").arg(destination) : tr("Comentar en %1").arg(destination), "h2"));

    if (toRequirement) {
        m_outcomeBox = new QComboBox;
        m_outcomeBox->setObjectName(QStringLiteral("revisionOutcome"));
        for (const auto value : {QaOutcome::Conforme, QaOutcome::Observado})
            m_outcomeBox->addItem(label(value), static_cast<int>(value));
        m_outcomeBox->setCurrentIndex(std::max(0, m_outcomeBox->findData(static_cast<int>(outcome))));
        v->addWidget(field(tr("Resultado"), m_outcomeBox));
    } else {
        v->addWidget(field(tr("Resultado"), ui::label(label(outcome))));
    }

    m_comment = new TextArea(12);
    m_comment->setObjectName(QStringLiteral("revisionComment"));
    m_comment->setTextSilently(comment);
    v->addWidget(field(tr("Texto que se envía"), m_comment), 1);

    if (!m_documentPath.trimmed().isEmpty()) {
        m_attach = new QCheckBox(tr("Adjuntar el acta %1").arg(QFileInfo(m_documentPath).fileName()));
        m_attach->setObjectName(QStringLiteral("revisionAttach"));
        m_attach->setChecked(true);
        v->addWidget(m_attach);
    } else {
        auto* missing = ui::label(tr("Todavía no hay acta generada: se envía sólo el texto."), "muted-sm");
        missing->setWordWrap(true);
        v->addWidget(missing);
    }

    auto* note = ui::label(toRequirement
                               ? tr("Registrar el resultado cambia el estado del requerimiento en GESREQ y queda a nombre del "
                                    "usuario de la conexión. Sólo se hace cuando lo pides aquí.")
                               : tr("Se añade un comentario al issue del gestor; no se toca ningún otro campo."),
                           "muted-sm");
    note->setWordWrap(true);
    if (toRequirement) note->setStyleSheet(QStringLiteral("font-size:11.5px;color:%1;").arg(theme::AmberSoft));
    v->addWidget(note);

    auto* buttons = new QWidget;
    auto* h = ui::hbox(buttons, 0, 8);
    h->addStretch(1);
    auto* cancel = ui::button(tr("Cancelar"), "outline");
    auto* accept = ui::button(toRequirement ? tr("Registrar en GESREQ") : tr("Enviar al gestor"), "primary");
    accept->setObjectName(QStringLiteral("revisionResultAccept"));
    accept->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(accept, &QPushButton::clicked, this, &QDialog::accept);
    h->addWidget(cancel);
    h->addWidget(accept);
    v->addWidget(buttons);
}

QaOutcome RevisionResultDialog::outcome() const {
    if (!m_outcomeBox) return m_outcome;
    return static_cast<QaOutcome>(m_outcomeBox->currentData().toInt());
}

QString RevisionResultDialog::comment() const { return m_comment->toPlainText(); }

QString RevisionResultDialog::documentPath() const {
    if (m_documentPath.trimmed().isEmpty() || (m_attach && !m_attach->isChecked())) return {};
    return m_documentPath;
}

} // namespace qaflow
