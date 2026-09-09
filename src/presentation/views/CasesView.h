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
class TestPublishService;
class TextArea;

/// Pantalla "Casos de prueba": lista filtrable a la izquierda y editor del caso a la derecha.
class CasesView : public QWidget {
    Q_OBJECT
public:
    /// `publish` puede ser nullptr (tests): sin él, el caso no ofrece crear su Test en Zephyr.
    CasesView(TestCaseStore& store, RunController& run, RunHistoryStore& history, CaseTransferService& transfer,
              BugStore& bugs, EvidenceService& evidence, TestPublishService* publish = nullptr, QWidget* parent = nullptr);

    // Acciones también accesibles desde el menú de la ventana
    void focusSearch();
    void duplicateSelected();
    void removeSelected();
    void importCases();
    void exportCases(CaseTransferService::Format format);

signals:
    void runRequested(const QString& caseId);
    void captureRequested();
    void historyRequested(const QString& caseId);
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
    void refreshShots();
    void refreshHistory();
    void refreshBugs();
    /// El Test de Zephyr del caso: se puede enlazar a mano y, si no lo hay, crearlo desde el caso.
    void refreshTestKey();
    void createZephyrTest();
    void onCaseChanged(const QString& id);
    void edit(const std::function<void()>& mutation);

    void newSuite();

    TestCaseStore& m_store;
    RunController& m_run;
    RunHistoryStore& m_history;
    CaseTransferService& m_transfer;
    BugStore& m_bugs;
    EvidenceService& m_evidence;
    TestPublishService* m_publish = nullptr;
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
    QLineEdit* m_testKey = nullptr;      // issue de tipo Test en Zephyr, enlazado al caso
    QPushButton* m_openTest = nullptr;   // abrirlo en Jira
    QPushButton* m_createTest = nullptr; // crearlo a partir del caso
    bool m_creatingTest = false;
    QPushButton* m_openJira = nullptr;
    QLineEdit* m_tags = nullptr;
    TextArea* m_pre = nullptr;
    QLabel* m_stepsHeader = nullptr;
    QVBoxLayout* m_stepsLayout = nullptr;
    QLabel* m_shotsHeader = nullptr;
    QLabel* m_unassigned = nullptr;
    QPushButton* m_sortShots = nullptr;
    QPushButton* m_record = nullptr;
    QGridLayout* m_shotsGrid = nullptr;
    QWidget* m_shotsContainer = nullptr;
    QLabel* m_historyHeader = nullptr;
    QVBoxLayout* m_historyLayout = nullptr;
    QLabel* m_bugsHeader = nullptr;
    QVBoxLayout* m_bugsLayout = nullptr;
};

} // namespace qaflow
