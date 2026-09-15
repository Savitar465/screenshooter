#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <memory>

class QSaveFile;

namespace qaflow {

/// Escritor de ficheros ZIP mínimo: sólo lo que hace falta para armar un .docx (que es un ZIP de
/// partes XML). Las entradas se guardan **sin comprimir** (método «store»): Word y LibreOffice las
/// leen igual, el documento ocupa poco de todas formas —las imágenes ya vienen comprimidas— y así
/// QAflow no tiene que enlazar zlib ni depender de API privada de Qt.
///
/// Uso: construir con la ruta, `add()` por cada parte y `close()` al final. Si algo falla, `add()` y
/// `close()` devuelven falso y `error()` dice qué pasó; el fichero no se deja a medias.
class ZipWriter {
public:
    explicit ZipWriter(const QString& path);
    ~ZipWriter();
    ZipWriter(const ZipWriter&) = delete;
    ZipWriter& operator=(const ZipWriter&) = delete;

    bool isOpen() const;
    /// Añade una entrada con ese nombre dentro del ZIP ("word/document.xml").
    bool add(const QString& name, const QByteArray& data);
    /// Escribe el directorio central y cierra el fichero.
    bool close();
    QString error() const { return m_error; }

    /// CRC-32 (el del ZIP y el PNG) de unos datos.
    static quint32 crc32(const QByteArray& data);

private:
    struct Entry {
        QByteArray name;
        quint32 crc = 0;
        quint32 size = 0;
        quint32 offset = 0;
    };

    bool write(const QByteArray& bytes);

    std::unique_ptr<QSaveFile> m_file;
    QList<Entry> m_entries;
    quint32 m_offset = 0;
    quint16 m_dosTime = 0;
    quint16 m_dosDate = 0;
    QString m_error;
    bool m_closed = false;
};

} // namespace qaflow
