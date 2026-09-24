#include "CycleStartDialog.h"

#include "presentation/theme/Theme.h"
#include "presentation/widgets/Ui.h"

#include <QComboBox>

#include <algorithm>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

namespace qaflow {

CycleStartDialog::CycleStartDialog(const Setup& setup, QWidget* parent) : QDialog(parent) {
    const bool continuing = !setup.continuation.trimmed().isEmpty();
    setObjectName(QStringLiteral("cycleStartDialog"));
    setWindowTitle(continuing ? tr("Continuar ciclo") : tr("Arrancar ciclo"));
    setWindowIcon(ui::appIcon());
    setMinimumWidth(460);

    auto* v = ui::vbox(this, 18, 12);
    v->addWidget(ui::label(setup.planName.trimmed().isEmpty() ? tr("Arrancar el ciclo") : setup.planName.trimmed(), "h2"));
    auto* subtitle = ui::label(setup.context.trimmed().isEmpty()
                                   ? tr("Este ciclo no prueba ningún requerimiento: es una ejecución suelta del plan.")
                                   : tr("El ciclo quedará anotado en %1.").arg(setup.context.trimmed()),
                               "muted-sm");
    subtitle->setWordWrap(true);
    v->addWidget(subtitle);

    // Continuar no repite el plan entero: conviene ver qué se va a ejecutar antes de arrancar.
    if (continuing) {
        auto* note = ui::label(setup.continuation.trimmed(), "muted-sm");
        note->setObjectName(QStringLiteral("cycleStartContinuation"));
        note->setWordWrap(true);
        note->setStyleSheet(QStringLiteral("color:%1;").arg(theme::Amber));
        v->addWidget(note);
    }

    auto* field = new QWidget;
    auto* fv = ui::vbox(field, 0, 6);
    fv->addWidget(ui::label(setup.fixedEnvironment || setup.phases ? tr("FASE") : tr("AMBIENTE"), "eyebrow"));
    m_environment = new QComboBox;
    m_environment->setObjectName(QStringLiteral("cycleStartEnvironment"));
    m_blocked = setup.blocked;
    if (setup.fixedEnvironment) {
        m_environment->addItem(setup.environment.trimmed());
        m_environment->setEnabled(false);
    } else if (setup.phases) {
        // Las fases del requerimiento, sin escribir otra: la fase decide la revisión y su ciclo de Zephyr.
        m_environment->addItems(setup.environments);
        m_environment->setCurrentIndex(std::max(0, int(setup.environments.indexOf(setup.environment.trimmed()))));
    } else {
        m_environment->setEditable(true);   // los ambientes de cada organización no son sólo éstos
        m_environment->addItems(setup.environments);
        m_environment->setCurrentText(setup.environment.trimmed());
    }
    fv->addWidget(m_environment);
    v->addWidget(field);

    if (setup.fixedEnvironment || setup.phases) {
        auto* phase = ui::label(setup.fixedEnvironment
                                    ? tr("Continúa en la fase del ciclo que continúa.")
                                    : tr("Se propone la fase que le toca a la revisión; aprobada una fase desde el issue, se pasa a la siguiente."),
                                "muted-sm");
        phase->setObjectName(QStringLiteral("cycleStartPhase"));
        phase->setWordWrap(true);
        v->addWidget(phase);
        m_blockedNote = ui::label(QString(), "muted-sm");
        m_blockedNote->setObjectName(QStringLiteral("cycleStartBlocked"));
        m_blockedNote->setWordWrap(true);
        m_blockedNote->setStyleSheet(QStringLiteral("color:%1;").arg(theme::Amber));
        v->addWidget(m_blockedNote);
    }

    auto* hint = ui::label(tr("Va con el ciclo a Zephyr: en el nombre del ciclo y en su campo «environment», "
                              "para saber dónde se obtuvieron estos resultados."), "muted-sm");
    hint->setWordWrap(true);
    v->addWidget(hint);

    auto* buttons = new QWidget;
    auto* bh = ui::hbox(buttons, 0, 8);
    bh->addStretch(1);
    auto* cancel = ui::button(tr("Cancelar"), "outline");
    auto* accept = ui::button(continuing ? tr("Continuar") : tr("Arrancar"), "primary");
    accept->setObjectName(QStringLiteral("cycleStartAccept"));
    m_accept = accept;
    accept->setDefault(true);
    bh->addWidget(cancel);
    bh->addWidget(accept);
    v->addWidget(buttons);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(accept, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_environment, &QComboBox::currentTextChanged, this, [this](const QString&) { refreshBlocked(); });
    refreshBlocked();
    if (setup.fixedEnvironment || setup.phases) {
        accept->setFocus();
    } else {
        m_environment->setFocus();
        if (auto* edit = m_environment->lineEdit()) edit->selectAll();
    }
}

QString CycleStartDialog::environment() const { return m_environment->currentText().trimmed(); }

void CycleStartDialog::refreshBlocked() {
    const QString reason = m_blocked.value(environment());
    if (m_blockedNote) {
        m_blockedNote->setText(reason);
        m_blockedNote->setVisible(!reason.isEmpty());
    }
    if (m_accept) m_accept->setEnabled(reason.isEmpty());
}

} // namespace qaflow
