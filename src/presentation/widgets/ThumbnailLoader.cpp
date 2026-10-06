#include "ThumbnailLoader.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QPixmapCache>
#include <QPointer>
#include <QThread>

#include <algorithm>

namespace qaflow {

ThumbnailLoader& ThumbnailLoader::instance() {
    // Colgado de qApp: se destruye antes que la aplicación y espera a las lecturas en curso.
    static QPointer<ThumbnailLoader> loader;
    if (!loader) loader = new ThumbnailLoader(QCoreApplication::instance());
    return *loader;
}

ThumbnailLoader::ThumbnailLoader(QObject* parent) : QObject(parent) {
    // Pocos hilos: en un equipo modesto, leer muchas imágenes a la vez compite con la propia interfaz.
    m_pool.setMaxThreadCount(std::clamp(QThread::idealThreadCount() - 1, 1, 2));
    // Una miniatura ocupa ~0,5 MB; la caché por defecto (10 MB) no llega para un informe mediano.
    QPixmapCache::setCacheLimit(std::max(QPixmapCache::cacheLimit(), 64 * 1024));
}

ThumbnailLoader::~ThumbnailLoader() {
    m_pool.clear();
    m_pool.waitForDone();
}

QString ThumbnailLoader::keyFor(const QString& path) {
    const QFileInfo fi(path);
    if (!fi.exists()) return {};
    return QStringLiteral("qaflow-thumb:%1:%2:%3").arg(fi.absoluteFilePath()).arg(fi.size()).arg(fi.lastModified().toMSecsSinceEpoch());
}

bool ThumbnailLoader::find(const QString& key, QPixmap* out) const {
    return !key.isEmpty() && QPixmapCache::find(key, out);
}

void ThumbnailLoader::request(const QString& path, const QString& key) {
    if (key.isEmpty() || m_pending.contains(key) || m_failed.contains(key)) return;
    m_pending.insert(key);
    // El destructor espera a las lecturas en curso, así que `this` sigue vivo mientras corren; y si
    // muere con el aviso ya encolado, Qt descarta la llamada.
    m_pool.start([this, path, key]() {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        const QSize full = reader.size();
        // Los formatos que lo admiten (JPEG) se decodifican ya reducidos; el resto se reduce aquí,
        // fuera del hilo de la interfaz.
        if (full.isValid()) reader.setScaledSize(full.scaled(maxSize(), Qt::KeepAspectRatio).expandedTo({1, 1}));
        QImage image = reader.read();
        if (!image.isNull() && (image.width() > maxSize().width() || image.height() > maxSize().height()))
            image = image.scaled(maxSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QMetaObject::invokeMethod(this, [this, key, image]() { finish(key, image); }, Qt::QueuedConnection);
    });
}

void ThumbnailLoader::finish(const QString& key, const QImage& image) {
    m_pending.remove(key);
    // QPixmap sólo se puede crear en el hilo de la interfaz: la conversión se hace aquí.
    if (image.isNull()) m_failed.insert(key);
    else QPixmapCache::insert(key, QPixmap::fromImage(image));
    emit loaded(key);
}

} // namespace qaflow
