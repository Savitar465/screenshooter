// AttachmentTextService (application/AttachmentTextService.h): texto de los adjuntos de GESREQ para la IA.

#include "application/AttachmentTextService.h"
#include "support/AppFixture.h"

#include <QtTest>

using namespace qaflow;
using qaflow::testing::AppFixture;

namespace {
/// Lector falso: el «texto» es el contenido tal cual, salvo los ficheros que empiezan por "ilegible".
class FakeReader : public IDocumentReader {
public:
    QStringList read_;
    void read(const QString& fileName, const QByteArray& data, std::function<void(const DocumentText&)> done) override {
        read_ << fileName;
        if (data.startsWith("ilegible")) done(DocumentText{false, {}, QStringLiteral("no tiene texto")});
        else done(DocumentText{true, QString::fromUtf8(data), {}});
    }
};

DocumentText readSync(AttachmentTextService& service, const RequirementAttachment& a) {
    DocumentText out;
    service.read(a, [&out](const DocumentText& t) { out = t; });
    return out;
}
} // namespace

class AttachmentTextServiceTest : public QObject {
    Q_OBJECT
private slots:
    void downloadsReadsAndRemembersTheText() {
        AppFixture f;
        const RequirementAttachment spec{QStringLiteral("Respaldo"), QStringLiteral("spec.pdf"), QStringLiteral("http://g/doc1")};
        f.requirementSource->files.insert(spec.url, "La placa es obligatoria.");
        auto reader = std::make_shared<FakeReader>();
        AttachmentTextService service(f.requirements, reader);

        const DocumentText first = readSync(service, spec);
        QVERIFY(first.ok);
        QCOMPARE(first.text, QStringLiteral("La placa es obligatoria."));
        QCOMPARE(reader->read_, QStringList{QStringLiteral("spec.pdf")});
        // La segunda vez no se descarga ni se lee otra vez.
        QCOMPARE(readSync(service, spec).text, first.text);
        QCOMPARE(f.requirementSource->downloads.size(), 1);
        QCOMPARE(reader->read_.size(), 1);
    }

    void failuresSayWhichAttachmentAndAreNotRemembered() {
        AppFixture f;
        const RequirementAttachment missing{QStringLiteral("Respaldo"), QStringLiteral("borrado.pdf"), QStringLiteral("http://g/doc2")};
        const RequirementAttachment scan{QStringLiteral("Anexo"), QStringLiteral("scan.pdf"), QStringLiteral("http://g/doc3")};
        f.requirementSource->files.insert(scan.url, "ilegible");
        AttachmentTextService service(f.requirements, std::make_shared<FakeReader>());

        const DocumentText notDownloaded = readSync(service, missing);
        QVERIFY(!notDownloaded.ok);
        QVERIFY(notDownloaded.error.contains(QStringLiteral("borrado.pdf")));
        QVERIFY(!readSync(service, scan).ok);
        QVERIFY(!readSync(service, scan).ok);
        QCOMPARE(f.requirementSource->downloads.size(), 3);   // lo que falló se vuelve a intentar
    }
};

QTEST_GUILESS_MAIN(AttachmentTextServiceTest)
#include "test_attachment_text_service.moc"
