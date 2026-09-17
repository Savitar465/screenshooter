#pragma once
#include <QMainWindow>

class QLabel;
class QStackedWidget;
namespace qaflow {
class BusyIndicator;
class MainWindow;
/// Ventana nativa estable; las vistas y servicios de cada proyecto conservan su contexto.
class WorkspaceWindow : public QMainWindow {
public:
    explicit WorkspaceWindow(QWidget* parent = nullptr);
    ~WorkspaceWindow() override;
    void showProject(MainWindow* project);
    MainWindow* currentProject() const;
    /// Estado «cargando» de la aplicación: atenúa lo que hay y dice qué se está haciendo. Vive aquí, en
    /// el armazón, porque al abrir un proyecto se sustituye la ventana entera del anterior y un aviso
    /// que viviera en ella desaparecería a mitad de la operación.
    void setBusy(bool busy, const QString& title = QString(), const QString& detail = QString());
    bool isBusy() const;
protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
private:
    QStackedWidget* m_projects;
    QWidget* m_busy = nullptr;
    QLabel* m_busyTitle = nullptr;
    QLabel* m_busyDetail = nullptr;
    BusyIndicator* m_busySpinner = nullptr;
};
} // namespace qaflow
