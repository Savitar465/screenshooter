#include "JiraPublishDialog.h"

#include "presentation/theme/Theme.h"
#include "presentation/widgets/TextArea.h"
#include "presentation/widgets/Ui.h"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
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

JiraPublishDialog::JiraPublishDialog(Mode mode, const QString& destination, const IssueDraft& draft, const QStringList& issueTypes,
                                     QWidget* parent)
    : QDialog(parent), m_mode(mode), m_draft(draft) {
    setObjectName(QStringLiteral("jiraPublishDialog"));
    setWindowTitle(mode == Mode::Create ? tr("Publicar en el gestor") : tr("Actualizar en el gestor"));
    setWindowIcon(ui::appIcon());
    setMinimumSize(640, 520);

    auto* v = ui::vbox(this, 18, 10);
    v->addWidget(ui::label(mode == Mode::Create ? tr("Crear el issue en %1").arg(destination) : tr("Actualizar el issue en %1").arg(destination), "h2"));

    m_summary = new QLineEdit(draft.summary);
    m_summary->setObjectName(QStringLiteral("jiraSummary"));
    connect(m_summary, &QLineEdit::textChanged, this, [this](const QString& t) { ui::setFlag(m_summary, "invalid", t.trimmed().isEmpty()); });
    v->addWidget(field(tr("Título"), m_summary));

    m_issueType = new QComboBox;
    m_issueType->setObjectName(QStringLiteral("jiraIssueType"));
    m_issueType->setEditable(true);
    m_issueType->setInsertPolicy(QComboBox::NoInsert);
    m_issueType->addItems(issueTypes);
    m_issueType->setCurrentText(draft.issueType);
    m_issueType->lineEdit()->setPlaceholderText(QStringLiteral("Tarea"));
    auto* typeField = field(tr("Tipo de incidencia"), m_issueType);
    // El tipo sólo se elige al crear: cambiarlo después es cosa del gestor.
    typeField->setVisible(mode == Mode::Create);
    v->addWidget(typeField);

    m_description = new TextArea(12);
    m_description->setObjectName(QStringLiteral("jiraDescription"));
    m_description->setTextSilently(draft.description);
    v->addWidget(field(tr("Descripción"), m_description), 1);

    auto* note = ui::label(mode == Mode::Create
                               ? tr("Se crea con las etiquetas %1, con las que se encuentra en el gestor.").arg(draft.labels.join(QStringLiteral(", ")))
                               : tr("Se reescriben el título y la descripción del issue en el gestor; lo que alguien haya editado allí se pierde. "
                                    "El resto de campos (tipo, estado, asignación…) no se toca."),
                           "muted-sm");
    note->setWordWrap(true);
    if (mode == Mode::Update) note->setStyleSheet(QStringLiteral("font-size:11.5px;color:%1;").arg(theme::AmberSoft));
    v->addWidget(note);

    auto* buttons = new QWidget;
    auto* h = ui::hbox(buttons, 0, 8);
    h->addStretch(1);
    auto* cancel = ui::button(tr("Cancelar"), "outline");
    auto* accept = ui::button(mode == Mode::Create ? tr("Crear en el gestor") : tr("Actualizar en el gestor"), "primary");
    accept->setObjectName(QStringLiteral("jiraPublishAccept"));
    accept->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(accept, &QPushButton::clicked, this, &JiraPublishDialog::accept);
    h->addWidget(cancel);
    h->addWidget(accept);
    v->addWidget(buttons);
}

IssueDraft JiraPublishDialog::draft() const {
    IssueDraft draft = m_draft;
    draft.summary = m_summary->text().trimmed();
    draft.description = m_description->toPlainText();
    if (m_mode == Mode::Create) draft.issueType = m_issueType->currentText().trimmed();
    return draft;
}

void JiraPublishDialog::accept() {
    if (m_summary->text().trimmed().isEmpty()) {
        ui::setFlag(m_summary, "invalid", true);
        m_summary->setFocus();
        return;
    }
    emit confirmed(draft());
    QDialog::accept();
}

} // namespace qaflow
