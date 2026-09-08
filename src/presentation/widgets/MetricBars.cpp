#include "MetricBars.h"

#include "presentation/theme/Theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace qaflow {

// ---- RateBar -------------------------------------------------------------------------------

RateBar::RateBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(10);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void RateBar::setCounts(int passed, int failed, int blocked, int notRun) {
    m_passed = passed; m_failed = failed; m_blocked = blocked; m_notRun = notRun;
    update();
}

void RateBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath clip;
    clip.addRoundedRect(rect(), 5, 5);
    p.setClipPath(clip);
    p.fillRect(rect(), QColor(theme::Border));
    const int total = m_passed + m_failed + m_blocked + m_notRun;
    if (total <= 0) return;
    const std::pair<int, QString> parts[] = {{m_passed, theme::Green}, {m_failed, theme::Red}, {m_blocked, theme::Amber}};
    qreal x = 0;
    for (const auto& [n, color] : parts) {
        if (n <= 0) continue;
        const qreal w = width() * static_cast<qreal>(n) / total;
        p.fillRect(QRectF(x, 0, w, height()), QColor(color));
        x += w;
    }
}

// ---- TrendChart ----------------------------------------------------------------------------

namespace {
constexpr int kBarGap = 10;
constexpr int kMaxBarWidth = 48;
constexpr int kLabelHeight = 30;
constexpr int kTopPad = 22;
} // namespace

TrendChart::TrendChart(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(170);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);
}

void TrendChart::setCycles(const QList<CycleMetrics>& cycles) {
    m_cycles = cycles;
    m_hover = -1;
    update();
}

QRect TrendChart::barRect(int i) const {
    const int n = m_cycles.size();
    if (n == 0) return {};
    const int available = width() - kBarGap * (n + 1);
    const int barW = std::min(kMaxBarWidth, std::max(8, available / n));
    const int totalW = n * barW + (n - 1) * kBarGap;
    const int x0 = (width() - totalW) / 2;
    const int chartH = height() - kLabelHeight - kTopPad;
    const int h = std::max(3, chartH * m_cycles[i].successRate() / 100);
    return QRect(x0 + i * (barW + kBarGap), kTopPad + chartH - h, barW, h);
}

int TrendChart::indexAt(const QPoint& pt) const {
    for (int i = 0; i < m_cycles.size(); ++i) {
        QRect r = barRect(i);
        r.setTop(kTopPad);
        r.setBottom(height());
        if (r.contains(pt)) return i;
    }
    return -1;
}

void TrendChart::mousePressEvent(QMouseEvent* e) {
    const int i = indexAt(e->pos());
    if (i >= 0) emit cycleClicked(m_cycles[i].planRunId);
}

void TrendChart::mouseMoveEvent(QMouseEvent* e) {
    const int i = indexAt(e->pos());
    if (i != m_hover) { m_hover = i; setCursor(i >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor); update(); }
}

void TrendChart::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int chartH = height() - kLabelHeight - kTopPad;
    // Rejilla: 0, 50 y 100 %
    p.setPen(QPen(QColor(theme::Border), 1, Qt::DashLine));
    for (int pct : {0, 50, 100}) {
        const int y = kTopPad + chartH - chartH * pct / 100;
        p.drawLine(0, y, width(), y);
    }
    QFont small = p.font();
    small.setPixelSize(10);
    small.setBold(true);
    p.setFont(small);
    if (m_cycles.isEmpty()) {
        p.setPen(QColor(theme::Muted));
        p.drawText(rect(), Qt::AlignCenter, tr("Termina al menos un ciclo de un plan para ver su evolución."));
        return;
    }
    for (int i = 0; i < m_cycles.size(); ++i) {
        const CycleMetrics& c = m_cycles[i];
        const QRect r = barRect(i);
        const int rate = c.successRate();
        QColor color(rate >= 80 ? theme::Green : rate >= 50 ? theme::Amber : theme::Red);
        if (i == m_hover) color = color.lighter(115);
        QPainterPath path;
        path.addRoundedRect(r, 4, 4);
        p.fillPath(path, color);
        // Cobertura: cuánto del plan se ejecutó, como borde punteado hasta el 100 % si no fue todo.
        if (c.coverage() < 100) {
            p.setPen(QPen(QColor(theme::Muted), 1, Qt::DotLine));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRect(r.left(), kTopPad, r.width(), chartH), 4, 4);
        }
        p.setPen(QColor(theme::Text));
        p.drawText(QRect(r.left() - 12, r.top() - 18, r.width() + 24, 16), Qt::AlignCenter, QStringLiteral("%1 %").arg(rate));
        p.setPen(QColor(theme::Muted));
        const QString date = c.finishedAt.toString(QStringLiteral("dd/MM"));
        p.drawText(QRect(r.left() - 16, kTopPad + chartH + 4, r.width() + 32, 12), Qt::AlignCenter, date);
        p.drawText(QRect(r.left() - 16, kTopPad + chartH + 16, r.width() + 32, 12), Qt::AlignCenter, QStringLiteral("%1/%2").arg(c.executed).arg(c.total));
    }
}

} // namespace qaflow
