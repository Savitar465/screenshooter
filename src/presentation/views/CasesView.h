#pragma once

#include "core/models/CaseFilter.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QComboBox;
class QVBoxLayout;
class QGridLayout;
class QHBoxLayout;
class QPushButton;
class QLayout;

namespace qaflow {

class TestCaseStore;
class RunController;
class RunHistoryStore;
class CaseTransferService;
class TextArea;

/// Pantalla "Casos de prueba": lista filtrable a la izquierda y editor del caso a la derecha.
class CasesView : public QWidget {
    Q_OBJECT
public:
    CasesView(TestCaseStore& store, RunController& run, RunHistoryStore& history, CaseTransferService& transfer,
              QWidget* parent = nullptr);

signals:
    void runRequested(const QString& caseId);
    void captureRequested();
    void historyRequested(const QString& caseId);
    /// Abrir en el navegador la historia de Jira enlazada al caso.
    void openJiraRequested(const QString& key);
    void toast(const QString& message, const QString& color);

private:
    void buildListPane(QHBoxLayout* root);
    void buildEditor(QHBoxLayout* root);
    void refreshFilters();
    void refreshList();
    void loadEditor();
    void refreshSteps();
    void refreshShots();
    void refreshHistory();
    void onCaseChanged(const QString& id);
    void edit(const std::function<void()>& mutation);

    void newSuite();
    void duplicateSelected();
    void removeSelected();
    void importCases();
    void exportCases(int format);

    TestCaseStore& m_store;
    RunController& m_run;
    RunHistoryStore& m_history;
    CaseTransferService& m_transfer;
    CaseFilter m_filter;
    bool m_selfEdit = false;

    // lista
    QLayout* m_filterRow = nullptr;
    QComboBox* m_statusFilter = nullptr;
    QComboBox* m_priorityFilter = nullptr;
    QComboBox* m_outcomeFilter = nullptr;
    QLabel* m_listCount = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
    // editor
    QWidget* m_editor = nullptr;
    QLabel* m_idLabel = nullptr;
    QLineEdit* m_title = nullptr;
    QComboBox* m_suiteBox = nullptr;
    QComboBox* m_priorityBox = nullptr;
    QComboBox* m_statusBox = nullptr;
    QLabel* m_lastRun = nullptr;
    QLineEdit* m_component = nullptr;
    QLineEdit* m_jiraKey = nullptr;
    QPushButton* m_openJira = nullptr;
    QLineEdit* m_tags = nullptr;
    TextArea* m_pre = nullptr;
    QLabel* m_stepsHeader = nullptr;
    QVBoxLayout* m_stepsLayout = nullptr;
    QLabel* m_shotsHeader = nullptr;
    QLabel* m_unassigned = nullptr;
    QPushButton* m_sortShots = nullptr;
    QGridLayout* m_shotsGrid = nullptr;
    QWidget* m_shotsContainer = nullptr;
    QLabel* m_historyHeader = nullptr;
    QVBoxLayout* m_historyLayout = nullptr;
};

} // namespace qaflow
