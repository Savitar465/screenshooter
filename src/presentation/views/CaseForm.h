#pragma once

#include "core/models/TestCase.h"

#include <QStringList>
#include <QWidget>

#include <functional>

class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QScrollArea;
class QTimer;
class QVBoxLayout;

namespace qaflow {

class TestCaseStore;
class TextArea;

/// El formulario de un caso de prueba: título, suite, prioridad, estado, componente, etiquetas,
/// precondiciones y pasos (acción, datos de la prueba y resultado esperado). Es el mismo en la pantalla
/// Casos y en la ventana del caso nuevo, para que los dos pidan lo mismo y se llenen igual.
///
/// Trabaja de dos maneras: ligado al caso seleccionado del catálogo —cada tecla va al store, y quien lo
/// usa lo recarga con `load()` cuando el caso cambia por otro lado— o sobre un caso propio, suelto, que
/// se recoge con `testCase()` (el caso nuevo, que no existe hasta aceptarlo).
///
/// Los campos crecen con su texto: un título o un paso largo se ve entero, y los cortos no ocupan de más.
/// Ctrl+Intro en un paso crea otro debajo, y los pasos se reordenan arrastrándolos por su asa.
class CaseForm : public QWidget {
    Q_OBJECT
public:
    struct Options {
        bool status = true;   // el estado (Listo / Obsoleto); un caso nuevo siempre está Listo
        bool quiet = true;    // campos sin caja hasta pasar por encima; con false, con su caja (formulario vacío)
    };

    /// Ligado al caso seleccionado de `store`.
    CaseForm(TestCaseStore& store, Options options, QWidget* parent = nullptr);
    /// Sobre un caso propio; `suites` son las del proyecto, para elegir una.
    CaseForm(TestCase initial, const QStringList& suites, Options options, QWidget* parent = nullptr);

    /// El caso que se edita: el seleccionado del catálogo o el propio (nullptr si no hay ninguno).
    const TestCase* current() const;
    /// El caso propio tal y como se escribió, sin los pasos vacíos.
    TestCase testCase() const;

    /// Rehace los campos con lo que tiene el caso.
    void load();
    /// Rehace el desplegable de suites (conserva la del caso aunque no esté en la lista).
    void setSuites(const QStringList& suites);
    /// Verdadero mientras un cambio sale del propio formulario: quien lo recarga al cambiar el caso no
    /// debe hacerlo entonces, o rehace el campo en el que se está escribiendo.
    bool isEditing() const { return m_selfEdit; }
    /// Estrecho, los datos y el resultado esperado de cada paso van uno debajo del otro.
    void setNarrow(bool narrow);
    void focusTitle();

signals:
    /// Cualquier cambio hecho en el formulario.
    void edited();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void build();
    void refreshSteps();
    void edit(const std::function<void(TestCase&)>& mutate);
    void editStep(int index, const std::function<void(TestStep&)>& mutate);
    /// Cambio de la lista de pasos; `focus` es el paso que se queda con el foco al rehacerla.
    void changeSteps(int focus, const std::function<void(const QString& id)>& inStore,
                     const std::function<void(QList<TestStep>&)>& local);
    void insertStep(int index);
    void duplicateStep(int index);
    void removeStep(int index);
    void moveStep(int index, int delta);
    void newSuite();
    // Arrastrar y soltar pasos
    void startDrag(QWidget* grip, int index);
    /// El hueco entre pasos (0..n) más cercano a `y`, en coordenadas de la lista.
    int dropGap(int y) const;
    void showDropLine(int gap);
    /// Mueve el paso `from` al hueco `gap`.
    void dropStep(int from, int gap);
    /// Con el paso arrastrado a `y` del área que se desplaza: cerca de un borde, la desplaza.
    void autoScroll(int y);
    void autoScrollTick();
    void stopDragFeedback();
    TextArea* field(const QString& placeholder, int maxRows, const char* quietRole, const char* boxedRole);

    TestCaseStore* m_store = nullptr;
    TestCase m_local;
    Options m_options;
    QStringList m_suites;
    bool m_selfEdit = false;
    bool m_narrow = false;
    int m_focusStep = -1;

    TextArea* m_title = nullptr;
    QComboBox* m_suite = nullptr;
    QComboBox* m_priority = nullptr;
    QComboBox* m_status = nullptr;
    QLineEdit* m_component = nullptr;
    QLineEdit* m_tags = nullptr;
    TextArea* m_pre = nullptr;
    QLabel* m_stepsCount = nullptr;
    QVBoxLayout* m_stepsLayout = nullptr;
    QWidget* m_stepsList = nullptr;
    QFrame* m_dropLine = nullptr;   // dónde caerá el paso que se arrastra
    QPoint m_dragStart;
    QScrollArea* m_scroll = nullptr;   // el área que desplaza al formulario (si la hay)
    QTimer* m_autoScroll = nullptr;
    int m_scrollStep = 0;              // píxeles por vuelta del desplazamiento automático (negativo: hacia arriba)
};

} // namespace qaflow
