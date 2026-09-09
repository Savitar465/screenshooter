#pragma once

#include "core/models/RunHistory.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QLayout;
class QVBoxLayout;
class QHBoxLayout;

namespace qaflow {

class EvidenceService;
class TestCaseStore;
class RunHistoryStore;
class TrendChart;
struct PlanReport;

/// Pantalla "Historial": lista de ejecuciones de planes y de casos a la izquierda;
/// a la derecha, el informe del plan (exportable a Markdown) o el detalle de una ejecución.
class TestPublishService;

class HistoryView : public QWidget {
    Q_OBJECT
public:
    /// `publish` puede ser nullptr (tests, o sin Zephyr configurado): entonces no se ofrece publicar;
    /// `evidence` también (sin él las evidencias se ven pero no se anotan ni se copian).
    HistoryView(TestCaseStore& cases, RunHistoryStore& history, TestPublishService* publish = nullptr,
                EvidenceService* evidence = nullptr, QWidget* parent = nullptr);

    void showPlan(const QString& planRunId);
    void showRun(const QString& runId);
    /// Filtra la lista a las ejecuciones de un caso.
    void showCase(const QString& caseId);
    /// Panel de métricas: tasa de éxito por suite y evolución entre ciclos.
    void showMetrics();

signals:
    void openCaseRequested(const QString& caseId);
    /// Abrir en el navegador un issue de Jira (la historia del caso o su Test de Zephyr).
    void openJiraRequested(const QString& key);
    void toast(const QString& message, const QString& color);

private:
    enum class Mode { All, Plans, Runs, Metrics };

    void buildListPane(QHBoxLayout* root);
    void buildDetailPane(QHBoxLayout* root);
    void refreshFilters();
    void refreshList();
    void refreshDetail();
    void renderPlan(const PlanReport& report);
    void renderRun(const RunRecord& run);
    void renderMetrics();
    QWidget* stepsList(const RunRecord& run) const;
    /// Fila con la historia de Jira y el Test de Zephyr del caso; nullptr si no tiene ninguno.
    QWidget* issueLinks(const QString& jiraKey, const QString& testKey);
    /// Rejilla con las evidencias que se capturaron en esa ejecución; nullptr si no hubo ninguna.
    QWidget* evidenceGrid(const QString& caseId, const QString& runId, int columns);
    void exportMarkdown(const PlanReport& report);
    void copyMarkdown(const PlanReport& report);
    /// Crea en Zephyr el ciclo con las ejecuciones del informe, sus pasos y sus evidencias.
    void publishToZephyr(const PlanReport& report);

    TestCaseStore& m_cases;
    RunHistoryStore& m_history;
    TestPublishService* m_publish;
    EvidenceService* m_evidence;
    Mode m_mode = Mode::All;
    QString m_search;
    QString m_selectedPlan;
    QString m_selectedRun;
    QString m_metricsPlan;   // plan cuya evolución se muestra (vacío = todos)

    QLineEdit* m_searchBox = nullptr;
    QLayout* m_filterRow = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
    QVBoxLayout* m_detailLayout = nullptr;
    QLabel* m_empty = nullptr;
};

} // namespace qaflow
