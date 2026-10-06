#pragma once

#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QThreadPool>

namespace qaflow {

/// Decodifica las miniaturas de las evidencias fuera del hilo de la interfaz y las guarda en una caché
/// compartida. Una captura a pantalla completa tarda en leerse (un PNG se descomprime entero aunque
/// se pida reducido), y hacerlo al construir cada tarjeta congelaba la ventana al abrir un informe
/// con muchas evidencias. Cada fichero se lee una sola vez: las pantallas que lo vuelvan a enseñar
/// lo toman de la caché.
class ThumbnailLoader : public QObject {
    Q_OBJECT
public:
    /// El de la aplicación (vive lo que vive `qApp`).
    static ThumbnailLoader& instance();

    /// Clave de caché de un fichero: cambia cuando el fichero cambia (p. ej. al anotarlo), así que
    /// la miniatura vieja no se vuelve a servir. Vacía si el fichero no existe.
    static QString keyFor(const QString& path);

    /// La miniatura, si ya está decodificada.
    bool find(const QString& key, QPixmap* out) const;
    /// Si el fichero se intentó leer y no es una imagen que se pueda decodificar.
    bool failed(const QString& key) const { return m_failed.contains(key); }
    /// Pide la miniatura de `path`; `loaded(key)` avisa cuando está (o cuando falló). Las peticiones
    /// repetidas de la misma clave mientras se decodifica no se duplican.
    void request(const QString& path, const QString& key);

    /// Tamaño máximo al que se decodifica: el de la tarjeta más grande, sin leer la imagen entera.
    static QSize maxSize() { return {480, 300}; }

signals:
    void loaded(const QString& key);

private:
    explicit ThumbnailLoader(QObject* parent);
    ~ThumbnailLoader() override;
    void finish(const QString& key, const QImage& image);

    QThreadPool m_pool;
    QSet<QString> m_pending;
    QSet<QString> m_failed;
};

} // namespace qaflow
