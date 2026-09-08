#pragma once

#include "core/models/Metrics.h"

#include <QWidget>

namespace qaflow {

/// Barra apilada de resultados (superados / fallidos / bloqueados / sin ejecutar) de una suite.
class RateBar : public QWidget {
    Q_OBJECT
public:
    explicit RateBar(QWidget* parent = nullptr);
    void setCounts(int passed, int failed, int blocked, int notRun);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    int m_passed = 0, m_failed = 0, m_blocked = 0, m_notRun = 0;
};

/// Evolución de la tasa de éxito entre ciclos: una barra por ciclo terminado, en orden cronológico.
class TrendChart : public QWidget {
    Q_OBJECT
public:
    explicit TrendChart(QWidget* parent = nullptr);
    void setCycles(const QList<CycleMetrics>& cycles);

signals:
    void cycleClicked(const QString& planRunId);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;

private:
    int indexAt(const QPoint& p) const;
    QRect barRect(int i) const;

    QList<CycleMetrics> m_cycles;
    int m_hover = -1;
};

} // namespace qaflow
