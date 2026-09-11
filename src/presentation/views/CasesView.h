#pragma once

#include "application/CaseTransferService.h"
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
class BugStore;
class EvidenceService;
class TextArea;

/// Pantalla "Casos de prueba": lista filtrable a la izquierda y editor del caso a la derecha.
class CasesView : public QWidget {
    Q_OBJECT
public:
    CasesView(TestCaseStore& store, RunController& run, RunHistoryStore& history, CaseTransferService& transfer,
              BugStore& bugs, EvidenceService& evidence, QWidget* parent = nullptr);

    // Acciones también accesibles desde el menú de la ventana
    void focusSearch();
    void duplicateSelected();
    void removeSelected();
    void importCases();
    void exportCases(CaseTransferService::Format format);

signals:
    void historyRequested(const QString& caseId);
    /// Abrir en el historial los resultados de una ejecución concreta del caso.
    void openRunRequested(const QString& runId);
    /// Abrir en el navegador la historia de Jira enlazada al caso.
    void openJiraRequested(const QString& key);
    /// Abrir en el navegador un issue ya creado.
    void openIssueRequested(const QString& url);
    void toast(const QString& message, const QString& color);

private:
    void buildListPane(QHBoxLayout* root);
    void buildEditor(QHBoxLayout* root);
    void refreshFilters();
    void refreshList();
    void loadEditor();
    void refreshSteps();
    void refreshHistory();
    void refreshBugs();
    void onCaseChanged(const QString& id);
    void edit(const std::function<void()>& mutation);

    void newSuite();

    TestCaseStore& m_store;
    RunController& m_run;
    RunHistoryStore& m_history;
    CaseTransferService& m_transfer;
    BugStore& m_bugs;
    EvidenceService& m_evidence;
    CaseFilter m_filter;
    bool m_selfEdit = false;

    // lista
    QLineEdit* m_search = nullptr;
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
    QLabel* m_historyHeader = nullptr;
    QVBoxLayout* m_historyLayout = nullptr;
    QLabel* m_bugsHeader = nullptr;
    QVBoxLayout* m_bugsLayout = nullptr;
};

} // namespace qaflow
