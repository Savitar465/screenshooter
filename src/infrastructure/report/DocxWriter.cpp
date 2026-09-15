#include "DocxWriter.h"

#include "infrastructure/report/ZipWriter.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QSize>
#include <QStringList>

namespace qaflow::docx {

namespace {

// Medidas de Word: 1 pulgada = 1440 twips = 914400 EMU, así que un twip son 635 EMU.
constexpr int kEmuPerTwip = 635;
constexpr int kPageWidth = 9070;   // A4 (11906 twips) menos los márgenes de 1418

/// Una imagen del documento, ya leída del disco y lista para ir en `word/media/`.
struct Image {
    QString source;      // ruta en disco de la que salió
    QString id;          // rId
    QString part;        // word/media/imagen1.png
    QString extension;   // png
    QByteArray data;
    int widthEmu = 0;
    int heightEmu = 0;
};

QString escape(const QString& text) {
    QString out;
    out.reserve(text.size());
    for (const QChar c : text) {
        if (c == QLatin1Char('&')) out += QStringLiteral("&amp;");
        else if (c == QLatin1Char('<')) out += QStringLiteral("&lt;");
        else if (c == QLatin1Char('>')) out += QStringLiteral("&gt;");
        else if (c == QLatin1Char('"')) out += QStringLiteral("&quot;");
        else if (c == QLatin1Char('\t')) out += QStringLiteral(" ");
        else if (c.unicode() >= 0x20 || c == QLatin1Char('\n')) out += c;
    }
    return out;
}

QString alignment(Align align) {
    switch (align) {
        case Align::Left: return QStringLiteral("left");
        case Align::Center: return QStringLiteral("center");
        case Align::Right: return QStringLiteral("right");
        case Align::Justify: return QStringLiteral("both");
    }
    return QStringLiteral("left");
}

/// El texto de un párrafo: cada salto de línea es un `<w:br/>` para que la celda no se parta en dos.
QString runsFor(const Paragraph& p) {
    const QStringList lines = p.text.split(QLatin1Char('\n'));
    QString properties = QStringLiteral("<w:rPr>%1<w:sz w:val=\"%2\"/><w:szCs w:val=\"%2\"/></w:rPr>")
                             .arg(p.bold ? QStringLiteral("<w:b/>") : QString())
                             .arg(p.size);
    QString out;
    for (int i = 0; i < lines.size(); ++i) {
        out += QStringLiteral("<w:r>") + properties;
        if (i > 0) out += QStringLiteral("<w:br/>");
        out += QStringLiteral("<w:t xml:space=\"preserve\">%1</w:t></w:r>").arg(escape(lines[i]));
    }
    return out;
}

QString drawingFor(const Image& image, int index) {
    return QStringLiteral(
               "<w:r><w:drawing><wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\">"
               "<wp:extent cx=\"%1\" cy=\"%2\"/><wp:effectExtent l=\"0\" t=\"0\" r=\"0\" b=\"0\"/>"
               "<wp:docPr id=\"%3\" name=\"Imagen %3\"/>"
               "<wp:cNvGraphicFramePr><a:graphicFrameLocks noChangeAspect=\"1\"/></wp:cNvGraphicFramePr>"
               "<a:graphic><a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
               "<pic:pic><pic:nvPicPr><pic:cNvPr id=\"%3\" name=\"Imagen %3\"/><pic:cNvPicPr/></pic:nvPicPr>"
               "<pic:blipFill><a:blip r:embed=\"%4\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
               "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"%1\" cy=\"%2\"/></a:xfrm>"
               "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr>"
               "</pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r>")
        .arg(image.widthEmu)
        .arg(image.heightEmu)
        .arg(index)
        .arg(image.id);
}

QString paragraphXml(const Paragraph& p, const QHash<QString, Image>& images, int& imageIndex) {
    QString properties = QStringLiteral("<w:pPr><w:spacing w:after=\"0\"/><w:jc w:val=\"%1\"/></w:pPr>").arg(alignment(p.align));
    if (p.isImage()) {
        const auto it = images.constFind(p.imagePath);
        if (it == images.constEnd()) return QStringLiteral("<w:p>%1</w:p>").arg(properties);
        return QStringLiteral("<w:p>%1%2</w:p>").arg(properties, drawingFor(*it, ++imageIndex));
    }
    return QStringLiteral("<w:p>%1%2</w:p>").arg(properties, runsFor(p));
}

QString cellXml(const Cell& cell, const QHash<QString, Image>& images, int& imageIndex) {
    QString properties = QStringLiteral("<w:tcPr>");
    if (cell.width > 0) properties += QStringLiteral("<w:tcW w:w=\"%1\" w:type=\"dxa\"/>").arg(cell.width);
    if (cell.gridSpan > 1) properties += QStringLiteral("<w:gridSpan w:val=\"%1\"/>").arg(cell.gridSpan);
    if (cell.vMerge == VMerge::Restart) properties += QStringLiteral("<w:vMerge w:val=\"restart\"/>");
    else if (cell.vMerge == VMerge::Continue) properties += QStringLiteral("<w:vMerge w:val=\"continue\"/>");
    if (!cell.shade.isEmpty()) properties += QStringLiteral("<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\"%1\"/>").arg(cell.shade);
    properties += QStringLiteral("<w:vAlign w:val=\"center\"/></w:tcPr>");

    QString content;
    for (const auto& p : cell.paragraphs) content += paragraphXml(p, images, imageIndex);
    // Word no admite una celda sin párrafos.
    if (content.isEmpty()) content = QStringLiteral("<w:p><w:pPr><w:spacing w:after=\"0\"/></w:pPr></w:p>");
    return QStringLiteral("<w:tc>%1%2</w:tc>").arg(properties, content);
}

QString tableXml(const Table& table, const QHash<QString, Image>& images, int& imageIndex) {
    static const QString borders =
        QStringLiteral("<w:tblBorders>"
                       "<w:top w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"000000\"/>"
                       "<w:left w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"000000\"/>"
                       "<w:bottom w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"000000\"/>"
                       "<w:right w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"000000\"/>"
                       "<w:insideH w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"000000\"/>"
                       "<w:insideV w:val=\"single\" w:sz=\"4\" w:space=\"0\" w:color=\"000000\"/>"
                       "</w:tblBorders>");
    int total = 0;
    for (const int width : table.grid) total += width;
    QString out = QStringLiteral("<w:tbl><w:tblPr><w:tblW w:w=\"%1\" w:type=\"dxa\"/>%2"
                                 "<w:tblCellMar><w:top w:w=\"40\" w:type=\"dxa\"/><w:left w:w=\"80\" w:type=\"dxa\"/>"
                                 "<w:bottom w:w=\"40\" w:type=\"dxa\"/><w:right w:w=\"80\" w:type=\"dxa\"/></w:tblCellMar>"
                                 "</w:tblPr><w:tblGrid>")
                      .arg(total > 0 ? total : pageWidth())
                      .arg(borders);
    for (const int width : table.grid) out += QStringLiteral("<w:gridCol w:w=\"%1\"/>").arg(width);
    out += QStringLiteral("</w:tblGrid>");
    for (const auto& row : table.rows) {
        out += QStringLiteral("<w:tr>");
        for (const auto& cell : row.cells) out += cellXml(cell, images, imageIndex);
        out += QStringLiteral("</w:tr>");
    }
    return out + QStringLiteral("</w:tbl>");
}

QByteArray documentXml(const QList<Block>& blocks, const QHash<QString, Image>& images) {
    QString body;
    int imageIndex = 0;
    for (const auto& block : blocks) {
        if (block.isTable()) body += tableXml(block.table, images, imageIndex);
        else body += paragraphXml(block.paragraph, images, imageIndex);
    }
    // Dos tablas seguidas se pegarían en una sola: Word necesita un párrafo entre ellas y al final.
    body += QStringLiteral("<w:p><w:pPr><w:spacing w:after=\"0\"/></w:pPr></w:p>");
    body += QStringLiteral("<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/>"
                           "<w:pgMar w:top=\"1134\" w:right=\"1418\" w:bottom=\"1134\" w:left=\"1418\" "
                           "w:header=\"709\" w:footer=\"709\" w:gutter=\"0\"/></w:sectPr>");

    const QString xml =
        QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                       "<w:document "
                       "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
                       "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
                       "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\" "
                       "xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\" "
                       "xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
                       "<w:body>%1</w:body></w:document>")
            .arg(body);
    return xml.toUtf8();
}

QByteArray stylesXml() {
    return QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
        "<w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii=\"Calibri\" w:hAnsi=\"Calibri\" w:cs=\"Arial\"/>"
        "<w:sz w:val=\"20\"/><w:szCs w:val=\"20\"/><w:lang w:val=\"es-BO\"/></w:rPr></w:rPrDefault>"
        "<w:pPrDefault><w:pPr><w:spacing w:after=\"0\"/></w:pPr></w:pPrDefault></w:docDefaults>"
        "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\"><w:name w:val=\"Normal\"/><w:qFormat/></w:style>"
        "</w:styles>");
}

QByteArray contentTypesXml(const QStringList& imageExtensions) {
    QString defaults;
    for (const auto& extension : imageExtensions)
        defaults += QStringLiteral("<Default Extension=\"%1\" ContentType=\"image/%2\"/>")
                        .arg(extension, extension == QStringLiteral("jpg") ? QStringLiteral("jpeg") : extension);
    return QStringLiteral(
               "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
               "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
               "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
               "<Default Extension=\"xml\" ContentType=\"application/xml\"/>%1"
               "<Override PartName=\"/word/document.xml\" "
               "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml\"/>"
               "<Override PartName=\"/word/styles.xml\" "
               "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml\"/>"
               "</Types>")
        .arg(defaults)
        .toUtf8();
}

QByteArray packageRelsXml() {
    return QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
        "Target=\"word/document.xml\"/></Relationships>");
}

QByteArray documentRelsXml(const QList<Image>& images) {
    QString relationships =
        QStringLiteral("<Relationship Id=\"rId1\" "
                       "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" "
                       "Target=\"styles.xml\"/>");
    for (const auto& image : images)
        relationships += QStringLiteral("<Relationship Id=\"%1\" "
                                        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" "
                                        "Target=\"media/%2\"/>")
                             .arg(image.id, QFileInfo(image.part).fileName());
    return QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                          "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">%1</Relationships>")
        .arg(relationships)
        .toUtf8();
}

/// Lee del disco las imágenes que se usan y las escala al ancho disponible sin deformarlas. Una que
/// no se pueda leer se queda fuera: el acta sale sin ella, no rota.
QList<Image> collectImages(const QList<Block>& blocks) {
    QList<Image> images;
    QStringList seen;
    auto take = [&](const Paragraph& p) {
        const QString path = p.imagePath.trimmed();
        if (path.isEmpty() || seen.contains(path)) return;
        QImageReader reader(path);
        const QSize size = reader.size();
        QFile file(path);
        if (!size.isValid() || size.isEmpty() || !file.open(QIODevice::ReadOnly)) return;
        const QString extension = QFileInfo(path).suffix().toLower();
        if (!QStringList({QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("gif")}).contains(extension))
            return;
        seen << path;
        Image image;
        image.source = path;
        image.id = QStringLiteral("rId%1").arg(images.size() + 2);   // rId1 son los estilos
        image.extension = extension == QStringLiteral("jpeg") ? QStringLiteral("jpg") : extension;
        image.part = QStringLiteral("word/media/imagen%1.%2").arg(images.size() + 1).arg(image.extension);
        image.data = file.readAll();
        const int maxWidth = p.imageMaxWidth > 0 ? p.imageMaxWidth : pageWidth();
        // 96 ppp: un píxel son 15 twips. Nunca se amplía, sólo se reduce para que quepa.
        const double widthTwips = size.width() * 15.0;
        const double scale = widthTwips > maxWidth ? maxWidth / widthTwips : 1.0;
        image.widthEmu = int(widthTwips * scale * kEmuPerTwip);
        image.heightEmu = int(size.height() * 15.0 * scale * kEmuPerTwip);
        images << image;
    };
    for (const auto& block : blocks) {
        if (!block.isTable()) {
            take(block.paragraph);
            continue;
        }
        for (const auto& row : block.table.rows)
            for (const auto& cell : row.cells)
                for (const auto& p : cell.paragraphs) take(p);
    }
    return images;
}

} // namespace

int pageWidth() { return kPageWidth; }

Result write(const QString& path, const QList<Block>& blocks) {
    const QList<Image> images = collectImages(blocks);
    QHash<QString, Image> byPath;
    QStringList extensions;
    for (const auto& image : images) {
        byPath.insert(image.source, image);
        if (!extensions.contains(image.extension)) extensions << image.extension;
    }

    ZipWriter zip(path);
    if (!zip.isOpen())
        return {false, QCoreApplication::translate("infrastructure", "No se pudo crear el documento: %1").arg(zip.error())};
    const bool written = zip.add(QStringLiteral("[Content_Types].xml"), contentTypesXml(extensions)) &&
                         zip.add(QStringLiteral("_rels/.rels"), packageRelsXml()) &&
                         zip.add(QStringLiteral("word/document.xml"), documentXml(blocks, byPath)) &&
                         zip.add(QStringLiteral("word/_rels/document.xml.rels"), documentRelsXml(images)) &&
                         zip.add(QStringLiteral("word/styles.xml"), stylesXml());
    if (!written) return {false, QCoreApplication::translate("infrastructure", "No se pudo escribir el documento: %1").arg(zip.error())};
    for (const auto& image : images)
        if (!zip.add(image.part, image.data))
            return {false, QCoreApplication::translate("infrastructure", "No se pudo escribir el documento: %1").arg(zip.error())};
    if (!zip.close()) return {false, QCoreApplication::translate("infrastructure", "No se pudo guardar el documento: %1").arg(zip.error())};
    return {true, {}};
}

} // namespace qaflow::docx
