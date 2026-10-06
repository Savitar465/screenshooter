#include "Responsive.h"

#include "presentation/theme/Theme.h"

#include <QBoxLayout>
#include <QFrame>
#include <QGridLayout>
#include <QResizeEvent>

#include <algorithm>

namespace qaflow {

// ---- AutoGrid ------------------------------------------------------------------------------

AutoGrid::AutoGrid(int minItemWidth, int maxColumns, int spacing, QWidget* parent)
    : QWidget(parent), m_grid(new QGridLayout(this)), m_minItemWidth(minItemWidth), m_maxColumns(std::max(1, maxColumns)) {
    m_grid->setContentsMargins(0, 0, 0, 0);
    m_grid->setSpacing(spacing);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
}

void AutoGrid::addWidget(QWidget* w) {
    w->setParent(this);
    m_items.append(w);
    relayout(std::max(1, m_columns));
}

int AutoGrid::columnsFor(int width) const {
    const int spacing = m_grid->horizontalSpacing();
    return std::clamp((width + spacing) / (m_minItemWidth + spacing), 1, m_maxColumns);
}

void AutoGrid::relayout(int columns) {
    m_columns = columns;
    for (auto* w : m_items) m_grid->removeWidget(w);
    for (int c = 0; c < m_maxColumns; ++c) m_grid->setColumnStretch(c, c < columns ? 1 : 0);
    for (int i = 0; i < m_items.size(); ++i) m_grid->addWidget(m_items[i], i / columns, i % columns, Qt::AlignTop);
    updateGeometry();
}

QSize AutoGrid::minimumSizeHint() const {
    // Una sola columna: lo que la rejilla necesita como poco, por estrecho que sea el panel.
    return {m_minItemWidth, QWidget::minimumSizeHint().height()};
}

void AutoGrid::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (const int cols = columnsFor(e->size().width()); cols != m_columns) relayout(cols);
}

// ---- AdaptiveSplit -------------------------------------------------------------------------

AdaptiveSplit::AdaptiveSplit(QWidget* side, QWidget* main, int sideWidth, int breakpoint, QWidget* parent)
    : QWidget(parent), m_box(new QBoxLayout(QBoxLayout::LeftToRight, this)), m_side(side), m_main(main),
      m_divider(new QFrame(this)), m_sideWidth(sideWidth), m_breakpoint(breakpoint) {
    m_box->setContentsMargins(0, 0, 0, 0);
    m_box->setSpacing(0);
    m_divider->setStyleSheet(QStringLiteral("background:%1;border:none;").arg(theme::Border));
    m_box->addWidget(m_side);
    m_box->addWidget(m_divider);
    m_box->addWidget(m_main, 1);
    setStacked(false);
}

void AdaptiveSplit::setStacked(bool stacked) {
    m_stacked = stacked;
    m_box->setDirection(stacked ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    if (stacked) {
        m_side->setMinimumWidth(0);
        m_side->setMaximumWidth(QWIDGETSIZE_MAX);
        m_divider->setFixedSize(QWIDGETSIZE_MAX, 1);
        m_divider->setMinimumWidth(0);
    } else {
        m_side->setFixedWidth(m_sideWidth);
        m_divider->setMinimumHeight(0);
        m_divider->setMaximumHeight(QWIDGETSIZE_MAX);
        m_divider->setFixedWidth(1);
    }
    updateGeometry();
}

QSize AdaptiveSplit::minimumSizeHint() const {
    // El de la disposición apilada: si no, el panel nunca llegaría a estrecharse lo bastante para apilarse.
    const QSize side = m_side->minimumSizeHint(), main = m_main->minimumSizeHint();
    return {std::max(std::min(side.width(), m_sideWidth), main.width()), side.height() + main.height() + 1};
}

void AdaptiveSplit::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    const bool stacked = e->size().width() < m_breakpoint;
    if (stacked != m_stacked) setStacked(stacked);
}

} // namespace qaflow
