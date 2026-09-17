#include "RevisionPublishDialog.h"

#include "presentation/theme/Theme.h"
#include "presentation/widgets/TextArea.h"
#include "presentation/widgets/Ui.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QComboBox>
#include <QFileInfo>
#include <QLabel>
#include <QPointer>
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

/// Nombre del destino en la casilla que lo elige.
QString title(RevisionPublishService::Destination destination) {
    switch (destination) {
        case RevisionPublishService::Destination::Zephyr:
            return QCoreApplication::translate("qaflow::RevisionPublishDialog", "Publicar los planes y sus casos en Zephyr");
        case RevisionPublishService::Destination::Tracker:
            return QCoreApplication::translate("qaflow::RevisionPublishDialog", "Dejar el resultado y el acta en el gestor");
        case RevisionPublishService::Destination::Requirement:
            return QCoreApplication::translate("qaflow::RevisionPublishDialog", "Registrar el resultado en GESREQ");
    }
    return {};
}

QString objectNameFor(RevisionPublishService::Destination destination) {
    switch (destination) {
        case RevisionPublishService::Destination::Zephyr: return QStringLiteral("revisionPublishZephyr");
        case RevisionPublishService::Destination::Tracker: return QStringLiteral("revisionPublishTracker");
        case RevisionPublishService::Destination::Requirement: return QStringLiteral("revisionPublishRequirement");
    }
    return {};
}
} // namespace

RevisionPublishDialog::RevisionPublishDialog(RevisionPublishService& service, const QString& issueId, QaOutcome outcome,
                                             const QString& comment, const QString& documentPath, int revision,
                                             QWidget* parent)
    : QDialog(parent), m_service(service), m_issueId(issueId), m_revision(revision), m_documentPath(documentPath) {
    setObjectName(QStringLiteral("revisionPublishDialog"));
    // El número de la ronda va en el título: con un requerimiento observado se publica más de una, y
    // hay que ver cuál se está mandando.
    const QString heading = revision > 0 ? tr("Publicar el resultado de la revisión %1").arg(revision)
                                         : tr("Publicar el resultado de la revisión");
    setWindowTitle(heading);
    setWindowIcon(ui::appIcon());
    setMinimumSize(660, 640);

    auto* v = ui::vbox(this, 18, 10);
    v->addWidget(ui::label(heading, "h2"));
    auto* intro = ui::label(tr("Las pruebas van a Zephyr como ciclos con sus casos; el resultado y el acta, al issue del gestor; "
                               "y el control de calidad queda registrado en el requerimiento de GESREQ."),
                            "muted-sm");
    intro->setWordWrap(true);
    v->addWidget(intro);

    m_outcome = new QComboBox;
    m_outcome->setObjectName(QStringLiteral("revisionPublishOutcome"));
    for (const auto value : {QaOutcome::Conforme, QaOutcome::Observado}) m_outcome->addItem(label(value), static_cast<int>(value));
    m_outcome->setCurrentIndex(std::max(0, m_outcome->findData(static_cast<int>(outcome))));
    v->addWidget(field(tr("Resultado del control de calidad"), m_outcome));

    buildSteps(v);
    // El resultado decide si GESREQ acepta el registro (no admite «OK» con observaciones, ni
    // «OBSERVADO» sin ninguna), así que su paso se revisa cada vez que cambia.
    connect(m_outcome, &QComboBox::currentIndexChanged, this, [this](int) { refreshRequirementStep(); });

    m_comment = new TextArea(10);
    m_comment->setObjectName(QStringLiteral("revisionPublishComment"));
    m_comment->setTextSilently(comment);
    v->addWidget(field(tr("Texto que se envía al gestor y a GESREQ"), m_comment), 1);

    if (!m_documentPath.trimmed().isEmpty()) {
        m_attach = new QCheckBox(tr("Adjuntar el acta %1").arg(QFileInfo(m_documentPath).fileName()));
        m_attach->setObjectName(QStringLiteral("revisionPublishAttach"));
        m_attach->setChecked(true);
        connect(m_attach, &QCheckBox::toggled, this, [this](bool) { refreshRequirementStep(); });
        v->addWidget(m_attach);
    } else {
        auto* missing = ui::label(tr("Todavía no hay acta generada: se publica sólo el texto. Genérala antes si quieres adjuntarla."),
                                  "muted-sm");
        missing->setWordWrap(true);
        v->addWidget(missing);
    }

    auto* note = ui::label(tr("Registrar el resultado cambia el estado del requerimiento en GESREQ y queda a nombre del usuario de "
                              "la conexión. Nada se envía hasta que pulses «Publicar»."),
                           "muted-sm");
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("font-size:11.5px;color:%1;").arg(theme::AmberSoft));
    v->addWidget(note);

    refreshRequirementStep();

    auto* buttons = new QWidget;
    auto* h = ui::hbox(buttons, 0, 8);
    h->addStretch(1);
    m_close = ui::button(tr("Cancelar"), "outline");
    m_publish = ui::button(tr("Publicar"), "primary");
    m_publish->setObjectName(QStringLiteral("revisionPublishAccept"));
    m_publish->setDefault(true);
    connect(m_close, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_publish, &QPushButton::clicked, this, &RevisionPublishDialog::start);
    h->addWidget(m_close);
    h->addWidget(m_publish);
    v->addWidget(buttons);
}

void RevisionPublishDialog::buildSteps(QVBoxLayout* v) {
    m_steps = m_service.stepsFor(m_issueId, m_revision);
    auto* box = new QWidget;
    auto* list = ui::vbox(box, 0, 10);
    for (const auto& step : m_steps) {
        const int key = static_cast<int>(step.destination);
        auto* row = new QWidget;
        auto* rv = ui::vbox(row, 0, 2);
        auto* choice = new QCheckBox(title(step.destination));
        choice->setObjectName(objectNameFor(step.destination));
        choice->setEnabled(step.available);
        // Lo que ya se hizo en esta revisión viene desmarcado: repetirlo es una decisión, no un descuido.
        choice->setChecked(step.available && !step.done);
        m_choices.insert(key, choice);
        rv->addWidget(choice);
        auto* detail = ui::label(step.blocked.isEmpty() ? QStringLiteral("%1 · %2").arg(step.target, step.detail) : step.blocked, "muted-sm");
        detail->setWordWrap(true);
        detail->setContentsMargins(24, 0, 0, 0);
        if (!step.blocked.isEmpty()) detail->setStyleSheet(QStringLiteral("font-size:11.5px;color:%1;").arg(theme::AmberSoft));
        m_details.insert(key, detail);
        m_baseDetails.insert(key, QStringLiteral("%1 · %2").arg(step.target, step.detail));
        rv->addWidget(detail);
        auto* status = ui::label(QString(), "muted-sm");
        status->setWordWrap(true);
        status->setContentsMargins(24, 0, 0, 0);
        status->setVisible(false);
        m_status.insert(key, status);
        rv->addWidget(status);
        list->addWidget(row);
    }
    v->addWidget(field(tr("Qué se publica"), box));
}

QaOutcome RevisionPublishDialog::outcome() const { return static_cast<QaOutcome>(m_outcome->currentData().toInt()); }

QString RevisionPublishDialog::documentPath() const {
    return (m_attach && !m_attach->isChecked()) ? QString() : m_documentPath;
}

void RevisionPublishDialog::refreshRequirementStep() {
    const int key = static_cast<int>(RevisionPublishService::Destination::Requirement);
    auto* choice = m_choices.value(key);
    auto* detail = m_details.value(key);
    if (!choice || !detail) return;
    RevisionPublishService::Step step;
    for (const auto& s : m_steps)
        if (s.destination == RevisionPublishService::Destination::Requirement) step = s;
    // Lo que está bloqueado por la configuración (o ya no hace falta) no depende del resultado.
    if (!step.available && !step.rule) return;

    const QString problem = m_service.requirementProblem(m_issueId, outcome(), documentPath(), m_revision);
    choice->setEnabled(problem.isEmpty());
    if (!problem.isEmpty()) choice->setChecked(false);
    else if (!step.done) choice->setChecked(true);
    detail->setText(problem.isEmpty() ? m_baseDetails.value(key) : problem);
    detail->setStyleSheet(problem.isEmpty() ? QString()
                                            : QStringLiteral("font-size:11.5px;color:%1;").arg(theme::AmberSoft));
}

void RevisionPublishDialog::start() {
    if (m_running) return;
    RevisionPublishService::Options options;
    options.zephyr = m_choices.value(static_cast<int>(RevisionPublishService::Destination::Zephyr))->isChecked();
    options.tracker = m_choices.value(static_cast<int>(RevisionPublishService::Destination::Tracker))->isChecked();
    options.requirement = m_choices.value(static_cast<int>(RevisionPublishService::Destination::Requirement))->isChecked();
    if (!options.zephyr && !options.tracker && !options.requirement) {
        reject();
        return;
    }
    options.outcome = outcome();
    options.comment = m_comment->toPlainText();
    options.documentPath = documentPath();
    options.revision = m_revision;

    m_running = true;
    m_publish->setEnabled(false);
    m_publish->setText(tr("Publicando…"));
    m_outcome->setEnabled(false);
    m_comment->setEnabled(false);
    if (m_attach) m_attach->setEnabled(false);
    for (auto* choice : m_choices) choice->setEnabled(false);

    QPointer<RevisionPublishDialog> self(this);
    m_service.publish(
        m_issueId, options,
        [self](const RevisionPublishService::Outcome& outcome) {
            if (self) self->showOutcome(outcome);
        },
        [self](const RevisionPublishService::Result& result) {
            if (!self) return;
            self->m_running = false;
            self->m_publish->setVisible(false);
            self->m_close->setText(tr("Cerrar"));
            self->m_close->setDefault(true);
            emit self->published(result.ok);
        });
}

void RevisionPublishDialog::showOutcome(const RevisionPublishService::Outcome& outcome) {
    QLabel* status = m_status.value(static_cast<int>(outcome.destination));
    if (!status) return;
    const QString color = outcome.ok ? theme::Green : (outcome.uncertain ? theme::AmberSoft : theme::Red);
    status->setText((outcome.ok ? QStringLiteral("✓ ") : QStringLiteral("⚠ ")) + outcome.message);
    status->setStyleSheet(QStringLiteral("font-size:11.5px;color:%1;").arg(color));
    status->setVisible(true);
}

} // namespace qaflow
