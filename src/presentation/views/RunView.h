#pragma once

#include "core/models/IssueLink.h"
#include "core/models/TestCase.h"
#include "core/models/TestRun.h"

#include <QHash>
#include <QPointer>
#include <QTimer>
#include <QWidget>

class QComboBox;
class QFrame;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QScrollArea;
class QShortcut;
class QLayout;
class QStackedWidget;
class QVBoxLayout;

namespace qaflow {

class TestCaseStore;
class BugStore;
class BugDetailWindow;
class RunController;
class RunHistoryStore;
class SettingsStore;
class EvidenceService;
class ElidedLabel;
class EvidencePreview;
class TextArea;

/// Pantalla "Ejecución": el visor manda y el paso se lee entero.
///   · izquierda: arriba el caso (estado, título, cronómetro y «Cerrar ejecución»); debajo la barra de
///     la evidencia (su paso, su fichero, a qué paso se asigna, ampliar, anotar, modo foco, quitar), el
///     visor —con ‹ › para recorrerlas— y la tira horizontal de capturas de la ejecución;
///   · derecha: el inspector, con sus pestañas —«Paso», la ficha completa del activo (acción, datos,
///     esperado y observaciones, con scroll: los textos pueden ser largos) con los números de todos
///     los pasos para saltar; «Pasos», la lista; «Bugs», los partes de esta ejecución por paso; y, en
///     un ciclo, «Casos», los del plan con cómo va cada uno, para saltar de uno a otro— y al pie,
///     siempre a mano, los veredictos, capturar, reportar bug y ir y venir de paso.
///
/// El **modo foco** (F11, o el botón del visor) deja la evidencia a toda la ventana: se esconden el
/// caso, el inspector, la tira y el marco de la ventana principal, y un mando al pie mantiene el paso
/// y sus veredictos para seguir marcando sin salir. Esc vuelve.
///
/// Cuando la ejecución **continúa** un ciclo (se repiten sólo los casos que fallaron o quedaron
/// bloqueados), la cabecera lo dice y los pasos que vienen de la ejecución anterior van marcados: lo
/// que hay que volver a probar es lo de ahí en adelante.
class RunView : public QWidget {
    Q_OBJECT
public:
    RunView(TestCaseStore& cases, RunController& run, RunHistoryStore& history, SettingsStore& settings,
            EvidenceService& evidence, BugStore& bugs, QWidget* parent = nullptr);

    bool focusMode() const { return m_focusMode; }
    /// Entra o sale del modo foco. Sin ejecución no hay nada que enfocar: se queda fuera.
    void setFocusMode(bool on);

signals:
    void captureRequested();
    /// Abrir una URL en el navegador (el bug en el gestor, desde su ficha).
    void openUrlRequested(const QString& url);
    /// Abrir el parte de un bug para el paso `stepIndex` (0-based; -1 = el que decida la ejecución).
    void reportBugRequested(int stepIndex);
    /// El usuario pulsó "Cerrar ejecución": la ventana decide si sigue el plan, muestra el informe o vuelve.
    void finishRequested();
    void toast(const QString& message, const QString& color);
    /// El modo foco cambió: la ventana esconde (o recupera) su rail, su barra y su barra de estado.
    void focusModeChanged(bool on);

protected:
    /// Salir de la pantalla saca del modo foco: la ventana no se queda sin su marco en otra pantalla.
    void hideEvent(QHideEvent* e) override;

private:
    // Construcción (en el orden en que se leen en pantalla).
    QWidget* buildCaseBar();
    QWidget* buildStage();
    QWidget* buildFilmStrip();
    QWidget* buildFocusBar();
    QWidget* buildInspector();
    QWidget* buildStepPage();
    QWidget* buildFooter();
    /// Los cuatro veredictos en un grupo; hay dos: el del inspector y el del modo foco.
    QWidget* buildVerdicts();
    /// «Capturar» con su icono; el del inspector lleva además el atajo (`shortcut`).
    QPushButton* captureButton(QLabel** shortcut);

    void refresh();
    void refreshSteps();
    /// Paso al que se le colgaría un bug ahora mismo: el de la pantalla si tiene problema, si no
    /// el fallo o bloqueo que haya visto la ejecución. -1 si no hay caso.
    int bugStepIndex() const;
    void refreshShots();
    /// La lista de bugs de la pestaña: los reportados desde este caso, por el paso del que salieron.
    void refreshBugs();
    /// Bugs que se han reportado en la ejecución que está en curso. No son «los del caso»: un bug
    /// pertenece a las pruebas de las que salió, y de las anteriores se habla en sus resultados.
    QList<IssueLink> bugsOfRun() const;
    /// La pestaña «Casos»: los del ciclo en el orden del plan, con su estado; sólo existe en un ciclo.
    void refreshCases();
    /// Tarjeta de un caso en la pestaña «Casos». Las de los pendientes llevan a él con un clic.
    QWidget* caseCard(const QString& caseId, const QHash<QString, Verdict>& archived);
    /// Cambia de pestaña en el inspector: 0 = el paso, 1 = la lista de pasos, 2 = los bugs, 3 = los casos.
    void showTab(int index);
    /// Abre (o trae al frente) la ficha del bug en su propia ventana.
    void openBug(const QString& key);
    /// Cabecera de un grupo de la pestaña de bugs: «PASO 03 · acción» o «SIN PASO».
    QWidget* stepGroupHeader(int step, const TestCase& c) const;
    /// Tarjeta de un paso en la pestaña «Pasos».
    QWidget* stepCard(int index, const TestCase& c, const RunState& r);
    /// Número de un paso en la pestaña «Paso»: el color de su veredicto y un clic para ir a él.
    QPushButton* stepChip(int index, const TestCase& c, const RunState& r);
    /// Color con el que se pinta un paso: su veredicto, azul si es el activo, gris si está pendiente.
    QString stepColor(int index, const RunState& r) const;
    /// Evidencia abierta en el visor; la mantiene al refrescar y sigue a las capturas nuevas.
    void selectShot(int shotId);
    void selectRelativeShot(int delta);
    const Screenshot* selectedShot() const;
    void openSelectedShot();
    void annotateSelectedShot();
    /// Coloca las flechas y el contador que van sobre el visor.
    void placeViewerOverlay();
    void tick();   // cronómetros (cada segundo)
    /// Un clic en una tarjeta de la lista pone ese paso (o ese caso) en pantalla; el visor recoloca sus flechas.
    bool eventFilter(QObject* watched, QEvent* event) override;

    TestCaseStore& m_cases;
    RunController& m_run;
    RunHistoryStore& m_history;
    SettingsStore& m_settings;
    EvidenceService& m_evidence;
    BugStore& m_bugs;

    // El caso, encima del visor
    QWidget* m_caseBar;
    QFrame* m_stateDot;
    QLabel* m_stateText;
    ElidedLabel* m_caseTitle;
    QLabel* m_caseStats;
    QPushButton* m_finish;
    QWidget* m_continuation;   // aviso de que esta ejecución continúa una revisión
    QLabel* m_continuationText;

    // Visor
    QWidget* m_stage;   // barra + visor
    QFrame* m_shotBar;
    QLabel* m_shotStep;
    ElidedLabel* m_shotName;
    QComboBox* m_assign;
    QList<QWidget*> m_shotControls;   // lo de la barra que sólo sirve con una evidencia abierta
    QPushButton* m_focusButton;
    EvidencePreview* m_preview;
    QPushButton* m_shotPrev;
    QPushButton* m_shotNext;
    QLabel* m_shotCounter;

    // Tira de capturas
    QFrame* m_filmPanel;
    QLabel* m_shotsCount;
    QPushButton* m_sortShots;
    QScrollArea* m_filmScroll;
    QHBoxLayout* m_shotsLayout;
    QPushButton* m_record;
    QWidget* m_empty;

    // Inspector
    QFrame* m_casePanel;
    QPushButton* m_stepTab;
    QPushButton* m_stepsTab;
    QPushButton* m_bugsTab;
    QPushButton* m_casesTab;
    QStackedWidget* m_inspectorStack;
    QLayout* m_chipsLayout;
    QScrollArea* m_stepsScroll;
    QVBoxLayout* m_stepsLayout;
    QVBoxLayout* m_bugsLayout;
    QLabel* m_bugsEmpty;
    QScrollArea* m_casesScroll;
    QVBoxLayout* m_casesLayout;

    // Inspector: la ficha del paso
    QLabel* m_stepCounter;
    QLabel* m_stepClock;
    QLabel* m_action;
    QWidget* m_dataBlock;
    QLabel* m_data;
    QLabel* m_expectedTitle;
    QLabel* m_expected;
    QWidget* m_noteBlock;
    TextArea* m_note;

    // Inspector: el pie
    QWidget* m_verdicts;
    QWidget* m_doneActions;
    QPushButton* m_reopen;
    QPushButton* m_capture;
    QLabel* m_captureShortcut;
    QPushButton* m_reportBug;
    QPushButton* m_back;
    QPushButton* m_next;

    // Modo foco: el mando del pie
    QFrame* m_focusBar;
    QLabel* m_focusCounter;
    ElidedLabel* m_focusAction;
    QWidget* m_focusVerdicts;
    QPushButton* m_focusReportBug;
    QShortcut* m_exitFocus;

    /// Fichas de bug abiertas, por clave: pulsar otra vez el mismo bug trae la suya al frente.
    QHash<QString, QPointer<BugDetailWindow>> m_bugWindows;

    bool m_focusMode = false;
    bool m_groupedShots = false;   // hay evidencias de más de un paso: ordenarlas por paso tiene sentido
    int m_selectedShot = 0;   // id de la evidencia abierta en el visor (0 = ninguna)
    int m_maxShotId = 0;      // para abrir sola la captura recién hecha
    bool m_selfEdit = false;
    QTimer m_clock;
};

} // namespace qaflow
