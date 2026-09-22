#pragma once

#include "core/models/BugReport.h"
#include "core/models/TestCase.h"
#include "core/services/IIssueTracker.h"

#include <QDialog>

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
class EvidenceService;
class TextArea;

/// Parte de bug, en **su propia ventana**: se abre desde la ejecución (el botón «Reportar bug» o
/// Ctrl+B) y desde la pantalla de bugs, con el formulario ya relleno con el caso, el paso y lo que
/// dijo el tester.
///
/// Es modal —el parte se termina o se deja— salvo mientras se captura la pantalla: entonces el
/// diálogo se esconde solo, deja ver la aplicación que se está probando y vuelve con la captura ya
/// adjunta.
class BugDialog : public QDialog {
    Q_OBJECT
public:
    BugDialog(TestCaseStore& cases, SettingsStore& settings, BugReportService& bugs, EvidenceService& evidence,
              QWidget* parent = nullptr);

    /// Rellena el formulario con el borrador actual (caso seleccionado + ejecución). `stepIndex`
    /// (0-based) es el paso del que se reporta; -1 deja que lo decida la ejecución.
    void loadDraft(int stepIndex = -1);

    /// Adjuntos del parte: copias propias, independientes de la evidencia de la ejecución.
    const QList<Screenshot>& attachments() const { return m_shots; }
    /// Se quita de en medio, captura y vuelve con la captura adjunta al parte. Es lo que hace el
    /// atajo de captura mientras hay un parte abierto; una segunda pulsación durante la cuenta
    /// atrás la cancela.
    void captureScreen();

public slots:
    /// Al cancelar se borran los ficheros de los adjuntos, que ya no son de nadie.
    void done(int result) override;

signals:
    void toast(const QString& message, const QString& color);
    /// Bug creado en el gestor (`key`) o encolado sin conexión (`key` vacío).
    void submitted(const QString& issueKey);

private:
    void buildForm(QVBoxLayout* v);
    void refreshHeader();
    void refreshShots();
    /// Añade copias al parte (numeradas con ids propios) y repinta.
    void addShots(const QList<Screenshot>& shots);
    void removeShot(int shotId);
    /// Suelta los adjuntos actuales; con `deleteFiles` borra también sus ficheros.
    void clearShots(bool deleteFiles);
    const Screenshot* findShot(int shotId) const;
    void openShot(int shotId);
    bool annotateShot(int shotId);
    void showShotInFolder(int shotId) const;
    /// Opciones del combo de paso, con los pasos del caso seleccionado.
    void refreshStepOptions(int step);
    void refreshTrackerFields();
    void loadMetadata(bool force);
    /// Pide al gestor las personas que encajan con lo escrito en "Asignado a".
    void searchAssignees();
    /// Sustituye las opciones del combo sin tocar lo que se está escribiendo.
    void setAssigneeOptions(const QList<Assignee>& people);
    void submit();
    BugReport collect() const;

    TestCaseStore& m_cases;
    SettingsStore& m_settings;
    BugReportService& m_bugs;
    EvidenceService& m_evidence;
    bool m_touched = false;
    bool m_sending = false;
    bool m_busy = false;
    bool m_capturing = false;      // escondido mientras se captura la pantalla
    QList<Screenshot> m_shots;     // adjuntos del parte, copias en la carpeta de bugs
    int m_nextShotId = 1;

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
};

} // namespace qaflow
