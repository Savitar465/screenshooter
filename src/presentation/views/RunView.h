#pragma once

#include <QTimer>
#include <QWidget>

class QLabel;
class QVBoxLayout;
class QFrame;
class QPushButton;

namespace qaflow {

class TestCaseStore;
class RunController;
class SettingsStore;
class EvidenceService;
class ProgressCells;
class TextArea;

/// Pantalla "Ejecución": paso actual con veredicto P/F/B, registro y capturas del caso.
class RunView : public QWidget {
    Q_OBJECT
public:
    RunView(TestCaseStore& cases, RunController& run, SettingsStore& settings, EvidenceService& evidence, QWidget* parent = nullptr);

signals:
    void captureRequested();
    void reportBugRequested();
    /// El usuario pulsó "Finalizar": la ventana decide si sigue el plan, muestra el informe o vuelve.
    void finishRequested();
    void toast(const QString& message, const QString& color);

private:
    void refresh();
    void refreshLog();
    void refreshShots();
    void tick();   // cronómetros (cada segundo)

    TestCaseStore& m_cases;
    RunController& m_run;
    SettingsStore& m_settings;
    EvidenceService& m_evidence;

    QLabel* m_eyebrow;
    QLabel* m_title;
    QLabel* m_shortcut;
    ProgressCells* m_progress;
    QFrame* m_stepCard;
    QLabel* m_stepCounter;
    QLabel* m_stepClock;
    QLabel* m_caseClock;
    QPushButton* m_back;
    QLabel* m_action;
    QLabel* m_expected;
    TextArea* m_note;
    QFrame* m_doneCard;
    QLabel* m_verdict;
    QLabel* m_summary;
    QPushButton* m_reportBug;
    QPushButton* m_reopen;
    QPushButton* m_finish;
    QVBoxLayout* m_logLayout;
    QLabel* m_shotsHeader;
    QLabel* m_shotFolder;
    QVBoxLayout* m_shotsLayout;
    QPushButton* m_sortShots;
    QPushButton* m_record;
    QLabel* m_empty;
    QTimer m_clock;
};

} // namespace qaflow
