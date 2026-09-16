#pragma once

#include <QDialog>
#include <QString>

class QComboBox;

namespace qaflow {

/// Lo que hay que saber antes de arrancar un ciclo de plan: **en qué ambiente se va a probar**.
///
/// El ambiente no es un adorno: acompaña al ciclo hasta Zephyr (va en el nombre del ciclo y en su campo
/// «environment»), así que sin él no se sabe dónde se obtuvieron esos resultados. Se propone el último
/// que se usó en el proyecto y se puede escribir cualquier otro; dejarlo vacío también vale.
///
/// El diálogo enseña además de qué requerimiento y de qué revisión va a ser el ciclo, que es lo que le
/// queda anotado al arrancarlo.
class CycleStartDialog : public QDialog {
    Q_OBJECT
public:
    /// `planName`: el plan que se va a ejecutar. `context`: a qué issue y revisión pertenecerá el ciclo
    /// ("GREQ 2026997 · revisión 2"); vacío en un ciclo que no prueba ningún requerimiento.
    /// `environment`: el que se propone (el último usado en el proyecto). `continuation`: qué ciclo se
    /// continúa y con qué casos; vacío = ciclo nuevo con todo el plan.
    CycleStartDialog(const QString& planName, const QString& context, const QString& environment,
                     const QString& continuation = QString(), QWidget* parent = nullptr);

    /// Ambiente elegido, ya limpio; vacío si no se indicó ninguno.
    QString environment() const;

private:
    QComboBox* m_environment;
};

} // namespace qaflow
