#pragma once

#include "application/CaseTransferService.h"
#include "core/models/CaseFilter.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QComboBox;
class QVBoxLayout;
class QPushButton;
class QTimer;

namespace qaflow {

class TestCaseStore;
class RunController;
class RunHistoryStore;
class CaseTransferService;
class EvidenceService;
class CaseForm;

/// Pantalla "Casos de prueba": lista filtrable a la izquierda y editor del caso a la derecha.
class CasesView : public QWidget {
    Q_OBJECT
public:
    CasesView(TestCaseStore& store, RunController& run, RunHistoryStore& history, CaseTransferService& transfer,
              EvidenceService& evidence, QWidget* parent = nullptr);

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
    void toast(const QString& message, const QString& color);

private:
    QWidget* buildListPane();
    QWidget* buildEditor();
    /// Rehace los desplegables de suites sólo si ha cambiado la lista de suites.
    void refreshSuites();
    void refreshList();
    /// El botón «Filtros» dice cuántos hay puestos, y el recuento de la lista sólo se ve con alguno.
    void refreshFilterButton();
    void loadEditor();
    void refreshLastRun();
    void refreshHistory();
    void onCaseChanged(const QString& id);

    TestCaseStore& m_store;
    RunController& m_run;
    RunHistoryStore& m_history;
    CaseTransferService& m_transfer;
    EvidenceService& m_evidence;
    CaseFilter m_filter;

    // lista
    QLineEdit* m_search = nullptr;
    QPushButton* m_filtersButton = nullptr;
    QWidget* m_filtersPanel = nullptr;     // suite, estado, prioridad y ejecución: plegados tras «Filtros»
    QComboBox* m_suiteFilter = nullptr;   // con búsqueda: puede haber muchas suites
    QStringList m_knownSuites;            // las que hay en los desplegables
    QComboBox* m_statusFilter = nullptr;
    QComboBox* m_priorityFilter = nullptr;
    QComboBox* m_outcomeFilter = nullptr;
    QLabel* m_listCount = nullptr;
    QLabel* m_total = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
    QTimer* m_listRefresh = nullptr;   // agrupa los refrescos de la lista mientras se escribe en el editor
    // editor
    QWidget* m_editor = nullptr;
    QLabel* m_idLabel = nullptr;
    CaseForm* m_form = nullptr;
    QLabel* m_historyCount = nullptr;
    QLabel* m_lastRun = nullptr;
    QVBoxLayout* m_historyLayout = nullptr;
};

} // namespace qaflow
