#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QLayout;
class QVBoxLayout;
class QHBoxLayout;
class QPushButton;
class QFrame;

namespace qaflow {

class TestCaseStore;
class PlanStore;
class ProgressCells;

/// Pantalla "Planes": lista de planes (activos y archivados) a la izquierda; a la derecha el
/// plan abierto: casos en orden de ejecución, ciclo actual y arranque de un ciclo nuevo.
class PlanView : public QWidget {
    Q_OBJECT
public:
    PlanView(TestCaseStore& cases, PlanStore& plans, QWidget* parent = nullptr);

signals:
    void startPlanRequested(const QStringList& caseIds, const QString& planName, const QString& planId);
    /// Abrir en el historial el informe de un ciclo.
    void cycleReportRequested(const QString& planRunId);
    void toast(const QString& message, const QString& color);

private:
    void buildListPane(QHBoxLayout* root);
    void buildEditor(QHBoxLayout* root);
    void refreshList();
    void refreshEditor();
    void refreshCycle();
    void refreshRows();
    void newPlan();
    void duplicateActive();
    void toggleArchiveActive();
    void removeActive();

    TestCaseStore& m_cases;
    PlanStore& m_plans;
    bool m_selfEdit = false;
    bool m_showArchived = false;

    // lista
    QLayout* m_filterRow = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
    // editor
    QWidget* m_editor = nullptr;
    QLabel* m_eyebrow = nullptr;
    QLineEdit* m_name = nullptr;
    QLabel* m_archivedBadge = nullptr;
    QLabel* m_count = nullptr;
    QLabel* m_steps = nullptr;
    QLabel* m_time = nullptr;
    QLabel* m_basis = nullptr;
    QFrame* m_cycleCard = nullptr;
    QLabel* m_cycleTitle = nullptr;
    QLabel* m_cycleSummary = nullptr;
    ProgressCells* m_cycleCells = nullptr;
    QPushButton* m_cycleReport = nullptr;
    QLabel* m_inPlanHeader = nullptr;
    QVBoxLayout* m_inPlan = nullptr;
    QLabel* m_availableHeader = nullptr;
    QVBoxLayout* m_available = nullptr;
    QPushButton* m_start = nullptr;
};

} // namespace qaflow
