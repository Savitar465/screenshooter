#pragma once

#include "core/models/RunHistory.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QLayout;
class QVBoxLayout;
class QHBoxLayout;

namespace qaflow {

class TestCaseStore;
class RunHistoryStore;
class TrendChart;
struct PlanReport;

/// Pantalla "Historial": lista de ejecuciones de planes y de casos a la izquierda;
/// a la derecha, el informe del plan (exportable a Markdown) o el detalle de una ejecución.
class HistoryView : public QWidget {
    Q_OBJECT
public:
    HistoryView(TestCaseStore& cases, RunHistoryStore& history, QWidget* parent = nullptr);

    void showPlan(const QString& planRunId);
    void showRun(const QString& runId);
    /// Filtra la lista a las ejecuciones de un caso.
    void showCase(const QString& caseId);
    /// Panel de métricas: tasa de éxito por suite y evolución entre ciclos.
    void showMetrics();

signals:
    void openCaseRequested(const QString& caseId);
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
    void exportMarkdown(const PlanReport& report);
    void copyMarkdown(const PlanReport& report);

    TestCaseStore& m_cases;
    RunHistoryStore& m_history;
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
