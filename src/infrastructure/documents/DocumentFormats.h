#pragma once

#include <QByteArray>
#include <QString>
#include <optional>

/// Lectura del texto de los formatos de documento que llegan como adjuntos de un requerimiento. Funciones
/// puras sobre bytes: los formatos que necesitan un programa externo (PDF, Word antiguo) los resuelve
/// DocumentReader.
namespace qaflow::documents {

enum class Format {
    Pdf,
    Docx,          // Word 2007+ (también .docm): ZIP con word/document.xml
    Odt,           // OpenDocument: ZIP con content.xml
    LegacyOffice,  // .doc, .rtf…: sólo LibreOffice los convierte
    Text,
    Html,
    Unknown,
};

/// Por la extensión y, si no la hay o no se conoce, por la firma del contenido.
Format formatOf(const QString& fileName, const QByteArray& data);

/// ¿Se puede descomprimir un ZIP con compresión (deflate)? Depende de que se haya compilado con zlib.
bool canInflate();

/// Una entrada de un fichero ZIP ("word/document.xml"), descomprimida. nullopt si no está o no se puede
/// leer, con el motivo en `error`.
std::optional<QByteArray> zipEntry(const QByteArray& zip, const QString& name, QString* error = nullptr);

/// El texto de word/document.xml: un párrafo por línea, las celdas de una tabla separadas por « | ».
QString wordText(const QByteArray& documentXml);
/// El texto del content.xml de un OpenDocument, con el mismo criterio.
QString odtText(const QByteArray& contentXml);
/// El texto visible de una página HTML.
QString htmlText(const QByteArray& html);
/// Texto de un fichero de texto: UTF-8 si lo es, si no Latin-1 (lo habitual en los sistemas antiguos).
QString plainText(const QByteArray& data);

/// Deja el texto listo para un prompt: sin espacios al final de línea ni más de una línea en blanco seguida.
QString tidy(const QString& text);

} // namespace qaflow::documents
