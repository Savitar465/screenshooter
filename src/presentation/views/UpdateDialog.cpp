#include "UpdateDialog.h"

#include "application/UpdateService.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/Ui.h"

#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QTextBrowser>

namespace qaflow {

UpdateDialog::UpdateDialog(UpdateService& updates, const UpdateRelease& release, const QString& restartBlocker, QWidget* parent)
    : QDialog(parent), m_updates(updates), m_release(release) {
    setObjectName(QStringLiteral("updateDialog"));
    setWindowTitle(tr("Actualización disponible"));
    setModal(true);
    setMinimumWidth(560);

    auto* v = ui::vbox(this, 18, 12);
    auto* intro = ui::label(tr("<b>QAflow %1</b> está disponible. Tienes la %2.")
                                .arg(release.version.toString(), updates.currentVersion().toString()));
    intro->setWordWrap(true);
    v->addWidget(intro);
    if (release.publishedAt.isValid())
        v->addWidget(ui::label(tr("Publicada el %1").arg(QLocale().toString(release.publishedAt.toLocalTime().date(), QLocale::LongFormat)),
                               "muted-sm"));

    v->addWidget(ui::label(tr("NOVEDADES"), "eyebrow"));
    auto* notes = new QTextBrowser;
    notes->setObjectName(QStringLiteral("updateNotes"));
    notes->setOpenExternalLinks(true);
    if (release.notes.trimmed().isEmpty()) notes->setPlainText(tr("Esta versión no trae notas."));
    else notes->setMarkdown(release.notes);
    notes->setMinimumHeight(240);
    v->addWidget(notes, 1);

    m_error = ui::label(QString(), "muted-sm");
    m_error->setObjectName(QStringLiteral("updateError"));
    m_error->setWordWrap(true);
    m_error->setStyleSheet(QStringLiteral("color:%1;").arg(theme::Red));
    v->addWidget(m_error);

    m_footer = new QStackedWidget;
    v->addWidget(m_footer);

    // Ofrecer la versión: omitirla, dejarla para luego o ir a por ella.
    m_offerPage = new QWidget;
    auto* oh = ui::hbox(m_offerPage, 0, 10);
    auto* skip = ui::button(tr("Omitir esta versión"), "ghost");
    skip->setObjectName(QStringLiteral("updateSkip"));
    skip->setToolTip(tr("No volver a avisar de la %1; sí de las siguientes").arg(release.version.toString()));
    connect(skip, &QPushButton::clicked, this, [this]() { choose(Choice::Skip); });
    oh->addWidget(skip);
    oh->addStretch(1);
    auto* later = ui::button(tr("Recordar más tarde"), "ghost");
    later->setObjectName(QStringLiteral("updateLater"));
    connect(later, &QPushButton::clicked, this, &QDialog::reject);
    oh->addWidget(later);
    m_manual = ui::button(tr("Descargar a mano"), "ghost");
    m_manual->setObjectName(QStringLiteral("updateManual"));
    m_manual->setToolTip(tr("Abrir la página de la versión, con los paquetes de cada sistema"));
    connect(m_manual, &QPushButton::clicked, this, [this]() { choose(Choice::Download); });
    oh->addWidget(m_manual);
    m_primary = ui::button(QString(), "primary");
    m_primary->setObjectName(QStringLiteral("updateDownload"));
    m_primary->setDefault(true);
    connect(m_primary, &QPushButton::clicked, this, [this]() {
        if (m_updates.canInstall()) m_updates.downloadUpdate();
        else choose(Choice::Download);
    });
    oh->addWidget(m_primary);
    m_footer->addWidget(m_offerPage);

    // Descargando y comprobando.
    m_progressPage = new QWidget;
    auto* ph = ui::hbox(m_progressPage, 0, 10);
    m_progress = new QProgressBar;
    m_progress->setObjectName(QStringLiteral("updateProgress"));
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(6);
    ph->addWidget(m_progress, 1);
    m_progressText = ui::label(QString(), "muted-sm");
    ph->addWidget(m_progressText);
    auto* cancel = ui::button(tr("Cancelar"), "ghost");
    cancel->setObjectName(QStringLiteral("updateCancel"));
    connect(cancel, &QPushButton::clicked, this, [this]() { m_updates.cancelDownload(); });
    ph->addWidget(cancel);
    m_footer->addWidget(m_progressPage);
    connect(&m_updates, &UpdateService::downloadProgress, this, [this](qint64 received, qint64 total) {
        const double mb = 1024.0 * 1024.0;
        if (total > 0) {
            m_progress->setRange(0, 1000);
            m_progress->setValue(int(received * 1000 / total));
            m_progressText->setText(tr("%1 de %2 MB").arg(received / mb, 0, 'f', 1).arg(total / mb, 0, 'f', 1));
        } else {
            m_progress->setRange(0, 0);
            m_progressText->setText(tr("%1 MB").arg(received / mb, 0, 'f', 1));
        }
    });

    // Descargada y comprobada: reiniciar ya o al cerrar.
    m_readyPage = new QWidget;
    auto* rh = ui::hbox(m_readyPage, 0, 10);
    rh->addWidget(ui::label(tr("Descargada y comprobada."), "muted-sm"));
    rh->addStretch(1);
    auto* onExit = ui::button(tr("Al cerrar QAflow"), "ghost");
    onExit->setObjectName(QStringLiteral("updateOnExit"));
    onExit->setToolTip(tr("Seguir trabajando: la versión nueva se instala cuando cierres QAflow"));
    connect(onExit, &QPushButton::clicked, this, [this]() { choose(Choice::InstallOnExit); });
    rh->addWidget(onExit);
    m_restart = ui::button(tr("Reiniciar ahora"), "primary");
    m_restart->setObjectName(QStringLiteral("updateRestart"));
    m_restart->setEnabled(restartBlocker.isEmpty());
    m_restart->setToolTip(restartBlocker.isEmpty() ? tr("Cerrar QAflow, instalar la versión nueva y volver a abrirla") : restartBlocker);
    connect(m_restart, &QPushButton::clicked, this, [this]() { choose(Choice::RestartNow); });
    rh->addWidget(m_restart);
    m_footer->addWidget(m_readyPage);

    // Ya instalada (o programada): sólo queda cerrar QAflow.
    m_scheduledPage = new QWidget;
    auto* sh = ui::hbox(m_scheduledPage, 0, 10);
    sh->addWidget(ui::label(tr("La versión nueva se instala al cerrar QAflow."), "muted-sm"));
    sh->addStretch(1);
    auto* close = ui::button(tr("Cerrar"), "primary");
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    sh->addWidget(close);
    m_footer->addWidget(m_scheduledPage);

    connect(&m_updates, &UpdateService::installChanged, this, &UpdateDialog::refresh);
    refresh();
}

void UpdateDialog::refresh() {
    using IS = UpdateService::InstallState;
    const IS state = m_updates.installState();
    const bool installable = m_updates.canInstall();
    m_error->setVisible(state == IS::Failed);
    m_error->setText(m_updates.installError());
    switch (state) {
        case IS::Downloading:
            m_progress->setRange(0, 0);
            m_progressText->setText(tr("Descargando…"));
            m_footer->setCurrentWidget(m_progressPage);
            break;
        case IS::Ready:
            m_footer->setCurrentWidget(m_readyPage);
            break;
        case IS::Scheduled:
            m_footer->setCurrentWidget(m_scheduledPage);
            break;
        case IS::Idle:
        case IS::Failed:
            m_primary->setText(!installable ? tr("Descargar") : state == IS::Failed ? tr("Reintentar") : tr("Actualizar"));
            m_primary->setToolTip(installable ? tr("Descargar la versión nueva, comprobar que es la publicada e instalarla (%1)")
                                                    .arg(m_updates.installKind())
                                              : tr("Abrir la página de la versión, con los paquetes de cada sistema"));
            // Sin instalación automática, el botón principal ya es la descarga a mano.
            m_manual->setVisible(installable && state == IS::Failed);
            m_footer->setCurrentWidget(m_offerPage);
            break;
    }
}

void UpdateDialog::choose(Choice c) {
    m_choice = c;
    accept();
}

void UpdateDialog::reject() {
    // Cerrar mientras se descarga la corta: no se queda nada a medias en segundo plano.
    m_updates.cancelDownload();
    m_choice = Choice::Later;
    QDialog::reject();
}

} // namespace qaflow
