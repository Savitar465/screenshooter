#pragma once

#include "core/models/IssueLink.h"
#include "core/models/RunHistory.h"

#include <QPointer>
#include <QSet>
#include <QWidget>

class QLabel;
class QPushButton;
class QLineEdit;
class QLayout;
class QVBoxLayout;
class QHBoxLayout;

namespace qaflow {

class EvidenceService;
class RunController;
class TestCaseStore;
class RunHistoryStore;
class TrendChart;
struct PlanReport;
struct PlanReportRow;

/// Pantalla "Historial": lista de ejecuciones de planes y de casos a la izquierda;
/// a la derecha, el informe del plan (exportable a Markdown) o el detalle de una ejecución.
/// El informe del plan es maestro-detalle: la lista de sus casos y, al lado, el elegido con sus
/// pasos y evidencias. Sólo se construye un caso a la vez, así que abrir un ciclo grande no lee
/// todas sus capturas.
class TestPublishService;

class HistoryView : public QWidget {
    Q_OBJECT
public:
    /// `publish` puede ser nullptr (tests, o sin Zephyr configurado): entonces no se ofrece publicar;
    /// `evidence` también (sin él las evidencias se ven pero no se anotan, ni se copian, ni se añaden o se quitan); y `run`
    /// dice qué ciclo está en curso, que es el único que no se puede eliminar (sin él, ninguno
    /// de los no terminados).
    HistoryView(TestCaseStore& cases, RunHistoryStore& history, TestPublishService* publish = nullptr,
                EvidenceService* evidence = nullptr, RunController* run = nullptr, QWidget* parent = nullptr);

    void showPlan(const QString& planRunId);
    void showRun(const QString& runId);
    /// Filtra la lista a las ejecuciones de un caso.
    void showCase(const QString& caseId);
    /// Panel de métricas: tasa de éxito por suite y evolución entre ciclos.
    void showMetrics();
    /// Vuelve a pintar la pantalla (p. ej. al activar o desactivar Zephyr en los ajustes).
    void refresh();

signals:
    void openCaseRequested(const QString& caseId);
    /// Abrir en el navegador un issue de Jira (la historia del caso o su Test de Zephyr).
    void openJiraRequested(const QString& key);
    /// Abrir en el navegador una URL ya construida (el ciclo de Zephyr en Jira).
    void openUrlRequested(const QString& url);
    /// Continuar ese ciclo con sus casos fallados y bloqueados; lo arranca quien coordina la ejecución.
    void continueCycleRequested(const QString& planRunId);
    /// Ejecutar esos casos en el ciclo terminado (reabriéndolo); lo arranca quien coordina la ejecución.
    void runInCycleRequested(const QString& planRunId, const QStringList& caseIds);
    void toast(const QString& message, const QString& color);

private:
    enum class Mode { All, Plans, Runs, Metrics };

    QWidget* buildListPane();
    QWidget* buildDetailPane();
    void refreshFilters();
    void refreshList();
    void refreshDetail();
    void renderPlan(const PlanReport& report);
    /// Fila de la lista de casos del informe: veredicto, título, pasos y cuántas evidencias tiene.
    QPushButton* caseRow(const PlanReportRow& row);
    /// Elige el caso del informe que se ve al lado de la lista; sólo rehace ese panel.
    void selectPlanCase(const QString& caseId);
    /// Panel del caso elegido (`m_selectedCase`) del informe: enlaces, bugs, pasos y evidencias.
    void renderPlanCase(const PlanReport& report);
    /// Tarjeta «Bugs encontrados» del informe: los reportados mientras corría el ciclo, con su caso,
    /// el paso que falló y su estado en el gestor. nullptr si el ciclo no dejó ninguno.
    QWidget* bugsCard(const PlanReport& report);
    /// Una línea de bug, para la tarjeta del ciclo y para la del caso.
    QWidget* bugRow(const IssueLink& bug, bool withCase);
    void renderRun(const RunRecord& run);
    void renderMetrics();
    QWidget* stepsList(const RunRecord& run) const;
    /// Fila con la historia de Jira y el Test de Zephyr del caso; nullptr si no tiene ninguno.
    QWidget* issueLinks(const QString& jiraKey, const QString& testKey);
    /// Evidencias de esa ejecución (las que se capturaron en ella y las que heredó) como una lista
    /// de su paso y su fichero, sin miniaturas, con el botón «Ver evidencias» que abre el visor y, con
    /// servicio de evidencias, el de adjuntar más y la × de cada una. nullptr si no hay ninguna ni se
    /// pueden añadir.
    QWidget* evidenceList(const RunRecord& run);
    /// Visor a tamaño completo de las evidencias de la ejecución, abierto en la `index`-ésima, con el
    /// caso, su ciclo y el paso de cada una.
    void openEvidenceViewer(const RunRecord& run, int index);
    /// «Adjuntar…» de la ejecución: con pasos, un menú para elegir a cuál va la evidencia.
    QPushButton* attachButton(const RunRecord& run);
    /// Elige ficheros y los adjunta a la ejecución archivada, en el paso `step` (0 = sin paso).
    void attachEvidence(const RunRecord& run, int step);
    /// Quita una evidencia de la ejecución (si ya se publicó, avisa de que en Zephyr se queda). Si otra
    /// ejecución la tiene, sólo se desvincula de ésta. El aviso sale sobre `dialogParent` (el visor, si
    /// se quita desde él). Devuelve si se quitó.
    bool removeEvidence(const RunRecord& run, int shotId, QWidget* dialogParent = nullptr);
    /// La ejecución es de un ciclo ya publicado en Zephyr.
    bool isPublished(const RunRecord& run) const;
    /// Un caso cambió (evidencia añadida, quitada, anotada o deshecha): si se está viendo, se repinta.
    void caseEvidenceChanged(const QString& caseId);
    void exportMarkdown(const PlanReport& report);
    void copyMarkdown(const PlanReport& report);
    /// Crea en Zephyr el ciclo con las ejecuciones del informe, sus pasos y sus evidencias.
    void publishToZephyr(const PlanReport& report);
    /// Actualiza el ciclo de Zephyr en el que el informe ya está publicado.
    void updateInZephyr(const PlanReport& report);
    /// Elige un caso del proyecto que el ciclo no tiene para ejecutarlo en él.
    void pickCaseForCycle(const PlanReport& report);
    /// Elige una ejecución de fuera del ciclo (de otro ciclo o suelta) para traerla a él.
    void pickRunForCycle(const PlanReport& report);
    /// Trae la ejecución al ciclo (`RunController::importIntoCycle`) y la enseña en el informe.
    void importRun(const QString& planRunId, const QString& runId);
    /// Pide confirmación y quita el caso del ciclo con sus ejecuciones en él.
    void removeCaseFromCycle(const PlanReport& report, const QString& caseId);
    /// Pide confirmación y elimina el ciclo con sus ejecuciones y evidencias.
    void deletePlan(const PlanReport& report);

    TestCaseStore& m_cases;
    RunHistoryStore& m_history;
    TestPublishService* m_publish;
    EvidenceService* m_evidence;
    RunController* m_run;
    Mode m_mode = Mode::All;
    QString m_search;
    QString m_selectedPlan;
    QString m_selectedRun;
    QString m_selectedCase;  // caso del informe del plan que se ve en detalle
    QString m_metricsPlan;   // plan cuya evolución se muestra (vacío = todos)
    QSet<QString> m_pendingCases;   // casos cambiados pendientes de repintar (se agrupan en un único repintado)

    QLineEdit* m_searchBox = nullptr;
    QLayout* m_filterRow = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
    QVBoxLayout* m_detailLayout = nullptr;
    QPointer<QWidget> m_caseList;          // lista de casos del informe abierto
    QPointer<QVBoxLayout> m_caseLayout;    // panel del caso elegido
    QLabel* m_empty = nullptr;
};

} // namespace qaflow
