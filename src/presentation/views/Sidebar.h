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

class Sidebar : public QFrame {
    Q_OBJECT
public:
    Sidebar(TestCaseStore& cases, PlanStore& plan, RunController& run, QWidget* parent = nullptr);
    void setActive(Screen s);

signals:
    void navigate(Screen s);

private:
    void refresh();
    QPushButton* navButton(Screen s, const QString& label, const QString& dotColor);

    TestCaseStore& m_cases;
    PlanStore& m_plan;
    RunController& m_run;
    Screen m_active = Screen::Casos;

    QMap<Screen, QPushButton*> m_buttons;
    QMap<Screen, QLabel*> m_counts;
    QPushButton* m_runningCard;
    QLabel* m_runId;
    QLabel* m_runTitle;
    QLabel* m_runStep;
    QLabel* m_sprintCount;
    QProgressBar* m_sprintBar;
};

} // namespace qaflow
