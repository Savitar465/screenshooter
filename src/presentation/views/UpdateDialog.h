#pragma once

#include "core/models/Update.h"

#include <QDialog>

class QLabel;
class QProgressBar;
class QPushButton;
class QStackedWidget;

namespace qaflow {

class UpdateService;

/// Aviso de una versión nueva, como en los IDE de JetBrains: cuál es, cuál se tiene y sus novedades.
/// Donde QAflow sabe instalarse sola, "Actualizar" descarga y comprueba el paquete aquí mismo y después
/// se elige reiniciar ya o instalarla al cerrar; si no, "Descargar" abre la página de la versión.
///
/// No instala ni cierra nada: quien lo abre actúa según `choice()`.
class UpdateDialog : public QDialog {
    Q_OBJECT
public:
    enum class Choice { Later, Skip, Download, RestartNow, InstallOnExit };

    /// `restartBlocker`, si no está vacío, dice por qué ahora no se puede reiniciar (una grabación en
    /// curso…): entonces sólo se ofrece instalarla al cerrar.
    UpdateDialog(UpdateService& updates, const UpdateRelease& release, const QString& restartBlocker = {}, QWidget* parent = nullptr);

    Choice choice() const { return m_choice; }

protected:
    void reject() override;

private:
    void refresh();
    void choose(Choice c);

    UpdateService& m_updates;
    UpdateRelease m_release;
    Choice m_choice = Choice::Later;

    QStackedWidget* m_footer;
    QWidget* m_offerPage;
    QWidget* m_progressPage;
    QWidget* m_readyPage;
    QWidget* m_scheduledPage;
    QLabel* m_error;
    QPushButton* m_primary;
    QPushButton* m_manual;
    QProgressBar* m_progress;
    QLabel* m_progressText;
    QPushButton* m_restart;
};

} // namespace qaflow
