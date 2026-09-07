#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QVBoxLayout;

namespace qaflow {

class TestCaseStore;
class PlanStore;

/// Pantalla "Plan de pruebas": selección de casos y arranque de la ejecución del plan.
class PlanView : public QWidget {
    Q_OBJECT
public:
    PlanView(TestCaseStore& cases, PlanStore& plan, QWidget* parent = nullptr);

signals:
    void startPlanRequested(const QStringList& caseIds, const QString& planName);
    void toast(const QString& message, const QString& color);

private:
    void refresh();

    TestCaseStore& m_cases;
    PlanStore& m_plan;
    bool m_selfEdit = false;

    QLineEdit* m_name;
    QLabel* m_count;
    QLabel* m_steps;
    QLabel* m_time;
    QVBoxLayout* m_rows;
};

} // namespace qaflow
