#include "DocumentFormats.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QTextDocumentFragment>
#include <QXmlStreamReader>
#include <algorithm>

#ifdef QAFLOW_HAS_ZLIB
#include <zlib.h>
#endif

namespace qaflow::documents {

namespace {

QString tr(const char* text) { return QCoreApplication::translate("infrastructure", text); }

quint32 le16(const QByteArray& b, qsizetype at) {
    return quint32(quint8(b[at])) | quint32(quint8(b[at + 1])) << 8;
}
quint32 le32(const QByteArray& b, qsizetype at) {
    return le16(b, at) | le16(b, at + 2) << 16;
}

std::optional<QByteArray> inflateRaw(const QByteArray& compressed, quint32 size, QString* error) {
#ifdef QAFLOW_HAS_ZLIB
    QByteArray out(qsizetype(size), Qt::Uninitialized);
    z_stream z{};
    z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.constData()));
    z.avail_in = uInt(compressed.size());
    z.next_out = reinterpret_cast<Bytef*>(out.data());
    z.avail_out = uInt(out.size());
    if (inflateInit2(&z, -MAX_WBITS) != Z_OK) {
        if (error) *error = tr("No se pudo descomprimir el documento");
        return std::nullopt;
    }
    const int status = inflate(&z, Z_FINISH);
    inflateEnd(&z);
    if (status != Z_STREAM_END || z.total_out != size) {
        if (error) *error = tr("El documento está dañado: su contenido comprimido no se puede leer");
        return std::nullopt;
    }
    return out;
#else
    Q_UNUSED(compressed); Q_UNUSED(size);
    if (error) *error = tr("Esta instalación de QAflow no puede descomprimir documentos (compilada sin zlib)");
    return std::nullopt;
#endif
}

/// Cierra una celda de tabla: el párrafo que la terminaba pasa a ser un separador.
void endCell(QString& out) {
    while (out.endsWith(QLatin1Char('\n'))) out.chop(1);
    out += QStringLiteral(" | ");
}
void endRow(QString& out) {
    if (out.endsWith(QStringLiteral(" | "))) out.chop(3);
    out += QLatin1Char('\n');
}

} // namespace

Format formatOf(const QString& fileName, const QByteArray& data) {
    const QString ext = QFileInfo(fileName).suffix().toLower();
    if (ext == QLatin1String("pdf")) return Format::Pdf;
    if (ext == QLatin1String("docx") || ext == QLatin1String("docm") || ext == QLatin1String("dotx")) return Format::Docx;
    if (ext == QLatin1String("odt") || ext == QLatin1String("ott")) return Format::Odt;
    if (ext == QLatin1String("doc") || ext == QLatin1String("dot") || ext == QLatin1String("rtf") || ext == QLatin1String("wpd"))
        return Format::LegacyOffice;
    if (ext == QLatin1String("txt") || ext == QLatin1String("csv") || ext == QLatin1String("md") || ext == QLatin1String("log")
        || ext == QLatin1String("json") || ext == QLatin1String("xml") || ext == QLatin1String("sql"))
        return Format::Text;
    if (ext == QLatin1String("html") || ext == QLatin1String("htm")) return Format::Html;

    // Sin extensión conocida (docDownload.do no siempre la trae): la firma del contenido.
    if (data.startsWith("%PDF")) return Format::Pdf;
    if (data.startsWith("PK\x03\x04")) {
        if (data.contains("word/document.xml")) return Format::Docx;
        if (data.contains("content.xml")) return Format::Odt;
        return Format::Unknown;
    }
    if (data.startsWith("{\\rtf") || data.startsWith("\xD0\xCF\x11\xE0")) return Format::LegacyOffice;
    const QByteArray head = data.left(512).trimmed().toLower();
    if (head.startsWith("<!doctype html") || head.startsWith("<html")) return Format::Html;
    return Format::Unknown;
}

bool canInflate() {
#ifdef QAFLOW_HAS_ZLIB
    return true;
#else
    return false;
#endif
}

std::optional<QByteArray> zipEntry(const QByteArray& zip, const QString& name, QString* error) {
    const auto fail = [error](const QString& why) -> std::optional<QByteArray> {
        if (error) *error = why;
        return std::nullopt;
    };
    // El directorio central se localiza desde el final: el registro que lo cierra puede llevar un
    // comentario de hasta 64 KB detrás.
    qsizetype eocd = -1;
    for (qsizetype at = zip.size() - 22; at >= 0 && at >= zip.size() - 22 - 0xFFFF; --at)
        if (le32(zip, at) == 0x06054b50) { eocd = at; break; }
    if (eocd < 0) return fail(tr("El documento no es un ZIP válido"));

    const quint32 entries = le16(zip, eocd + 10);
    qsizetype at = qsizetype(le32(zip, eocd + 16));
    const QByteArray wanted = name.toUtf8();
    for (quint32 i = 0; i < entries; ++i) {
        if (at + 46 > zip.size() || le32(zip, at) != 0x02014b50) return fail(tr("El índice del documento está dañado"));
        const quint32 method = le16(zip, at + 10);
        const quint32 compressedSize = le32(zip, at + 20);
        const quint32 size = le32(zip, at + 24);
        const quint32 nameLength = le16(zip, at + 28);
        const quint32 extraLength = le16(zip, at + 30);
        const quint32 commentLength = le16(zip, at + 32);
        const qsizetype local = qsizetype(le32(zip, at + 42));
        const QByteArray entryName = zip.mid(at + 46, nameLength);
        at += 46 + nameLength + extraLength + commentLength;
        if (entryName != wanted) continue;

        if (local + 30 > zip.size() || le32(zip, local) != 0x04034b50) return fail(tr("El documento está dañado"));
        const qsizetype start = local + 30 + le16(zip, local + 26) + le16(zip, local + 28);
        if (start + qsizetype(compressedSize) > zip.size()) return fail(tr("El documento está incompleto"));
        const QByteArray stored = zip.mid(start, compressedSize);
        if (method == 0) return stored;
        if (method == 8) return inflateRaw(stored, size, error);
        return fail(tr("El documento usa una compresión que QAflow no sabe leer"));
    }
    return fail(tr("El documento no tiene %1").arg(name));
}

QString wordText(const QByteArray& documentXml) {
    QXmlStreamReader xml(documentXml);
    QString out;
    bool inText = false;
    int inFallback = 0;   // los cuadros de texto van dos veces (mc:Choice y mc:Fallback): se lee sólo uno
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (xml.name() == QLatin1String("Fallback")) {
            if (token == QXmlStreamReader::StartElement) ++inFallback;
            else if (token == QXmlStreamReader::EndElement) inFallback = std::max(0, inFallback - 1);
            continue;
        }
        if (inFallback > 0) continue;
        switch (token) {
            case QXmlStreamReader::StartElement: {
                const auto n = xml.name();
                if (n == QLatin1String("t")) inText = true;
                else if (n == QLatin1String("tab")) out += QLatin1Char('\t');
                else if (n == QLatin1String("br") || n == QLatin1String("cr")) out += QLatin1Char('\n');
                break;
            }
            case QXmlStreamReader::EndElement: {
                const auto n = xml.name();
                if (n == QLatin1String("t")) inText = false;
                else if (n == QLatin1String("p")) out += QLatin1Char('\n');
                else if (n == QLatin1String("tc")) endCell(out);
                else if (n == QLatin1String("tr")) endRow(out);
                break;
            }
            case QXmlStreamReader::Characters:
                if (inText) out += xml.text();
                break;
            default:
                break;
        }
    }
    return tidy(out);
}

QString odtText(const QByteArray& contentXml) {
    QXmlStreamReader xml(contentXml);
    QString out;
    int inParagraph = 0;
    while (!xml.atEnd()) {
        switch (xml.readNext()) {
            case QXmlStreamReader::StartElement: {
                const auto n = xml.name();
                if (n == QLatin1String("p") || n == QLatin1String("h")) ++inParagraph;
                else if (n == QLatin1String("s")) {
                    const int count = xml.attributes().value(QLatin1String("text:c")).toInt();
                    out += QString(std::max(1, count), QLatin1Char(' '));
                } else if (n == QLatin1String("tab")) out += QLatin1Char('\t');
                else if (n == QLatin1String("line-break")) out += QLatin1Char('\n');
                break;
            }
            case QXmlStreamReader::EndElement: {
                const auto n = xml.name();
                if (n == QLatin1String("p") || n == QLatin1String("h")) {
                    inParagraph = std::max(0, inParagraph - 1);
                    out += QLatin1Char('\n');
                } else if (n == QLatin1String("table-cell")) endCell(out);
                else if (n == QLatin1String("table-row")) endRow(out);
                break;
            }
            case QXmlStreamReader::Characters:
                if (inParagraph > 0) out += xml.text();
                break;
            default:
                break;
        }
    }
    return tidy(out);
}

QString htmlText(const QByteArray& html) {
    return tidy(QTextDocumentFragment::fromHtml(plainText(html)).toPlainText());
}

QString plainText(const QByteArray& data) {
    QByteArray bytes = data;
    if (bytes.startsWith("\xEF\xBB\xBF")) bytes.remove(0, 3);
    QStringDecoder utf8(QStringDecoder::Utf8);
    const QString text = utf8.decode(bytes);
    if (!utf8.hasError()) return text;
    return QString::fromLatin1(bytes);
}

QString tidy(const QString& text) {
    static const QRegularExpression trailing(QStringLiteral("[ \\t\\x{00A0}]+\\n"));
    static const QRegularExpression blankLines(QStringLiteral("\\n{3,}"));
    QString out = text;
    out.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(QLatin1Char('\r'), QLatin1Char('\n'));
    out.replace(QChar(0x0C), QLatin1Char('\n'));   // salto de página de pdftotext
    out.replace(trailing, QStringLiteral("\n"));
    out.replace(blankLines, QStringLiteral("\n\n"));
    return out.trimmed();
}

} // namespace qaflow::documents
