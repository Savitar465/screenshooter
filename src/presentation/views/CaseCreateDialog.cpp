#include "CaseCreateDialog.h"

#include "presentation/theme/Theme.h"
#include "presentation/widgets/TextArea.h"
#include "presentation/widgets/Ui.h"

#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>

#include <algorithm>

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

CaseCreateDialog::CaseCreateDialog(const QStringList& suites, const QString& context, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("caseCreateDialog"));
    setWindowTitle(tr("Nuevo caso de prueba"));
    setModal(true);
    resize(760, 640);

    auto* v = ui::vbox(this, 18, 12);
    if (!context.isEmpty()) {
        auto* intro = ui::label(context, "muted-sm");
        intro->setWordWrap(true);
        v->addWidget(intro);
    }

    m_title = new QLineEdit;
    m_title->setObjectName(QStringLiteral("caseCreateTitle"));
    m_title->setPlaceholderText(tr("Qué se prueba: «Pago con cupón vencido»"));
    connect(m_title, &QLineEdit::textChanged, this, &CaseCreateDialog::validate);
    v->addWidget(field(tr("Título"), m_title));

    auto* meta = new QWidget;
    auto* mh = ui::hbox(meta, 0, 12);
    m_suite = new QComboBox;
    m_suite->setObjectName(QStringLiteral("caseCreateSuite"));
    m_suite->setEditable(true);   // una suite que todavía no existe se escribe
    m_suite->addItems(suites);
    mh->addWidget(field(tr("Suite"), m_suite), 1);
    m_priority = new QComboBox;
    m_priority->setObjectName(QStringLiteral("caseCreatePriority"));
    for (const auto p : {Priority::Alta, Priority::Media, Priority::Baja}) m_priority->addItem(label(p), static_cast<int>(p));
    m_priority->setCurrentIndex(1);
    mh->addWidget(field(tr("Prioridad"), m_priority), 1);
    v->addWidget(meta);

    m_preconditions = new TextArea(2);
    m_preconditions->setObjectName(QStringLiteral("caseCreatePreconditions"));
    m_preconditions->setPlaceholderText(tr("Estado inicial del sistema, datos de prueba, cuenta…"));
    m_preconditions->enableMarkupEditor(tr("Precondiciones"));
    v->addWidget(field(tr("Precondiciones"), m_preconditions));

    // Los pasos, en una rejilla que crece hacia abajo con su propio scroll.
    auto* stepsHead = new QWidget;
    auto* sh = ui::hbox(stepsHead, 0, 8);
    m_stepsHeader = ui::label(QString(), "eyebrow");
    sh->addWidget(m_stepsHeader, 1);
    v->addWidget(stepsHead);
    QWidget* stepsContent;
    QVBoxLayout* stepsLayout;
    auto* scroll = ui::scrollArea(&stepsContent, &stepsLayout);
    stepsLayout->setSpacing(12);
    auto* grid = new QWidget;
    m_stepsGrid = new QGridLayout(grid);
    m_stepsGrid->setContentsMargins(0, 0, 0, 0);
    m_stepsGrid->setHorizontalSpacing(8);
    m_stepsGrid->setVerticalSpacing(8);
    stepsLayout->addWidget(grid);
    auto* add = ui::button(tr("+ Añadir paso"), "dashed");
    add->setObjectName(QStringLiteral("caseCreateAddStep"));
    connect(add, &QPushButton::clicked, this, &CaseCreateDialog::addStep);
    stepsLayout->addWidget(add);
    stepsLayout->addStretch(1);
    v->addWidget(scroll, 1);

    m_problem = ui::label(QString(), "muted-sm");
    m_problem->setObjectName(QStringLiteral("caseCreateProblem"));
    m_problem->setStyleSheet(QStringLiteral("color:%1;").arg(theme::AmberSoft));
    v->addWidget(m_problem);

    auto* actions = new QWidget;
    auto* ah = ui::hbox(actions, 0, 10);
    ah->addStretch(1);
    auto* cancel = ui::button(tr("Cancelar"), "ghost");
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    ah->addWidget(cancel);
    m_accept = ui::button(tr("Crear caso"), "primary");
    m_accept->setObjectName(QStringLiteral("caseCreateAccept"));
    connect(m_accept, &QPushButton::clicked, this, &QDialog::accept);
    ah->addWidget(m_accept);
    v->addWidget(actions);

    rebuildSteps({TestStep{}});
    m_title->setFocus();
}

TestCase CaseCreateDialog::testCase() const {
    TestCase c;
    c.title = m_title->text().trimmed();
    c.suite = m_suite->currentText().trimmed();
    c.priority = static_cast<Priority>(m_priority->currentData().toInt());
    // Se crea para ejecutarlo ya: listo, no en borrador.
    c.status = CaseStatus::Listo;
    c.preconditions = m_preconditions->toPlainText().trimmed();
    c.steps = currentSteps(true);
    return c;
}

void CaseCreateDialog::addStep() {
    QList<TestStep> steps = currentSteps(false);
    steps << TestStep{};
    rebuildSteps(steps);
    m_steps.last().action->setFocus();
}

void CaseCreateDialog::removeStep(int index) {
    QList<TestStep> steps = currentSteps(false);
    if (index < 0 || index >= steps.size()) return;
    steps.removeAt(index);
    if (steps.isEmpty()) steps << TestStep{};   // siempre queda uno en el que escribir
    rebuildSteps(steps);
}

void CaseCreateDialog::rebuildSteps(const QList<TestStep>& steps) {
    ui::clearLayout(m_stepsGrid);
    m_steps.clear();
    m_stepsGrid->addWidget(ui::label(tr("ACCIÓN"), "eyebrow"), 0, 1);
    m_stepsGrid->addWidget(ui::label(tr("DATOS DE LA PRUEBA"), "eyebrow"), 0, 2);
    m_stepsGrid->addWidget(ui::label(tr("RESULTADO ESPERADO"), "eyebrow"), 0, 3);
    for (int i = 0; i < steps.size(); ++i) {
        const int row = i + 1;
        auto* number = ui::label(QString::number(row), "mono-muted");
        number->setAlignment(Qt::AlignTop | Qt::AlignHCenter);
        number->setFixedWidth(20);
        m_stepsGrid->addWidget(number, row, 0, Qt::AlignTop);
        StepFields f;
        f.action = new TextArea(2);
        f.action->setObjectName(QStringLiteral("caseCreateAction-%1").arg(row));
        f.action->setPlaceholderText(tr("Qué se hace…"));
        f.data = new TextArea(2);
        f.data->setObjectName(QStringLiteral("caseCreateData-%1").arg(row));
        f.data->setPlaceholderText(tr("Con qué datos…"));
        f.expected = new TextArea(2);
        f.expected->setObjectName(QStringLiteral("caseCreateExpected-%1").arg(row));
        f.expected->setPlaceholderText(tr("Qué tiene que pasar…"));
        f.action->setTextSilently(steps[i].action);
        f.data->setTextSilently(steps[i].data);
        f.expected->setTextSilently(steps[i].expected);
        connect(f.action, &QPlainTextEdit::textChanged, this, &CaseCreateDialog::validate);
        m_stepsGrid->addWidget(f.action, row, 1);
        m_stepsGrid->addWidget(f.data, row, 2);
        m_stepsGrid->addWidget(f.expected, row, 3);
        auto* remove = ui::button(QStringLiteral("×"), "icon");
        remove->setObjectName(QStringLiteral("caseCreateRemove-%1").arg(row));
        remove->setToolTip(tr("Quitar el paso"));
        connect(remove, &QPushButton::clicked, this, [this, i]() { removeStep(i); });
        m_stepsGrid->addWidget(remove, row, 4, Qt::AlignTop);
        m_steps << f;
    }
    m_stepsGrid->setColumnStretch(1, 3);
    m_stepsGrid->setColumnStretch(2, 2);
    m_stepsGrid->setColumnStretch(3, 3);
    validate();
}

QList<TestStep> CaseCreateDialog::currentSteps(bool skipEmpty) const {
    QList<TestStep> out;
    for (const auto& f : m_steps) {
        TestStep s{f.action->toPlainText().trimmed(), f.data->toPlainText().trimmed(), f.expected->toPlainText().trimmed()};
        if (skipEmpty && s.action.isEmpty() && s.data.isEmpty() && s.expected.isEmpty()) continue;
        out << s;
    }
    return out;
}

void CaseCreateDialog::validate() {
    const QList<TestStep> steps = currentSteps(true);
    m_stepsHeader->setText(tr("PASOS · %1").arg(steps.size()));
    const bool hasAction = std::any_of(steps.cbegin(), steps.cend(), [](const TestStep& s) { return !s.action.isEmpty(); });
    const bool stepWithoutAction = std::any_of(steps.cbegin(), steps.cend(), [](const TestStep& s) { return s.action.isEmpty(); });
    QString problem;
    if (m_title->text().trimmed().isEmpty()) problem = tr("Ponle un título al caso");
    else if (!hasAction) problem = tr("Escribe al menos un paso con su acción");
    else if (stepWithoutAction) problem = tr("Cada paso necesita su acción");
    m_problem->setText(problem);
    m_problem->setVisible(!problem.isEmpty());
    m_accept->setEnabled(problem.isEmpty());
}

} // namespace qaflow
