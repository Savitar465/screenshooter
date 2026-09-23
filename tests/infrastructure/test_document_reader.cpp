// documents:: y DocumentReader (infrastructure/documents): el texto de los adjuntos de un requerimiento.

#include "infrastructure/documents/DocumentFormats.h"
#include "infrastructure/documents/DocumentReader.h"
#include "infrastructure/report/ZipWriter.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace qaflow;

namespace {
QByteArray fixture(const char* name) {
    QFile f(QStringLiteral(QAFLOW_FIXTURES_DIR "/documents/") + QString::fromLatin1(name));
    if (!f.open(QIODevice::ReadOnly)) qFatal("Falta el fixture %s", name);
    return f.readAll();
}

/// Lee con el lector real y espera al resultado (algunos formatos pasan por un programa externo).
DocumentText readWith(DocumentReader& reader, const QString& name, const QByteArray& data) {
    DocumentText result;
    bool done = false;
    reader.read(name, data, [&](const DocumentText& r) { result = r; done = true; });
    if (!QTest::qWaitFor([&] { return done; }, 90000)) qFatal("La lectura no terminó");
    return result;
}
} // namespace

class DocumentReaderTest : public QObject {
    Q_OBJECT
private slots:
    // ---- Formatos ----------------------------------------------------------------------

    void recognizesTheFormatByExtensionAndThenByContent() {
        using documents::Format;
        QCOMPARE(documents::formatOf(QStringLiteral("a.PDF"), {}), Format::Pdf);
        QCOMPARE(documents::formatOf(QStringLiteral("a.docx"), {}), Format::Docx);
        QCOMPARE(documents::formatOf(QStringLiteral("a.odt"), {}), Format::Odt);
        QCOMPARE(documents::formatOf(QStringLiteral("a.doc"), {}), Format::LegacyOffice);
        QCOMPARE(documents::formatOf(QStringLiteral("a.txt"), {}), Format::Text);
        QCOMPARE(documents::formatOf(QStringLiteral("a.html"), {}), Format::Html);
        QCOMPARE(documents::formatOf(QStringLiteral("docDownload.do"), fixture("requerimiento.pdf")), Format::Pdf);
        QCOMPARE(documents::formatOf(QString(), fixture("requerimiento.docx")), Format::Docx);
        QCOMPARE(documents::formatOf(QString(), fixture("requerimiento.odt")), Format::Odt);
        QCOMPARE(documents::formatOf(QString(), fixture("requerimiento.rtf")), Format::LegacyOffice);
        QCOMPARE(documents::formatOf(QStringLiteral("firma.png"), QByteArray("\x89PNG")), Format::Unknown);
    }

    void readsStoredAndDeflatedZipEntries() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("store.zip"));
        {
            ZipWriter zip(path);
            QVERIFY(zip.add(QStringLiteral("a.txt"), "uno"));
            QVERIFY(zip.add(QStringLiteral("b.txt"), "dos"));
            QVERIFY(zip.close());
        }
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QByteArray stored = f.readAll();
        QCOMPARE(documents::zipEntry(stored, QStringLiteral("b.txt")).value_or(QByteArray()), QByteArray("dos"));
        QString error;
        QVERIFY(!documents::zipEntry(stored, QStringLiteral("c.txt"), &error));
        QVERIFY(error.contains(QStringLiteral("c.txt")));
        QVERIFY(!documents::zipEntry("no es un zip", QStringLiteral("a.txt"), &error));

        if (!documents::canInflate()) QSKIP("Compilado sin zlib");
        const auto xml = documents::zipEntry(fixture("requerimiento.docx"), QStringLiteral("word/document.xml"));
        QVERIFY(xml.has_value());
        QVERIFY(xml->contains("La placa es"));
    }

    void wordTextKeepsParagraphsTablesAndBreaks() {
        if (!documents::canInflate()) QSKIP("Compilado sin zlib");
        const QString text = documents::wordText(*documents::zipEntry(fixture("requerimiento.docx"), QStringLiteral("word/document.xml")));
        QCOMPARE(text, QStringLiteral("Requerimiento de tránsitos\nLa placa es obligatoria.\nCampo\tRegla\nsegunda línea\n"
                                      "Columna | Tipo\nPlaca | Texto\nCuadro de texto"));
    }

    void odtTextKeepsSpacesTabsAndTables() {
        if (!documents::canInflate()) QSKIP("Compilado sin zlib");
        const QString text = documents::odtText(*documents::zipEntry(fixture("requerimiento.odt"), QStringLiteral("content.xml")));
        QCOMPARE(text, QStringLiteral("Requerimiento de tránsitos\nLa placa es obligatoria.\nA   B\tC\nD\nPlaca | Texto"));
    }

    void plainTextFallsBackToLatin1AndTidyCleansUp() {
        QCOMPARE(documents::plainText("\xEF\xBB\xBFtr\xC3\xA1nsito"), QStringLiteral("tránsito"));
        QCOMPARE(documents::plainText("tr\xE1nsito"), QStringLiteral("tránsito"));
        QCOMPARE(documents::tidy(QStringLiteral("  a  \r\n\n\n\nb\f c ")), QStringLiteral("a\n\nb\n c"));
        QCOMPARE(documents::htmlText("<html><body><p>Uno</p><p>Dos &amp; tres</p></body></html>"), QStringLiteral("Uno\nDos & tres"));
    }

    // ---- Lector ------------------------------------------------------------------------

    void readsWordAndOpenDocumentInProcess() {
        if (!documents::canInflate()) QSKIP("Compilado sin zlib");
        DocumentReader reader;
        const DocumentText docx = readWith(reader, QStringLiteral("requerimiento.docx"), fixture("requerimiento.docx"));
        QVERIFY2(docx.ok, qPrintable(docx.error));
        QVERIFY(docx.text.contains(QStringLiteral("Placa | Texto")));
        const DocumentText odt = readWith(reader, QStringLiteral("requerimiento.odt"), fixture("requerimiento.odt"));
        QVERIFY2(odt.ok, qPrintable(odt.error));
    }

    void readsPdfWithPdftotext() {
        if (DocumentReader::pdfToText().isEmpty()) QSKIP("pdftotext no está instalado");
        DocumentReader reader;
        const DocumentText pdf = readWith(reader, QStringLiteral("requerimiento.pdf"), fixture("requerimiento.pdf"));
        QVERIFY2(pdf.ok, qPrintable(pdf.error));
        QVERIFY(pdf.text.contains(QStringLiteral("La placa es obligatoria.")));
        QVERIFY(pdf.text.contains(QStringLiteral("tránsitos")));
    }

    void aDamagedPdfIsAnErrorNotEmptyText() {
        if (DocumentReader::pdfToText().isEmpty()) QSKIP("pdftotext no está instalado");
        DocumentReader reader;
        const DocumentText pdf = readWith(reader, QStringLiteral("roto.pdf"), "%PDF-1.4 esto no es un pdf");
        QVERIFY(!pdf.ok);
        QVERIFY(!pdf.error.isEmpty());
    }

    void readsLegacyWordWithLibreOffice() {
        if (DocumentReader::office().isEmpty()) QSKIP("LibreOffice no está instalado");
        DocumentReader reader;
        const DocumentText rtf = readWith(reader, QStringLiteral("requerimiento.rtf"), fixture("requerimiento.rtf"));
        QVERIFY2(rtf.ok, qPrintable(rtf.error));
        QVERIFY(rtf.text.contains(QStringLiteral("La placa es obligatoria.")));
        QVERIFY(rtf.text.contains(QStringLiteral("Segunda línea.")));
    }

    void unknownOrEmptyDocumentsAreExplained() {
        DocumentReader reader;
        const DocumentText png = readWith(reader, QStringLiteral("firma.png"), "\x89PNG\r\n");
        QVERIFY(!png.ok);
        QVERIFY(png.error.contains(QStringLiteral("firma.png")));
        QVERIFY(!readWith(reader, QStringLiteral("vacio.txt"), QByteArray()).ok);
        QVERIFY(!readWith(reader, QStringLiteral("blanco.txt"), "   \n\n ").ok);
        QVERIFY(readWith(reader, QStringLiteral("notas.txt"), "La placa es obligatoria.").ok);
    }
};

QTEST_GUILESS_MAIN(DocumentReaderTest)
#include "test_document_reader.moc"
