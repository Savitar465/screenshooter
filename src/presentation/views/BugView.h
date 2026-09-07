#pragma once

#include "core/models/BugReport.h"

#include <QWidget>

class QLabel;
class QLineEdit;
class QComboBox;
class QLayout;
class QPushButton;

namespace qaflow {

class TestCaseStore;
class SettingsStore;
class BugReportService;
class TextArea;

/// Pantalla "Reportar bug": formulario prellenado desde la ejecución, se envía a Jira.
class BugView : public QWidget {
    Q_OBJECT
public:
    BugView(TestCaseStore& cases, SettingsStore& settings, BugReportService& bugs, QWidget* parent = nullptr);

    /// Rellena el formulario con el borrador actual (caso seleccionado + ejecución).
    void loadDraft();

signals:
    void captureRequested();
    void cancelled();
    void submitted(const QString& issueKey);
    void toast(const QString& message, const QString& color);

private:
    void refreshShots();
    void submit();
    BugReport collect() const;

    TestCaseStore& m_cases;
    SettingsStore& m_settings;
    BugReportService& m_bugs;
    bool m_touched = false;
    bool m_sending = false;

    QLabel* m_eyebrow;
    QLineEdit* m_title;
    QComboBox* m_severity;
    QComboBox* m_env;
    QLabel* m_linkedCase;
    TextArea* m_steps;
    TextArea* m_expected;
    TextArea* m_actual;
    QLabel* m_shotsHeader;
    QLayout* m_shotsRow;
    QPushButton* m_submit;
};

} // namespace qaflow
