#include "CaseCreateDialog.h"

#include "presentation/theme/Theme.h"
#include "presentation/views/CaseForm.h"
#include "presentation/widgets/Responsive.h"
#include "presentation/widgets/Ui.h"

#include <QCheckBox>
#include <QLabel>
#include <QPushButton>

#include <algorithm>

namespace qaflow {

CaseCreateDialog::CaseCreateDialog(const QStringList& suites, const QString& context, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("caseCreateDialog"));
    setWindowTitle(tr("Nuevo caso de prueba"));
    setModal(true);
    resize(820, 700);

    // Todo el formulario se desplaza junto (con muchos pasos, o con la interfaz aumentada en una
    // pantalla chica); el aviso y los botones quedan debajo, siempre a la vista.
    QVBoxLayout* footer;
    auto* v = ui::scrollingBody(this, 18, 14, &footer);
    if (!context.isEmpty()) {
        auto* intro = ui::label(context, "muted-sm");
        intro->setObjectName(QStringLiteral("caseCreateContext"));
        intro->setWordWrap(true);
        intro->setStyleSheet(QStringLiteral("background:%1;color:%2;border-radius:8px;padding:8px 10px;")
                                 .arg(theme::tint(theme::Blue, 22), theme::Text));
        v->addWidget(intro);
    }

    // Un caso nuevo siempre empieza Listo; y como está vacío, los campos llevan su caja.
    TestCase blank;
    if (!suites.isEmpty()) blank.suite = suites.first();
    m_form = new CaseForm(blank, suites, CaseForm::Options{false, false});
    connect(m_form, &CaseForm::edited, this, &CaseCreateDialog::validate);
    v->addWidget(m_form);
    v->addStretch(1);
    onBreakpoint(this, 640, [this](bool narrow) { m_form->setNarrow(narrow); });

    auto* actions = new QWidget;
    auto* ah = ui::hbox(actions, 0, 10);
    m_problem = ui::label(QString(), "muted-sm");
    m_problem->setObjectName(QStringLiteral("caseCreateProblem"));
    m_problem->setWordWrap(true);
    ah->addWidget(m_problem, 1);
    m_another = new QCheckBox(tr("Crear otro después"));
    m_another->setObjectName(QStringLiteral("caseCreateAnother"));
    m_another->setToolTip(tr("Al crearlo se abre otro caso nuevo vacío"));
    ah->addWidget(m_another);
    auto* cancel = ui::button(tr("Cancelar"), "ghost");
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    ah->addWidget(cancel);
    m_accept = ui::button(tr("Crear caso"), "primary");
    m_accept->setObjectName(QStringLiteral("caseCreateAccept"));
    connect(m_accept, &QPushButton::clicked, this, &QDialog::accept);
    ah->addWidget(m_accept);
    footer->addWidget(actions);

    validate();
    m_form->focusTitle();
}

TestCase CaseCreateDialog::testCase() const {
    TestCase c = m_form->testCase();
    c.status = CaseStatus::Listo;
    return c;
}

bool CaseCreateDialog::createAnother() const { return m_another->isChecked(); }

void CaseCreateDialog::validate() {
    const TestCase c = m_form->testCase();
    const bool hasAction = std::any_of(c.steps.cbegin(), c.steps.cend(), [](const TestStep& s) { return !s.action.isEmpty(); });
    const bool stepWithoutAction = std::any_of(c.steps.cbegin(), c.steps.cend(), [](const TestStep& s) { return s.action.isEmpty(); });
    QString problem;
    if (c.title.isEmpty()) problem = tr("Ponle un título al caso");
    else if (!hasAction) problem = tr("Escribe al menos un paso con su acción");
    else if (stepWithoutAction) problem = tr("Cada paso necesita su acción");
    if (problem.isEmpty()) {
        m_problem->setText(c.steps.size() == 1 ? tr("✓ Listo para crear · 1 paso") : tr("✓ Listo para crear · %1 pasos").arg(c.steps.size()));
        m_problem->setStyleSheet(QStringLiteral("color:%1;").arg(theme::Muted));
    } else {
        m_problem->setText(problem);
        m_problem->setStyleSheet(QStringLiteral("color:%1;").arg(theme::AmberSoft));
    }
    m_accept->setEnabled(problem.isEmpty());
}

} // namespace qaflow
