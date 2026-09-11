#include "WorkspaceWindow.h"
#include "MainWindow.h"
#include <QApplication>
#include <QCloseEvent>
#include <QStackedWidget>

namespace qaflow {
WorkspaceWindow::WorkspaceWindow(QWidget* parent) : QMainWindow(parent), m_projects(new QStackedWidget) {
    setObjectName(QStringLiteral("workspaceWindow"));
    setMinimumSize(1100, 720);
    resize(1360, 860);
    setCentralWidget(m_projects);
}
WorkspaceWindow::~WorkspaceWindow() {
    // La sesión es dueña de cada panel, incluso si se destruye primero esta ventana.
    while (m_projects->count()) {
        QWidget* panel = m_projects->widget(0);
        m_projects->removeWidget(panel);
        panel->setParent(nullptr);
    }
}
MainWindow* WorkspaceWindow::currentProject() const {
    return qobject_cast<MainWindow*>(m_projects->currentWidget());
}
void WorkspaceWindow::showProject(MainWindow* project) {
    if (!project) return;
    setUpdatesEnabled(false);
    if (auto* previous = currentProject(); previous && previous != project) previous->setProjectActive(false);
    if (m_projects->indexOf(project) < 0) {
        project->setParent(m_projects, Qt::Widget);
        m_projects->addWidget(project);
        connect(project, &QWidget::windowTitleChanged, this, [this, project](const QString& title) {
            if (currentProject() == project) setWindowTitle(title);
        });
    }
    m_projects->setCurrentWidget(project);
    project->setProjectActive(true);
    project->show();
    setWindowTitle(project->windowTitle());
    setWindowIcon(project->windowIcon());
    setUpdatesEnabled(true);
}
void WorkspaceWindow::closeEvent(QCloseEvent* event) {
    // Reutilizar la preferencia de cerrar a la bandeja del proyecto activo.
    if (auto* project = currentProject()) QApplication::sendEvent(project, event);
    else QMainWindow::closeEvent(event);
}
} // namespace qaflow
