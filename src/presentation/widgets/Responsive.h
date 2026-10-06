#pragma once

#include <QList>
#include <QWidget>

class QBoxLayout;
class QFrame;
class QGridLayout;

namespace qaflow {

/// Rejilla que pone tantas columnas como quepan (cada una de al menos `minItemWidth`, hasta
/// `maxColumns`) y las rehace al cambiar de ancho. Su ancho mínimo es el de una sola columna: en una
/// ventana estrecha los elementos bajan de fila en vez de salirse por la derecha.
class AutoGrid : public QWidget {
    Q_OBJECT
public:
    explicit AutoGrid(int minItemWidth, int maxColumns, int spacing = 12, QWidget* parent = nullptr);
    void addWidget(QWidget* w);
    int columns() const { return m_columns; }

    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    int columnsFor(int width) const;
    void relayout(int columns);

    QGridLayout* m_grid;
    QList<QWidget*> m_items;
    int m_minItemWidth;
    int m_maxColumns;
    int m_columns = 0;
};

/// Dos paneles —uno lateral de ancho acotado y el principal— lado a lado si caben, y uno encima del
/// otro si el ancho baja de `breakpoint`. Su ancho mínimo es el de la disposición apilada, así que
/// nunca obliga a su contenedor a ensancharse.
class AdaptiveSplit : public QWidget {
    Q_OBJECT
public:
    AdaptiveSplit(QWidget* side, QWidget* main, int sideWidth, int breakpoint, QWidget* parent = nullptr);
    bool isStacked() const { return m_stacked; }

    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    void setStacked(bool stacked);

    QBoxLayout* m_box;
    QWidget* m_side;
    QWidget* m_main;
    QFrame* m_divider;
    int m_sideWidth;
    int m_breakpoint;
    bool m_stacked = false;
};

} // namespace qaflow
