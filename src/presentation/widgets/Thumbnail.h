#pragma once

#include <QPixmap>
#include <QWidget>

namespace qaflow {

/// Miniatura 16:10 de una evidencia con la etiqueta del paso en la esquina. Las imágenes se
/// muestran reducidas; los demás ficheros (logs, vídeos…) con su extensión. Un clic emite
/// `clicked()` (abrir a tamaño completo).
class Thumbnail : public QWidget {
    Q_OBJECT
public:
    explicit Thumbnail(const QString& imagePath, int step, int seed, QWidget* parent = nullptr);
    void setWidthHint(int w);
    /// Vuelve a leer el fichero (tras anotarlo).
    void reload();

    QSize sizeHint() const override;
    int heightForWidth(int w) const override { return w * 10 / 16; }
    bool hasHeightForWidth() const override { return true; }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QString m_path;
    QPixmap m_pixmap;
    QString m_extension;   // sólo para ficheros que no son imagen
    bool m_animation = false;
    int m_step;
    int m_seed;
    int m_widthHint = 200;
    bool m_pressed = false;
};

} // namespace qaflow
