#pragma once

#include <QLabel>

namespace qaflow {

/// Etiqueta que recorta con «…» según el ancho que le toca, no por número de letras: no ensancha a su
/// contenedor por largo que sea el texto. El texto entero va en el tooltip. Es de una línea; con
/// `setMaxLines` parte el texto en varias y recorta la última.
class ElidedLabel : public QLabel {
    Q_OBJECT
public:
    explicit ElidedLabel(const QString& text = QString(), QWidget* parent = nullptr);

    void setFullText(const QString& text);
    const QString& fullText() const { return m_full; }
    /// Cuántas líneas puede ocupar como mucho (1 por defecto); lo que no cabe acaba en «…».
    void setMaxLines(int lines);

    QSize sizeHint() const override;   // lo que ocupa el texto entero: hasta ahí puede crecer
    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    void updateElided();
    QString m_full;
    int m_maxLines = 1;
};

} // namespace qaflow
