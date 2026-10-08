#pragma once

#include <QList>
#include <QSplitter>
#include <QWidget>

#include <functional>

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

/// Lista lateral a la izquierda y panel principal a la derecha, separados por un borde que se
/// arrastra (manteniendo el clic) para ensanchar o estrechar la lista. El ancho elegido se recuerda en
/// `settingsKey`. La lista nunca pasa de la mitad del ancho disponible, así que el panel principal
/// conserva sitio aunque la ventana se estreche.
class SideSplitter : public QSplitter {
    Q_OBJECT
public:
    SideSplitter(const QString& objectName, QString settingsKey, int defaultSideWidth, QWidget* parent = nullptr);
    /// Pone los dos paneles y les da el ancho recordado. Se llama una vez, con los paneles ya construidos.
    void setPanes(QWidget* side, QWidget* main);

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    /// Devuelve la lista a la mitad del ancho si se pasó (al arrastrar o al estrecharse la ventana).
    void keepSideWithinHalf();

    QString m_settingsKey;
    int m_defaultSideWidth;
};

/// Avisa con `narrow == true` cuando el ancho de `w` baja de `breakpoint`, y con `false` cuando vuelve
/// a llegar; también una vez al principio, con el ancho que tenga entonces. Sirve para recolocar
/// partes de un panel según su propio ancho —no el de la ventana—, que cambia también al arrastrar
/// el borde de un `SideSplitter`.
void onBreakpoint(QWidget* w, int breakpoint, std::function<void(bool narrow)> changed);

} // namespace qaflow
