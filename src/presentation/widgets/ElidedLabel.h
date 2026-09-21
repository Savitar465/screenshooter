#pragma once

#include <QLabel>

namespace qaflow {

/// Etiqueta de una línea que recorta con «…» según el ancho que le toca, no por número de letras:
/// no ensancha a su contenedor por largo que sea el texto. El texto entero va en el tooltip.
class ElidedLabel : public QLabel {
    Q_OBJECT
public:
    explicit ElidedLabel(const QString& text = QString(), QWidget* parent = nullptr);

    void setFullText(const QString& text);
    const QString& fullText() const { return m_full; }

    QSize minimumSizeHint() const override;

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    void updateElided();
    QString m_full;
};

} // namespace qaflow
