#pragma once

#include <QImage>
#include <QString>
#include <QVector>

#include <cstdint>
#include <memory>

class QIODevice;

namespace qaflow {

/// Escribe un GIF89a animado fotograma a fotograma, sin dependencias externas.
/// Cada fotograma lleva su propia paleta (median cut sobre un histograma de 15 bits) y se
/// comprime con LZW a medida que llega, así la grabación no acumula imágenes en memoria.
class GifEncoder {
public:
    GifEncoder();
    ~GifEncoder();

    /// Abre `path` y escribe la cabecera. `loop` 0 = repetir indefinidamente.
    bool open(const QString& path, const QSize& size, int loop = 0);
    /// Añade un fotograma (se reescala a `size` si hace falta). `delayMs` es el tiempo hasta el siguiente.
    bool addFrame(const QImage& frame, int delayMs);
    /// Escribe el terminador y cierra. Devuelve false si algo falló en la escritura.
    bool finish();

    int frameCount() const { return m_frames; }
    QSize size() const { return m_size; }
    QString error() const { return m_error; }

    /// Reduce una imagen a 256 colores como mucho. Devuelve la paleta en `palette` y los índices
    /// (uno por píxel, por filas) en `indices`. Expuesto para los tests.
    static void quantize(const QImage& image, QVector<QRgb>* palette, QByteArray* indices);

private:
    void writeLzw(const QByteArray& indices, int minCodeSize);
    bool flush();

    std::unique_ptr<QIODevice> m_device;
    QByteArray m_buffer;
    QSize m_size;
    int m_frames = 0;
    bool m_open = false;
    QString m_error;
};

} // namespace qaflow
