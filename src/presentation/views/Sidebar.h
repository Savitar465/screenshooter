#pragma once

#include "presentation/Screen.h"

#include <QFrame>
#include <QMap>

class QLabel;
class QPushButton;
class QProgressBar;

namespace qaflow {

class TestCaseStore;
class PlanStore;
class RunController;
class RunHistoryStore;
class BugStore;

class Sidebar : public QFrame {
    Q_OBJECT
public:
    Sidebar(TestCaseStore& cases, PlanStore& plan, RunController& run, RunHistoryStore& history, BugStore& bugs, QWidget* parent = nullptr);
    void setActive(Screen s);

signals:
    void navigate(Screen s);
    /// Abrir las métricas (tasa de éxito por suite y evolución entre ciclos) en el historial.
    void metricsRequested();

private:
    void refresh();
    QPushButton* navButton(Screen s, const QString& label, const QString& dotColor);

    TestCaseStore& m_cases;
    PlanStore& m_plan;
    RunController& m_run;
    RunHistoryStore& m_history;
    BugStore& m_bugs;
    Screen m_active = Screen::Casos;

    QMap<Screen, QPushButton*> m_buttons;
    QMap<Screen, QLabel*> m_counts;
    QPushButton* m_runningCard;
    QLabel* m_runId;
    QLabel* m_runTitle;
    QLabel* m_runStep;
    QLabel* m_planName;
    QLabel* m_planCycle;
    QLabel* m_sprintCount;
    QProgressBar* m_sprintBar;
    QLabel* m_rate;
    QLabel* m_rateDetail;
    QLabel* m_trend;
};

} // namespace qaflow
