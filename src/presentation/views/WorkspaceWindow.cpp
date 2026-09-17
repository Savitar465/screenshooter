#include "WorkspaceWindow.h"
#include "MainWindow.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/BusyIndicator.h"
#include "presentation/widgets/Ui.h"
#include <QApplication>
#include <QCloseEvent>
#include <QLabel>
#include <QPainter>
#include <QResizeEvent>
#include <QStackedWidget>

namespace qaflow {

namespace {
/// Velo del estado «cargando»: tapa lo que hay debajo para que no se pueda tocar y lo atenúa.
class BusyVeil : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        QColor veil(theme::Bg);
        veil.setAlpha(210);
        p.fillRect(rect(), veil);
    }
};
} // namespace

WorkspaceWindow::WorkspaceWindow(QWidget* parent) : QMainWindow(parent), m_projects(new QStackedWidget) {
    setObjectName(QStringLiteral("workspaceWindow"));
    setMinimumSize(1100, 720);
    resize(1360, 860);
    setCentralWidget(m_projects);

    m_busy = new BusyVeil(this);
    m_busy->setObjectName(QStringLiteral("workspaceBusy"));
    auto* v = ui::vbox(m_busy, 0, 12);
    v->addStretch(1);
    m_busySpinner = new BusyIndicator(30);
    v->addWidget(m_busySpinner, 0, Qt::AlignHCenter);
    m_busyTitle = ui::label(QString(), "h2");
    m_busyTitle->setObjectName(QStringLiteral("workspaceBusyTitle"));
    m_busyTitle->setAlignment(Qt::AlignCenter);
    v->addWidget(m_busyTitle);
    m_busyDetail = ui::label(QString(), "muted");
    m_busyDetail->setAlignment(Qt::AlignCenter);
    v->addWidget(m_busyDetail);
    v->addStretch(1);
    m_busy->hide();
}

void WorkspaceWindow::setBusy(bool busy, const QString& title, const QString& detail) {
    const bool wasBusy = !m_busy->isHidden();
    if (busy) {
        m_busyTitle->setText(title);
        m_busyDetail->setText(detail);
        m_busyDetail->setVisible(!detail.isEmpty());
        m_busy->setGeometry(rect());
        m_busy->raise();
        m_busy->show();
        m_busySpinner->start();
        // El cursor también lo dice, y llega a la barra de título y a los bordes de la ventana. Se
        // apila una sola vez aunque se vuelva a pedir el estado, o se quedaría puesto al quitarlo.
        if (!wasBusy) QApplication::setOverrideCursor(Qt::BusyCursor);
    } else {
        if (wasBusy) QApplication::restoreOverrideCursor();
        m_busySpinner->stop();
        m_busy->hide();
    }
}

bool WorkspaceWindow::isBusy() const { return m_busy && !m_busy->isHidden(); }

void WorkspaceWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (m_busy && !m_busy->isHidden()) m_busy->setGeometry(rect());
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
    // El velo queda por encima de la ventana que acaba de entrar; se quita cuando termina el cambio.
    if (m_busy && !m_busy->isHidden()) m_busy->raise();
    setUpdatesEnabled(true);
}
void WorkspaceWindow::closeEvent(QCloseEvent* event) {
    // Reutilizar la preferencia de cerrar a la bandeja del proyecto activo.
    if (auto* project = currentProject()) QApplication::sendEvent(project, event);
    else QMainWindow::closeEvent(event);
}
} // namespace qaflow
