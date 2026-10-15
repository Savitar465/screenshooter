#pragma once

#include <QDialog>
#include <QHash>
#include <QString>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
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
///
/// Un ciclo nuevo elige además **qué casos ejecuta**: el mismo plan no se prueba entero en cada fase (en
/// PRE no se repite todo lo de QA). Salen todos los del plan marcados, con cómo quedó cada uno en el
/// último ciclo; se desmarcan los que no se van a probar y se añaden otros del proyecto, sólo para este
/// ciclo o también para el plan.
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
        /// Lo que pasará al arrancar en esa fase y conviene saber antes («la revisión 1 se cierra como
        /// Aprobada en QA»). No impide arrancar.
        QHash<QString, QString> notes;
        /// Qué ciclo se continúa y con qué casos; vacío = ciclo nuevo con todo el plan.
        QString continuation;

        /// Un caso que se puede elegir: su id, su título y una pista (cómo quedó en el último ciclo).
        struct CaseOption {
            QString id;
            QString title;
            QString hint;
        };
        /// Los casos del plan, en su orden, para elegir cuáles ejecuta este ciclo. Vacío = no se eligen
        /// (continuar o reabrir un ciclo, que ya sabe los suyos).
        QList<CaseOption> cases;
        /// De qué ciclo vienen las pistas de `cases` ("PR-0012 · QA"); vacío si no hubo ninguno.
        QString previousCycle;
        /// Los casos del proyecto que se pueden añadir al ciclo (los que no son del plan).
        QList<CaseOption> catalog;
    };
    explicit CycleStartDialog(const Setup& setup, QWidget* parent = nullptr);

    /// Ambiente elegido, ya limpio; vacío si no se indicó ninguno.
    QString environment() const;

    /// Los casos que ejecutará el ciclo, en orden: los del plan que siguen marcados y, detrás, los
    /// añadidos. Sin elección (`Setup::cases` vacío), vacío.
    QStringList caseIds() const;
    /// Los del plan que se desmarcaron: se quedan fuera de este ciclo.
    QStringList excludedCaseIds() const;
    /// Los añadidos que siguen marcados (no son del plan).
    QStringList addedCaseIds() const;
    /// Los añadidos entran también en el plan, no sólo en este ciclo.
    bool addToPlan() const;
    /// Añade un caso del catálogo a la lista, marcado (lo que hace «Añadir caso…» al elegirlo).
    void addCase(const QString& caseId);

private:
    void refreshBlocked();
    void refreshCases();
    void pickCase();

    QComboBox* m_environment;
    QHash<QString, QString> m_blocked;
    QHash<QString, QString> m_notes;
    QLabel* m_blockedNote = nullptr;
    QPushButton* m_accept = nullptr;
    QList<Setup::CaseOption> m_catalog;
    QListWidget* m_cases = nullptr;
    QLabel* m_caseCount = nullptr;
    QCheckBox* m_addToPlan = nullptr;
};

} // namespace qaflow
