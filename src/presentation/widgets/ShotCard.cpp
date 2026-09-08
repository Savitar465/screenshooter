#include "ShotCard.h"

#include "presentation/theme/Theme.h"
#include "presentation/widgets/Thumbnail.h"
#include "presentation/widgets/Ui.h"

#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QComboBox>
#include <QGridLayout>
#include <QMenu>

namespace qaflow {

namespace {
QComboBox* stepCombo(const Screenshot& shot, const QList<TestStep>& steps, QWidget* parent) {
    auto* cb = new QComboBox(parent);
    cb->setProperty("role", QStringLiteral("small"));
    cb->addItem(QCoreApplication::translate("ShotCard", "Sin asignar"), 0);
    for (int i = 0; i < steps.size(); ++i)
        cb->addItem(QCoreApplication::translate("ShotCard", "Paso %1 · %2").arg(i + 1).arg(ui::elide(steps[i].action, 28)), i + 1);
    cb->setCurrentIndex(std::max(0, cb->findData(shot.step)));
    return cb;
}
} // namespace

ShotCard::ShotCard(const Screenshot& shot, const QList<TestStep>& steps, Layout layout, QWidget* parent) : QFrame(parent), m_shot(shot) {
    const int id = shot.id;
    const bool editable = shot.isImage() && !shot.isAnimation();
    auto* name = ui::label(shot.fileName, "mono-muted");
    name->setProperty("role", QStringLiteral("mono-muted"));
    name->setToolTip(shot.fileName);
    auto* remove = ui::button(QStringLiteral("×"), "icon");
    remove->setToolTip(tr("Eliminar"));
    connect(remove, &QPushButton::clicked, this, [this, id]() { emit removeRequested(id); });
    m_thumb = new Thumbnail(shot.path, shot.step, id);
    connect(m_thumb, &Thumbnail::clicked, this, [this, id]() { emit openRequested(id); });
    auto* annotate = ui::button(QStringLiteral("✎"), "icon-move");
    annotate->setToolTip(tr("Anotar (flechas, rectángulos, texto, difuminado)"));
    annotate->setVisible(editable);
    connect(annotate, &QPushButton::clicked, this, [this, id]() { emit annotateRequested(id); });

    if (layout == Layout::Compact) {
        setStyleSheet(QStringLiteral("QFrame{background:%1;border:1px solid %2;border-radius:8px;}").arg(theme::Field, theme::Border));
        setFixedWidth(140);
        auto* v = ui::vbox(this, 0, 0);
        m_thumb->setWidthHint(138);
        v->addWidget(m_thumb);
        auto* bottom = new QWidget;
        auto* h = ui::hbox(bottom, 0, 4);
        h->setContentsMargins(8, 5, 4, 5);
        name->setStyleSheet(QStringLiteral("font-size:11px;font-weight:400;"));
        h->addWidget(name, 1);
        annotate->setStyleSheet(QStringLiteral("font-size:11px;padding:1px 4px;"));
        h->addWidget(annotate);
        remove->setStyleSheet(QStringLiteral("font-size:13px;"));
        h->addWidget(remove);
        v->addWidget(bottom);
        return;
    }

    auto* combo = stepCombo(shot, steps, this);
    connect(combo, &QComboBox::currentIndexChanged, this, [this, combo, id](int) { emit stepChanged(id, combo->currentData().toInt()); });

    if (layout == Layout::Grid) {
        setStyleSheet(QStringLiteral("QFrame{background:%1;border:1px solid %2;border-radius:10px;}").arg(theme::Panel, theme::Border));
        auto* v = ui::vbox(this, 0, 0);
        m_thumb->setWidthHint(200);
        v->addWidget(m_thumb);
        auto* bottom = new QWidget;
        auto* bv = ui::vbox(bottom, 8, 6);
        bv->addWidget(combo);
        auto* row = new QWidget;
        auto* h = ui::hbox(row, 0, 4);
        name->setStyleSheet(QStringLiteral("font-weight:400;"));
        h->addWidget(name, 1);
        h->addWidget(annotate);
        auto* up = ui::button(QStringLiteral("◀"), "icon-move");
        up->setToolTip(tr("Mover antes"));
        auto* down = ui::button(QStringLiteral("▶"), "icon-move");
        down->setToolTip(tr("Mover después"));
        connect(up, &QPushButton::clicked, this, [this, id]() { emit moveRequested(id, -1); });
        connect(down, &QPushButton::clicked, this, [this, id]() { emit moveRequested(id, +1); });
        h->addWidget(up);
        h->addWidget(down);
        remove->setStyleSheet(QStringLiteral("font-size:14px;"));
        h->addWidget(remove);
        bv->addWidget(row);
        v->addWidget(bottom);
        return;
    }

    // Layout::Row
    setStyleSheet(QStringLiteral("QFrame{background:%1;border:1px solid %2;border-radius:8px;}").arg(theme::Field, theme::Border));
    auto* g = new QGridLayout(this);
    g->setContentsMargins(6, 6, 6, 6);
    g->setHorizontalSpacing(10);
    g->setVerticalSpacing(2);
    m_thumb->setWidthHint(72);
    m_thumb->setFixedWidth(72);
    g->addWidget(m_thumb, 0, 0, 3, 1, Qt::AlignVCenter);
    auto* nameRow = new QWidget;
    auto* nh = ui::hbox(nameRow, 0, 4);
    name->setStyleSheet(QStringLiteral("font-weight:400;"));
    nh->addWidget(name, 1);
    annotate->setStyleSheet(QStringLiteral("font-size:10px;padding:0 4px;"));
    nh->addWidget(annotate);
    g->addWidget(nameRow, 0, 1);
    combo->setStyleSheet(QStringLiteral("QComboBox{background:%1;font-size:11.5px;}").arg(theme::Panel));
    g->addWidget(combo, 1, 1, 2, 1, Qt::AlignTop);
    auto* up = ui::button(QStringLiteral("▲"), "icon-plain");
    up->setToolTip(tr("Subir"));
    auto* down = ui::button(QStringLiteral("▼"), "icon-plain");
    down->setToolTip(tr("Bajar"));
    connect(up, &QPushButton::clicked, this, [this, id]() { emit moveRequested(id, -1); });
    connect(down, &QPushButton::clicked, this, [this, id]() { emit moveRequested(id, +1); });
    remove->setStyleSheet(QStringLiteral("font-size:13px;"));
    g->addWidget(up, 0, 2);
    g->addWidget(remove, 1, 2);
    g->addWidget(down, 2, 2);
    g->setColumnStretch(1, 1);
}

void ShotCard::reloadThumbnail() { if (m_thumb) m_thumb->reload(); }

void ShotCard::contextMenuEvent(QContextMenuEvent* e) {
    const int id = m_shot.id;
    QMenu menu(this);
    menu.addAction(tr("Abrir"), this, [this, id]() { emit openRequested(id); });
    if (m_shot.isImage() && !m_shot.isAnimation()) menu.addAction(tr("Anotar…"), this, [this, id]() { emit annotateRequested(id); });
    if (m_shot.isImage()) menu.addAction(tr("Copiar imagen"), this, [this, id]() { emit copyRequested(id); });
    menu.addAction(tr("Mostrar en la carpeta"), this, [this, id]() { emit openFolderRequested(id); });
    menu.addSeparator();
    menu.addAction(tr("Eliminar"), this, [this, id]() { emit removeRequested(id); });
    menu.exec(e->globalPos());
}

} // namespace qaflow
