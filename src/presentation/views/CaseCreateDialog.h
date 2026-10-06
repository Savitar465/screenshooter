#pragma once

#include "core/models/TestCase.h"

#include <QDialog>
#include <QList>
#include <QStringList>

class QComboBox;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPushButton;

namespace qaflow {

class TextArea;

/// Un caso de prueba nuevo, escrito sin salir de donde se está: su título, su suite, su prioridad, sus
/// precondiciones y sus pasos (acción, datos de la prueba y resultado esperado), los tres campos del paso
/// de Zephyr.
///
/// Es sólo el formulario: quien lo abre recoge el caso con `testCase()` al aceptar y decide dónde va (el
/// catálogo, el plan del ciclo en curso…). No se acepta sin título ni sin al menos un paso con acción:
/// un caso así no se puede ejecutar.
class CaseCreateDialog : public QDialog {
    Q_OBJECT
public:
    /// `suites` son las del proyecto, para elegir una (o escribir otra); `context` dice a dónde irá el caso
    /// («Se añade al plan Regresión y entra al ciclo en curso»).
    CaseCreateDialog(const QStringList& suites, const QString& context, QWidget* parent = nullptr);

    /// El caso tal y como se escribió, sin los pasos vacíos. Sin id: lo pone el catálogo al añadirlo.
    TestCase testCase() const;

private:
    struct StepFields {
        TextArea* action = nullptr;
        TextArea* data = nullptr;
        TextArea* expected = nullptr;
    };
    void addStep();
    void removeStep(int index);
    /// Rehace la rejilla de pasos con lo que tiene cada uno: así se numeran de nuevo al quitar uno.
    void rebuildSteps(const QList<TestStep>& steps);
    QList<TestStep> currentSteps(bool skipEmpty) const;
    void validate();

    QLineEdit* m_title;
    QComboBox* m_suite;
    QComboBox* m_priority;
    TextArea* m_preconditions;
    QGridLayout* m_stepsGrid;
    QLabel* m_stepsHeader;
    QList<StepFields> m_steps;
    QLabel* m_problem;
    QPushButton* m_accept;
};

} // namespace qaflow
