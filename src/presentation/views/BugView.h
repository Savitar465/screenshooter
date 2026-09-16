#pragma once

#include "core/models/BugReport.h"
#include "core/services/IIssueTracker.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QComboBox;
class QLayout;
class QVBoxLayout;
class QPushButton;
class QTimer;

namespace qaflow {

class TestCaseStore;
class SettingsStore;
class BugReportService;
class BugStore;
class EvidenceService;
class TextArea;

/// Pantalla "Reportar bug": formulario prellenado desde la ejecución con los campos reales del
/// gestor, más los bugs ya reportados (con su estado) y la cola de envíos pendientes.
class BugView : public QWidget {
    Q_OBJECT
public:
    BugView(TestCaseStore& cases, SettingsStore& settings, BugReportService& bugs, BugStore& ledger, EvidenceService& evidence, QWidget* parent = nullptr);

    /// Rellena el formulario con el borrador actual (caso seleccionado + ejecución).
    void loadDraft();
    /// Paso (0-based) del que va el siguiente borrador; -1 deja que lo decida la ejecución.
    void setDraftStep(int stepIndex) { m_draftStep = stepIndex; }

signals:
    void captureRequested();
    void cancelled();
    void submitted(const QString& issueKey);
    void openIssueRequested(const QString& url);
    void toast(const QString& message, const QString& color);

private:
    void buildForm(QVBoxLayout* v);
    void buildLists(QVBoxLayout* v);
    void refreshHeader();
    void refreshShots();
    /// Opciones del combo de paso, con los pasos del caso seleccionado.
    void refreshStepOptions(int step);
    void refreshTrackerFields();
    void refreshIssues();
    void refreshPending();
    void loadMetadata(bool force);
    /// Pide al gestor las personas que encajan con lo escrito en "Asignado a".
    void searchAssignees();
    /// Sustituye las opciones del combo sin tocar lo que se está escribiendo.
    void setAssigneeOptions(const QList<Assignee>& people);
    void submit();
    void retryPending();
    void refreshStatuses();
    BugReport collect() const;

    TestCaseStore& m_cases;
    SettingsStore& m_settings;
    BugReportService& m_bugs;
    BugStore& m_ledger;
    EvidenceService& m_evidence;
    bool m_touched = false;
    int m_draftStep = -1;          // paso que pidió quien abrió el parte, para el próximo loadDraft()
    bool m_sending = false;
    bool m_busy = false;

    QLabel* m_eyebrow;
    QLineEdit* m_title;
    QComboBox* m_severity;
    QComboBox* m_classification;
    QComboBox* m_env;
    QLabel* m_linkedCase;
    QComboBox* m_linkedStep;       // paso del caso al que pertenece el bug (0 = el caso entero)
    QComboBox* m_issueType;
    QComboBox* m_priority;
    QComboBox* m_assignee;
    QTimer* m_assigneeSearch;      // retardo entre pulsaciones para no llamar al gestor en cada letra
    int m_assigneeSeq = 0;         // descarta respuestas que llegan tarde, ya con otro texto escrito
    QLineEdit* m_components;
    QLineEdit* m_versions;
    QLineEdit* m_labels;
    QLabel* m_metaNote;
    QPushButton* m_loadMeta;
    TextArea* m_steps;
    TextArea* m_expected;
    TextArea* m_actual;
    QLabel* m_shotsHeader;
    QLayout* m_shotsRow;
    QPushButton* m_submit;
    QLabel* m_issuesHeader;
    QPushButton* m_refreshStatuses;
    QVBoxLayout* m_issuesList;
    QWidget* m_pendingBlock;
    QLabel* m_pendingHeader;
    QPushButton* m_retry;
    QVBoxLayout* m_pendingList;
};

} // namespace qaflow
