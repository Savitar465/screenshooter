#pragma once

#include <QPixmap>
#include <QWidget>

namespace qaflow {

/// Miniatura 16:10 de una evidencia con la etiqueta del paso en la esquina. Las imágenes se
/// muestran reducidas; los demás ficheros (logs, vídeos…) con su extensión. Un clic emite
/// `clicked()` (abrir la evidencia: anotarla si es una imagen fija).
///
/// La imagen no se lee al construir la miniatura sino la primera vez que se enseña, y en segundo
/// plano (`ThumbnailLoader`): mientras tanto se pinta un hueco del mismo tamaño. Así, las tarjetas
/// que nunca llegan a verse (otra pestaña, otro caso del informe) no cuestan nada.
class Thumbnail : public QWidget {
    Q_OBJECT
public:
    explicit Thumbnail(const QString& imagePath, int step, int seed, QWidget* parent = nullptr);
    void setWidthHint(int w);
    /// Vuelve a leer el fichero (tras anotarlo).
    void reload();
    /// Si la imagen ya está lista para pintarse (o no hay imagen que esperar).
    bool isLoaded() const { return !m_loading; }

    QSize sizeHint() const override;
    int heightForWidth(int w) const override { return w * 10 / 16; }
    bool hasHeightForWidth() const override { return true; }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override;
    void showEvent(QShowEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    void requestImage();
    void onLoaded(const QString& key);

    QString m_path;
    QString m_key;           // clave en la caché de miniaturas (cambia si el fichero cambia)
    QPixmap m_pixmap;        // decodificada, a lo sumo ThumbnailLoader::maxSize()
    QPixmap m_scaled;        // recortada al tamaño del widget: se recalcula sólo al cambiar éste
    bool m_loading = false;  // imagen pedida y todavía no recibida
    QString m_extension;   // sólo para ficheros que no son imagen
    bool m_animation = false;
    int m_step;
    int m_seed;
    int m_widthHint = 200;
    bool m_pressed = false;
};

} // namespace qaflow
