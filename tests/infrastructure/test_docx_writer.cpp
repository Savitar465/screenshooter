// ZipWriter y DocxWriter (infrastructure/report/): el .docx que QAflow escribe sin plantillas ni
// dependencias. Se comprueba el ZIP (entradas almacenadas, CRC y directorio central), las partes
// obligatorias del documento y que el acta R-213 lleva en sus celdas lo que dice el modelo.

#include "core/models/QualityRecord.h"
#include "infrastructure/report/DocxWriter.h"
#include "infrastructure/report/QualityRecordDocx.h"
#include "infrastructure/report/ZipWriter.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QtTest>

using namespace qaflow;

namespace {

/// Lector mínimo de ZIP para los tests: recorre las cabeceras locales (todas las entradas son
/// «store», así que el tamaño comprimido es el real) y devuelve nombre → contenido.
QHash<QString, QByteArray> readZip(const QString& path, bool* ok = nullptr) {
    QHash<QString, QByteArray> entries;
    if (ok) *ok = false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return entries;
    const QByteArray bytes = f.readAll();
    auto u16 = [&bytes](int at) { return quint16(quint8(bytes[at])) | quint16(quint8(bytes[at + 1])) << 8; };
    auto u32 = [&bytes](int at) {
        quint32 v = 0;
        for (int i = 0; i < 4; ++i) v |= quint32(quint8(bytes[at + i])) << (8 * i);
        return v;
    };
    int at = 0;
    while (at + 30 <= bytes.size() && u32(at) == 0x04034b50) {
        const quint32 crc = u32(at + 14);
        const quint32 size = u32(at + 18);
        const int nameLen = u16(at + 26);
        const int extraLen = u16(at + 28);
        const QString name = QString::fromUtf8(bytes.mid(at + 30, nameLen));
        const QByteArray data = bytes.mid(at + 30 + nameLen + extraLen, int(size));
        if (ZipWriter::crc32(data) != crc) return entries;   // el CRC tiene que cuadrar
        entries.insert(name, data);
        at += 30 + nameLen + extraLen + int(size);
    }
    // Detrás de las entradas va el directorio central y el fin de directorio.
    if (ok) *ok = at + 4 <= bytes.size() && u32(at) == 0x02014b50 && bytes.contains(QByteArrayLiteral("PK\x05\x06"));
    return entries;
}

QualityRecord sampleRecord() {
    QualityRecord r;
    r.greq = QStringLiteral("2026997");
    r.system = QStringLiteral("SUMA V2 INGRESO");
    r.moduleLink = QStringLiteral("https://gitlab.test/ssu-mim-mig-dav-front");
    r.server = QStringLiteral("10.0.67.131");
    r.description = QStringLiteral("Integración de nuevos servicios backend\ny actualización de dominio");
    r.developedBy = QStringLiteral("CANAZA ESTEBAN");
    r.qaResource = QStringLiteral("MAIDANA JUAN JONAS");
    r.revisionNumber = 2;
    r.from = QDate(2026, 9, 9);
    r.to = QDate(2026, 9, 10);
    r.observations[0].observations = 3;   // A · Funcionamiento/Lógica
    r.observations[1].corrections = 1;    // B · Datos
    r.caseDesign = QStringLiteral("Jira: QA - Elaboración de casos de prueba GREQ 2026997");
    r.execution = QStringLiteral("http://jira.test/browse/SUMA2-2907");
    r.bugs = QStringLiteral("http://jira.test/browse/SUMA2-2912");
    r.characteristics[0].satisfied = false;
    r.characteristics[0].note = QStringLiteral("Falta la ayuda de la pantalla");
    r.generalNotes = QStringLiteral("Se reprograma la revisión");
    return r;
}

} // namespace

class DocxWriterTest : public QObject {
    Q_OBJECT
private slots:
    // ---- ZIP ------------------------------------------------------------------------------------
    void zipEntriesAreStoredWithTheirCrcAndCanBeReadBack() {
        QTemporaryDir dir;
        const QString path = QDir(dir.path()).filePath(QStringLiteral("prueba.zip"));
        {
            ZipWriter zip(path);
            QVERIFY(zip.isOpen());
            QVERIFY(zip.add(QStringLiteral("uno.txt"), QByteArrayLiteral("hola")));
            QVERIFY(zip.add(QStringLiteral("carpeta/dos.txt"), QByteArrayLiteral("qué tal")));
            QVERIFY(zip.close());
        }
        bool ok = false;
        const auto entries = readZip(path, &ok);
        QVERIFY(ok);
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries.value(QStringLiteral("uno.txt")), QByteArrayLiteral("hola"));
        QCOMPARE(entries.value(QStringLiteral("carpeta/dos.txt")), QByteArrayLiteral("qué tal"));
    }

    void aZipThatCouldNotBeCreatedSaysSo() {
        ZipWriter zip(QStringLiteral("/no/existe/ni/se/puede/crear.zip"));
        QVERIFY(!zip.isOpen());
        QVERIFY(!zip.error().isEmpty());
        QVERIFY(!zip.add(QStringLiteral("x"), QByteArrayLiteral("y")));
    }

    // ---- Documento ------------------------------------------------------------------------------
    void theDocumentHasTheMandatoryPartsAndItsText() {
        QTemporaryDir dir;
        const QString path = QDir(dir.path()).filePath(QStringLiteral("doc.docx"));
        docx::Table table;
        table.grid = {4000, 5000};
        docx::Row row;
        docx::Cell head;
        head.paragraphs << docx::Paragraph{QStringLiteral("Cabecera"), true, docx::Align::Center, 20, {}, 0};
        head.gridSpan = 2;
        head.shade = QStringLiteral("D9D9D9");
        row.cells << head;
        table.rows << row;
        docx::Row second;
        docx::Cell a;
        a.paragraphs << docx::Paragraph{QStringLiteral("línea 1\nlínea 2"), false, docx::Align::Left, 20, {}, 0};
        docx::Cell b;
        b.paragraphs << docx::Paragraph{QStringLiteral("5 < 6 & \"comillas\""), false, docx::Align::Left, 20, {}, 0};
        second.cells << a << b;
        table.rows << second;

        docx::Block block;
        block.table = table;
        const docx::Result result = docx::write(path, {block});
        QVERIFY2(result.ok, qPrintable(result.error));

        bool ok = false;
        const auto parts = readZip(path, &ok);
        QVERIFY(ok);
        for (const auto& part : {QStringLiteral("[Content_Types].xml"), QStringLiteral("_rels/.rels"),
                                 QStringLiteral("word/document.xml"), QStringLiteral("word/_rels/document.xml.rels"),
                                 QStringLiteral("word/styles.xml")})
            QVERIFY2(parts.contains(part), qPrintable(part));

        const QString document = QString::fromUtf8(parts.value(QStringLiteral("word/document.xml")));
        QVERIFY(document.contains(QStringLiteral("<w:t xml:space=\"preserve\">Cabecera</w:t>")));
        QVERIFY(document.contains(QStringLiteral("<w:gridSpan w:val=\"2\"/>")));
        QVERIFY(document.contains(QStringLiteral("w:fill=\"D9D9D9\"")));
        QVERIFY(document.contains(QStringLiteral("<w:br/>")));                      // el salto dentro de la celda
        QVERIFY(document.contains(QStringLiteral("5 &lt; 6 &amp; &quot;comillas&quot;")));
        QVERIFY(document.contains(QStringLiteral("<w:sectPr>")));
        QVERIFY(!document.contains(QStringLiteral("<w:drawing>")));                 // sin imágenes
    }

    void imagesAreEmbeddedAndTheOnesThatAreNotThereAreSkipped() {
        QTemporaryDir dir;
        const QString image = QDir(dir.path()).filePath(QStringLiteral("logo.png"));
        QImage(QSize(300, 100), QImage::Format_RGB32).save(image);
        const QString path = QDir(dir.path()).filePath(QStringLiteral("con_imagen.docx"));

        docx::Block block;
        docx::Table table;
        table.grid = {9000};
        docx::Row row;
        docx::Cell c;
        docx::Paragraph good;
        good.imagePath = image;
        docx::Paragraph missing;
        missing.imagePath = QDir(dir.path()).filePath(QStringLiteral("no_esta.png"));
        c.paragraphs << good << missing;
        row.cells << c;
        table.rows << row;
        block.table = table;
        QVERIFY(docx::write(path, {block}).ok);

        bool ok = false;
        const auto parts = readZip(path, &ok);
        QVERIFY(ok);
        QVERIFY(parts.contains(QStringLiteral("word/media/imagen1.png")));
        QVERIFY(!parts.contains(QStringLiteral("word/media/imagen2.png")));   // la que no existe no entra
        QVERIFY(QString::fromUtf8(parts.value(QStringLiteral("[Content_Types].xml"))).contains(QStringLiteral("Extension=\"png\"")));
        const QString rels = QString::fromUtf8(parts.value(QStringLiteral("word/_rels/document.xml.rels")));
        QVERIFY(rels.contains(QStringLiteral("Target=\"media/imagen1.png\"")));
        const QString document = QString::fromUtf8(parts.value(QStringLiteral("word/document.xml")));
        QCOMPARE(document.count(QStringLiteral("<w:drawing>")), 1);
        QVERIFY(document.contains(QStringLiteral("r:embed=\"rId2\"")));
    }

    // ---- Acta R-213 -----------------------------------------------------------------------------
    void theQualityRecordFillsTheFormWithWhatTheModelSays() {
        QTemporaryDir dir;
        const QString path = QDir(dir.path()).filePath(QStringLiteral("ControlCalidad_2026997.docx"));
        QualityRecordDocx writer;
        const QualityRecordWriteResult result = writer.write(sampleRecord(), path);
        QVERIFY2(result.ok, qPrintable(result.error));

        // Para revisar el acta a ojo: con QAFLOW_DOCX_OUT=<carpeta> el test deja ahí una copia que
        // se puede abrir con Word o LibreOffice.
        if (const QString out = qEnvironmentVariable("QAFLOW_DOCX_OUT"); !out.isEmpty()) {
            QDir().mkpath(out);
            const QString copy = QDir(out).filePath(QStringLiteral("ControlCalidad_2026997.docx"));
            QFile::remove(copy);
            QVERIFY(QFile::copy(path, copy));
        }

        bool ok = false;
        const QString document = QString::fromUtf8(readZip(path, &ok).value(QStringLiteral("word/document.xml")));
        QVERIFY(ok);
        auto contains = [&document](const QString& value) { return document.contains(value); };
        // Cabecera del formulario
        QVERIFY(contains(QStringLiteral("REVISIÓN CONTROL DE CALIDAD DE SOFTWARE")));
        QVERIFY(contains(QStringLiteral("R-213")));
        QVERIFY(contains(QStringLiteral("Página 1 de 1")));
        // Generales
        QVERIFY(contains(QStringLiteral("GREQ 2026997")));
        QVERIFY(contains(QStringLiteral("SUMA V2 INGRESO")));
        QVERIFY(contains(QStringLiteral("10.0.67.131")));
        QVERIFY(contains(QStringLiteral("CANAZA ESTEBAN")));
        QVERIFY(contains(QStringLiteral("MAIDANA JUAN JONAS")));
        QVERIFY(contains(QStringLiteral("Departamento de Investigación y Desarrollo de Sistemas")));
        QVERIFY(contains(QStringLiteral("09/09/2026 a 10/09/2026")));
        QVERIFY(contains(QStringLiteral("S/D")));     // lo que GESREQ no da
        QVERIFY(contains(QStringLiteral("n/a")));
        // Resumen de observaciones, con sus cinco tipos y el total
        for (const auto& name : {QStringLiteral("Funcionamiento/Lógica"), QStringLiteral("Datos"),
                                 QStringLiteral("Estético/Forma"), QStringLiteral("Recomendaciones"),
                                 QStringLiteral("Vulnerabilidades")})
            QVERIFY2(contains(name), qPrintable(name));
        QVERIFY(contains(QStringLiteral("Total")));
        // Detalles y resultados
        QVERIFY(contains(QStringLiteral("Elaboración de Casos de prueba")));
        QVERIFY(contains(QStringLiteral("http://jira.test/browse/SUMA2-2912")));
        QVERIFY(contains(QStringLiteral("Existen las ayudas que necesita en las opciones que utilizó")));
        QVERIFY(contains(QStringLiteral("Falta la ayuda de la pantalla")));
        QVERIFY(contains(QStringLiteral(">No<")));    // la característica que no satisface
        QVERIFY(contains(QStringLiteral(">Si<")));
        QVERIFY(contains(QStringLiteral("Observaciones Generales")));
        QVERIFY(contains(QStringLiteral("Se reprograma la revisión")));
    }

    void theRecordSaysWhereItCouldNotBeWritten() {
        QualityRecordDocx writer;
        const QualityRecordWriteResult result = writer.write(sampleRecord(), QStringLiteral("/no/existe/acta.docx"));
        QVERIFY(!result.ok);
        QVERIFY(!result.error.isEmpty());
    }
};

QTEST_MAIN(DocxWriterTest)
#include "test_docx_writer.moc"
