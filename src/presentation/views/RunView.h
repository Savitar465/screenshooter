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
class QLabel;
class QPushButton;
class QScrollArea;
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
class EvidencePreview;
class ProgressCells;
class TextArea;

/// Pantalla "Ejecución", en tres columnas:
///   · izquierda: el caso (progreso, cronómetro) y la lista de pasos con su veredicto;
///   · centro: el paso activo con sus veredictos, el visor grande de la evidencia elegida y las
///     observaciones del paso;
///   · derecha: dos pestañas —«Capturas», con las evidencias de la ejecución, y «Bugs», con los partes
///     que salieron de ella—, las dos **agrupadas por el paso** al que pertenecen.
///
/// Cuando la ejecución **continúa** un ciclo (se repiten sólo los casos que fallaron o quedaron
/// bloqueados), la cabecera lo dice y los pasos que vienen de la ejecución anterior van marcados: lo
/// que hay que volver a probar es lo de ahí en adelante.
class RunView : public QWidget {
    Q_OBJECT
public:
    RunView(TestCaseStore& cases, RunController& run, RunHistoryStore& history, SettingsStore& settings,
            EvidenceService& evidence, BugStore& bugs, QWidget* parent = nullptr);

signals:
    void captureRequested();
    /// Abrir una URL en el navegador (el bug en el gestor, desde su ficha).
    void openUrlRequested(const QString& url);
    /// Abrir el parte de un bug para el paso `stepIndex` (0-based; -1 = el que decida la ejecución).
    void reportBugRequested(int stepIndex);
    /// El usuario pulsó "Cerrar ejecución": la ventana decide si sigue el plan, muestra el informe o vuelve.
    void finishRequested();
    void toast(const QString& message, const QString& color);

private:
    // Construcción (una función por columna, en el orden en que se leen en pantalla).
    QWidget* buildCasePanel();
    QWidget* buildStepPanel();
    QWidget* buildFilmPanel();

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
    /// Cambia de pestaña en la columna de la derecha.
    void showTab(int index);
    /// Abre (o trae al frente) la ficha del bug en su propia ventana.
    void openBug(const QString& key);
    /// Cabecera de un grupo de la columna derecha: «PASO 03 · acción» o «SIN PASO».
    QWidget* stepGroupHeader(int step, const TestCase& c) const;
    /// Tarjeta de un paso en la lista de la izquierda.
    QWidget* stepCard(int index, const TestCase& c, const RunState& r);
    /// Evidencia abierta en el visor; la mantiene al refrescar y sigue a las capturas nuevas.
    void selectShot(int shotId);
    void selectRelativeShot(int delta);
    const Screenshot* selectedShot() const;
    void tick();   // cronómetros (cada segundo)
    /// Un clic en una tarjeta de la lista pone ese paso en pantalla.
    bool eventFilter(QObject* watched, QEvent* event) override;

    TestCaseStore& m_cases;
    RunController& m_run;
    RunHistoryStore& m_history;
    SettingsStore& m_settings;
    EvidenceService& m_evidence;
    BugStore& m_bugs;

    // Columna del caso
    QFrame* m_casePanel;
    QWidget* m_continuation;   // aviso de que esta ejecución continúa una revisión
    QLabel* m_continuationText;
    QFrame* m_stateDot;
    QLabel* m_stateText;
    QLabel* m_caseTitle;
    QLabel* m_caseMeta;
    ProgressCells* m_progress;
    QLabel* m_caseStats;
    QVBoxLayout* m_stepsLayout;
    QPushButton* m_finish;

    // Columna del paso
    QWidget* m_header;
    QLabel* m_stepCounter;
    QLabel* m_stepClock;
    QPushButton* m_back;
    QPushButton* m_next;
    QLabel* m_action;
    QLabel* m_data;
    QLabel* m_expected;
    QWidget* m_verdicts;
    QWidget* m_doneActions;
    QWidget* m_bugRow;
    QPushButton* m_reportBug;
    QPushButton* m_reopen;
    QPushButton* m_capture;
    QLabel* m_captureShortcut;
    QWidget* m_stage;   // visor + barra de la evidencia
    EvidencePreview* m_preview;
    QFrame* m_shotBar;
    QComboBox* m_assign;
    TextArea* m_note;
    QWidget* m_empty;

    // Columna de capturas y bugs
    QFrame* m_filmPanel;
    QPushButton* m_shotsTab;
    QPushButton* m_bugsTab;
    QStackedWidget* m_filmStack;
    QScrollArea* m_filmScroll;
    QPushButton* m_sortShots;
    QVBoxLayout* m_shotsLayout;
    QWidget* m_shotsActions;
    QVBoxLayout* m_bugsLayout;
    QLabel* m_bugsEmpty;
    QPushButton* m_record;
    /// Fichas de bug abiertas, por clave: pulsar otra vez el mismo bug trae la suya al frente.
    QHash<QString, QPointer<BugDetailWindow>> m_bugWindows;

    bool m_groupedShots = false;   // hay evidencias de más de un paso: ordenarlas por paso tiene sentido
    int m_selectedShot = 0;   // id de la evidencia abierta en el visor (0 = ninguna)
    int m_maxShotId = 0;      // para abrir sola la captura recién hecha
    bool m_selfEdit = false;
    QTimer m_clock;
};

} // namespace qaflow
