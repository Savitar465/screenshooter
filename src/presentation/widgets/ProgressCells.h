#pragma once

#include <QStringList>
#include <QWidget>

namespace qaflow {

/// Fila de celdas coloreadas: una por paso de la ejecución.
class ProgressCells : public QWidget {
    Q_OBJECT
public:
    explicit ProgressCells(QWidget* parent = nullptr);
    void setColors(const QStringList& colors);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    QStringList m_colors;
};

} // namespace qaflow
