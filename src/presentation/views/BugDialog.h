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
class QStackedWidget;
class QTimer;

namespace qaflow {

class TestCaseStore;
class SettingsStore;
class BugReportService;
class EvidenceService;
class TextArea;

/// Parte de bug, en **su propia ventana**: se abre desde la ejecución (el botón «Reportar bug» o
/// Ctrl+B) y desde la pantalla de bugs, con el formulario ya relleno con el caso, el paso, el ambiente
/// del ciclo y lo que dijo el tester.
///
/// Es un asistente de tres pasos, en el orden en que se escribe un bug: **qué pasó** (resultado y
/// pasos para reproducirlo), **contexto** (ambiente, usuarios, severidad, evidencia) y **revisar y
/// enviar** (campos del gestor y el título, que se propone al final con lo ya escrito). Los pasos se
/// recorren en cualquier orden; enviar comprueba lo obligatorio y lleva al paso donde falta.
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

    enum Page { WhatHappened = 0, Context = 1, Review = 2 };
    /// Paso del asistente que se está viendo.
    int page() const;
    void showPage(int page);

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
    /// El carril de pasos, completo o sólo con sus números cuando el diálogo es estrecho.
    void fitRail(bool narrow);
    QWidget* buildRail();
    QWidget* buildWhatHappened();
    QWidget* buildContext();
    QWidget* buildReview();
    QWidget* buildFooter();
    void refreshHeader();
    void refreshNav();
    /// Caso, paso y ciclo del parte, en el pie del carril.
    void refreshContext();
    /// Lo que lleva el parte, de un vistazo, en el último paso.
    void refreshSummary();
    /// Títulos propuestos con lo ya escrito; si el título no se ha tocado a mano, pone el primero.
    void refreshTitle();
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

    // Pasos para reproducir: una fila por paso, que se añaden, se quitan y se renumeran.
    void addStepRow(const QString& text, int at = -1, bool focus = false);
    void clearStepRows();
    void renumberSteps();
    QStringList reproSteps() const;
    void importCaseSteps();
    // Usuarios del sistema usados: una fila (usuario, rol) por persona.
    void addUserRow(const TestUser& user, bool focus = false);
    void clearUserRows();
    QList<TestUser> testUsers() const;
    /// Recientes que aún no están en el parte, como atajos para añadirlos.
    void refreshRecentUsers();

    /// Opciones del combo de paso, con los pasos del caso seleccionado.
    void refreshStepOptions(int step);
    void refreshEnvironment(const QString& environment);
    void refreshTrackerFields();
    /// Avisa de los componentes y versiones que el proyecto del gestor no tiene: no se envían.
    void refreshUnknownValues();
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
    bool m_titleEdited = false;    // el título lo escribió alguien: ya no se propone solo
    bool m_issueTypeEdited = false;
    QList<Screenshot> m_shots;     // adjuntos del parte, copias en la carpeta de bugs
    int m_nextShotId = 1;
    QString m_linkedCaseId;
    QString m_linkedPlanRunId;

    QLabel* m_eyebrow;
    QPushButton* m_nav[3];
    QLabel* m_navBadge[3];
    QWidget* m_navText[3];
    QWidget* m_rail;
    QLabel* m_railTitle;
    QLabel* m_context;
    QStackedWidget* m_pages;
    QPushButton* m_back;
    QPushButton* m_next;
    QPushButton* m_submit;

    TextArea* m_actual;
    TextArea* m_expected;
    QVBoxLayout* m_stepRows;

    QComboBox* m_env;
    QLabel* m_envNote;
    QComboBox* m_severity;
    QComboBox* m_classification;
    QComboBox* m_linkedStep;       // paso del caso al que pertenece el bug (0 = el caso entero)
    QVBoxLayout* m_userRows;
    QLayout* m_recentUsers;
    QLabel* m_shotsHeader;
    QLayout* m_shotsRow;

    QLineEdit* m_title;
    QLayout* m_titleSuggestions;
    QComboBox* m_issueType;
    QLabel* m_issueTypeNote;
    QComboBox* m_priority;
    QComboBox* m_assignee;
    QTimer* m_assigneeSearch;      // retardo entre pulsaciones para no llamar al gestor en cada letra
    int m_assigneeSeq = 0;         // descarta respuestas que llegan tarde, ya con otro texto escrito
    QLineEdit* m_components;
    QLineEdit* m_versions;
    QLabel* m_unknownNote;
    QLineEdit* m_labels;
    QLabel* m_metaNote;
    QPushButton* m_loadMeta;
    QLabel* m_summary;
};

} // namespace qaflow
