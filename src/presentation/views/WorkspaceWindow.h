#pragma once
#include <QMainWindow>

class QStackedWidget;
namespace qaflow {
class MainWindow;
/// Ventana nativa estable; las vistas y servicios de cada proyecto conservan su contexto.
class WorkspaceWindow : public QMainWindow {
public:
    explicit WorkspaceWindow(QWidget* parent = nullptr);
    ~WorkspaceWindow() override;
    void showProject(MainWindow* project);
    MainWindow* currentProject() const;
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    QStackedWidget* m_projects;
};
} // namespace qaflow
