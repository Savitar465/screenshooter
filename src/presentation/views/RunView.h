#pragma once

#include "core/models/TestCase.h"
#include "core/models/TestRun.h"

#include <QTimer>
#include <QWidget>

class QComboBox;
class QFrame;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace qaflow {

class TestCaseStore;
class RunController;
class SettingsStore;
class EvidenceService;
class EvidencePreview;
class ProgressCells;
class TextArea;

/// Pantalla "Ejecución", en tres columnas:
///   · izquierda: el caso (progreso, cronómetro) y la lista de pasos con su veredicto;
///   · centro: el paso activo con sus veredictos, el visor grande de la evidencia elegida y las
///     observaciones del paso;
///   · derecha: «Capturas», con todas las evidencias del caso.
class RunView : public QWidget {
    Q_OBJECT
public:
    RunView(TestCaseStore& cases, RunController& run, SettingsStore& settings, EvidenceService& evidence, QWidget* parent = nullptr);

signals:
    void captureRequested();
    void reportBugRequested();
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
    void refreshShots();
    /// Tarjeta de un paso en la lista de la izquierda.
    QWidget* stepCard(int index, const TestCase& c, const RunState& r);
    /// Evidencia abierta en el visor; la mantiene al refrescar y sigue a las capturas nuevas.
    void selectShot(int shotId);
    void selectRelativeShot(int delta);
    const Screenshot* selectedShot() const;
    void tick();   // cronómetros (cada segundo)

    TestCaseStore& m_cases;
    RunController& m_run;
    SettingsStore& m_settings;
    EvidenceService& m_evidence;

    // Columna del caso
    QFrame* m_casePanel;
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
    QLabel* m_action;
    QLabel* m_expected;
    QWidget* m_verdicts;
    QWidget* m_doneActions;
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

    // Columna de capturas
    QFrame* m_filmPanel;
    QScrollArea* m_filmScroll;
    QLabel* m_filmHeader;
    QPushButton* m_sortShots;
    QVBoxLayout* m_shotsLayout;
    QPushButton* m_record;

    int m_selectedShot = 0;   // id de la evidencia abierta en el visor (0 = ninguna)
    int m_maxShotId = 0;      // para abrir sola la captura recién hecha
    bool m_selfEdit = false;
    QTimer m_clock;
};

} // namespace qaflow
