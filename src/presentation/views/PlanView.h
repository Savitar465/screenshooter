#pragma once

#include <QWidget>

class QLabel;
class QComboBox;
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
class TestPublishService;
struct PlanReport;

/// Pantalla "Planes": lista de planes (activos y archivados) a la izquierda; a la derecha el
/// plan abierto: casos en orden de ejecución, ciclo actual, historial de ciclos con sus
/// resultados. Los ciclos se inician desde la barra superior de la ventana.
class PlanView : public QWidget {
    Q_OBJECT
public:
    /// `publish` puede ser nullptr (tests, o sin Zephyr): entonces los ciclos no se publican ni se
    /// actualizan desde aquí, aunque los ya publicados siguen enseñando sus Tests.
    PlanView(TestCaseStore& cases, PlanStore& plans, TestPublishService* publish = nullptr, QWidget* parent = nullptr);

    /// Vuelve a pintar la pantalla (p. ej. al activar o desactivar Zephyr en los ajustes).
    void refresh();

signals:
    /// Abrir en el historial el informe de un ciclo.
    void cycleReportRequested(const QString& planRunId);
    /// Abrir en el navegador el Test de Zephyr de una ejecución.
    void openJiraRequested(const QString& key);
    /// Abrir en el navegador una URL ya construida (el ciclo de Zephyr en Jira).
    void openUrlRequested(const QString& url);
    void toast(const QString& message, const QString& color);

private:
    void buildListPane(QHBoxLayout* root);
    void buildEditor(QHBoxLayout* root);
    void refreshList();
    void refreshEditor();
    void refreshCycle();
    void refreshCycles();
    /// Bloque de Zephyr de un ciclo: dónde se publicó y el Test de cada ejecución, con «Actualizar
    /// en Zephyr»; o «Publicar en Zephyr» si aún no se publicó. nullptr si no procede ninguno.
    QWidget* zephyrBlock(const PlanReport& report);
    struct CasePager {
        int page = 0;
        QLabel* summary = nullptr;
        QPushButton* previous = nullptr;
        QPushButton* next = nullptr;
    };
    QWidget* buildCasePager(CasePager& pager, const QString& name);
    void refreshCasePager(CasePager& pager, int count);
    void refreshRows();
    void newPlan();
    void duplicateActive();
    void toggleArchiveActive();
    void removeActive();

    TestCaseStore& m_cases;
    PlanStore& m_plans;
    TestPublishService* m_publish;
    bool m_selfEdit = false;
    bool m_showArchived = false;
    bool m_allCycles = false;   // la sección de historial muestra todos los ciclos o sólo los últimos

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
    QWidget* m_cyclesSection = nullptr;
    QPushButton* m_cyclesHeader = nullptr;
    QWidget* m_cyclesContent = nullptr;
    QString m_displayedPlanId;
    QVBoxLayout* m_cyclesList = nullptr;
    QLineEdit* m_caseSearch = nullptr;
    QComboBox* m_suiteFilter = nullptr;
    CasePager m_inPlanPager;
    CasePager m_availablePager;
    QLabel* m_inPlanHeader = nullptr;
    QVBoxLayout* m_inPlan = nullptr;
    QLabel* m_availableHeader = nullptr;
    QVBoxLayout* m_available = nullptr;
};

} // namespace qaflow
