#include "CycleStartDialog.h"

#include "core/models/BugReport.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/Ui.h"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

namespace qaflow {

CycleStartDialog::CycleStartDialog(const QString& planName, const QString& context, const QString& environment,
                                   const QString& continuation, QWidget* parent)
    : QDialog(parent) {
    const bool continuing = !continuation.trimmed().isEmpty();
    setObjectName(QStringLiteral("cycleStartDialog"));
    setWindowTitle(continuing ? tr("Continuar ciclo") : tr("Arrancar ciclo"));
    setWindowIcon(ui::appIcon());
    setMinimumWidth(460);

    auto* v = ui::vbox(this, 18, 12);
    v->addWidget(ui::label(planName.trimmed().isEmpty() ? tr("Arrancar el ciclo") : planName.trimmed(), "h2"));
    auto* subtitle = ui::label(context.trimmed().isEmpty()
                                   ? tr("Este ciclo no prueba ningún requerimiento: es una ejecución suelta del plan.")
                                   : tr("El ciclo quedará anotado en %1.").arg(context.trimmed()),
                               "muted-sm");
    subtitle->setWordWrap(true);
    v->addWidget(subtitle);

    // Continuar no repite el plan entero: conviene ver qué se va a ejecutar antes de arrancar.
    if (continuing) {
        auto* note = ui::label(continuation.trimmed(), "muted-sm");
        note->setObjectName(QStringLiteral("cycleStartContinuation"));
        note->setWordWrap(true);
        note->setStyleSheet(QStringLiteral("color:%1;").arg(theme::Amber));
        v->addWidget(note);
    }

    auto* field = new QWidget;
    auto* fv = ui::vbox(field, 0, 6);
    fv->addWidget(ui::label(tr("AMBIENTE"), "eyebrow"));
    m_environment = new QComboBox;
    m_environment->setObjectName(QStringLiteral("cycleStartEnvironment"));
    m_environment->setEditable(true);   // los ambientes de cada organización no son sólo estos tres
    m_environment->addItems(BugReport::environments());
    m_environment->setCurrentText(environment.trimmed());
    fv->addWidget(m_environment);
    v->addWidget(field);

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
    accept->setDefault(true);
    bh->addWidget(cancel);
    bh->addWidget(accept);
    v->addWidget(buttons);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(accept, &QPushButton::clicked, this, &QDialog::accept);

    m_environment->setFocus();
    if (auto* edit = m_environment->lineEdit()) edit->selectAll();
}

QString CycleStartDialog::environment() const { return m_environment->currentText().trimmed(); }

} // namespace qaflow
