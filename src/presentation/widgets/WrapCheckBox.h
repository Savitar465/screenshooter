#pragma once

#include <QCheckBox>

namespace qaflow {

/// Casilla cuyo texto se parte en líneas cuando no le cabe, como una etiqueta con `setWordWrap`: no
/// obliga a su contenedor a medir lo que mide la frase entera. Pulsar el texto también la marca.
class WrapCheckBox : public QCheckBox {
public:
    explicit WrapCheckBox(const QString& text, QWidget* parent = nullptr);

    QSize minimumSizeHint() const override;   // la palabra más larga
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override;

protected:
    void paintEvent(QPaintEvent* e) override;
    bool hitButton(const QPoint& pos) const override { return rect().contains(pos); }

private:
    /// Lo que ocupan la casilla y su separación, a la izquierda del texto.
    int indicatorSpace() const;
};

} // namespace qaflow
