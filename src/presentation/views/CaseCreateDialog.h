#pragma once

#include "core/models/TestCase.h"

#include <QDialog>
#include <QStringList>

class QCheckBox;
class QLabel;
class QPushButton;

namespace qaflow {

class CaseForm;

/// Un caso de prueba nuevo, escrito sin salir de donde se está, con el mismo formulario que la
/// pantalla Casos (`CaseForm`): título, suite, prioridad, componente, etiquetas, precondiciones y pasos.
///
/// Es sólo el formulario: quien lo abre recoge el caso con `testCase()` al aceptar y decide dónde va (el
/// catálogo, el plan del ciclo en curso…). No se acepta sin título ni sin al menos un paso con acción:
/// un caso así no se puede ejecutar.
class CaseCreateDialog : public QDialog {
    Q_OBJECT
public:
    /// `suites` son las del proyecto, para elegir una (o crear otra); `context` dice a dónde irá el caso
    /// («Se añade al plan Regresión y entra al ciclo en curso»).
    CaseCreateDialog(const QStringList& suites, const QString& context, QWidget* parent = nullptr);

    /// El caso tal y como se escribió, sin los pasos vacíos. Sin id: lo pone el catálogo al añadirlo.
    TestCase testCase() const;
    /// Si al crearlo se quiere escribir otro enseguida.
    bool createAnother() const;

private:
    void validate();

    CaseForm* m_form;
    QLabel* m_problem;
    QCheckBox* m_another;
    QPushButton* m_accept;
};

} // namespace qaflow
