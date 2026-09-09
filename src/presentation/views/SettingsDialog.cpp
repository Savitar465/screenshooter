#include "SettingsDialog.h"

#include "presentation/views/SettingsView.h"
#include "presentation/widgets/Ui.h"

#include <QApplication>
#include <QPushButton>
#include <QScreen>

#include <algorithm>

namespace qaflow {

SettingsDialog::SettingsDialog(SettingsStore& settings, BugReportService& bugs, IGlobalHotkey* hotkey,
                               const QString& captureBackend, TestPublishService* publish, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("settingsDialog"));
    setWindowTitle(tr("Ajustes · QAflow"));
    setWindowIcon(ui::appIcon());
    setModal(false);
    setMinimumSize(640, 480);
    // La página de ajustes mide 760 px; el resto es margen y barra de desplazamiento.
    if (QScreen* s = parent ? parent->screen() : QApplication::primaryScreen()) {
        const QSize avail = s->availableSize();
        resize(std::min(880, avail.width() - 80), std::min(780, avail.height() - 80));
    }

    auto* v = ui::vbox(this, 0, 0);
    auto* view = new SettingsView(settings, bugs, hotkey, captureBackend, publish);
    connect(view, &SettingsView::toast, this, &SettingsDialog::toast);
    v->addWidget(view, 1);

    // Pie: los ajustes se guardan según se editan, así que sólo hace falta cerrar.
    auto* footer = ui::card("dialog-footer");
    auto* h = ui::hbox(footer, 14, 8);
    h->addWidget(ui::label(tr("Los cambios se guardan al momento"), "muted-sm"));
    h->addStretch(1);
    auto* close = ui::button(tr("Cerrar"), "primary");
    close->setObjectName(QStringLiteral("settingsClose"));
    close->setDefault(true);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    h->addWidget(close);
    v->addWidget(footer);
}

} // namespace qaflow
