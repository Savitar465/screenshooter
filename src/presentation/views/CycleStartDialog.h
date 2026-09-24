#pragma once

#include <QDialog>
#include <QHash>
#include <QString>
#include <QStringList>

class QComboBox;
class QLabel;
class QPushButton;

namespace qaflow {

/// Lo que hay que saber antes de arrancar un ciclo de plan: **en qué ambiente se va a probar**.
///
/// El ambiente no es un adorno: acompaña al ciclo hasta Zephyr (va en el nombre del ciclo y en su campo
/// «environment»), así que sin él no se sabe dónde se obtuvieron esos resultados.
///
/// Un ciclo que prueba un requerimiento se ejecuta en una de sus **fases** (QA, PRE…): se propone la que
/// le toca y se puede elegir otra de las del requerimiento. Una revisión que ya tiene ciclos en una fase
/// no se puede seguir en otra (`blocked` dice por qué y el botón no arranca); continuar un ciclo sigue en
/// su fase. Un ciclo suelto propone el último ambiente usado en el proyecto y admite cualquier otro.
///
/// El diálogo enseña además de qué requerimiento y de qué revisión va a ser el ciclo, que es lo que le
/// queda anotado al arrancarlo.
class CycleStartDialog : public QDialog {
    Q_OBJECT
public:
    struct Setup {
        QString planName;              // el plan que se va a ejecutar
        /// A qué issue y revisión pertenecerá el ciclo ("GREQ 2026997 · revisión 2 · PRE"); vacío en un
        /// ciclo que no prueba ningún requerimiento.
        QString context;
        QString environment;           // el que se propone; con `fixedEnvironment`, la fase de la revisión
        bool fixedEnvironment = false; // el ambiente viene dado y no se cambia aquí (continuar un ciclo)
        /// El ambiente es la fase del requerimiento: se elige sólo entre `environments`, sin escribir otro.
        bool phases = false;
        QStringList environments;      // los que se ofrecen cuando se elige
        /// Fases en las que no se puede arrancar ahora, con el motivo.
        QHash<QString, QString> blocked;
        /// Qué ciclo se continúa y con qué casos; vacío = ciclo nuevo con todo el plan.
        QString continuation;
    };
    explicit CycleStartDialog(const Setup& setup, QWidget* parent = nullptr);

    /// Ambiente elegido, ya limpio; vacío si no se indicó ninguno.
    QString environment() const;

private:
    void refreshBlocked();

    QComboBox* m_environment;
    QHash<QString, QString> m_blocked;
    QLabel* m_blockedNote = nullptr;
    QPushButton* m_accept = nullptr;
};

} // namespace qaflow
