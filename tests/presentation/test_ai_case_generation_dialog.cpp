// AiCaseGenerationDialog (presentation/views/AiCaseGenerationDialog.h): prompt revisable, respuesta pegada y
// elección de los casos que se añaden.

#include "presentation/views/AiCaseGenerationDialog.h"
#include "presentation/widgets/TextArea.h"

#include <QApplication>
#include <QClipboard>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QtTest>

using namespace qaflow;

namespace {
ai::GenerationRequest request() {
    ai::GenerationRequest r;
    r.requirementId = QStringLiteral("2025175");
    r.source = QStringLiteral("Alcance original");
    return r;
}
} // namespace

class AiCaseGenerationDialogTest : public QObject {
    Q_OBJECT
private slots:
    void copiesThePromptWithTheEditedSource() {
        AiCaseGenerationDialog d(request(), QStringLiteral("al plan PL-0002"), {}, {});
        d.findChild<TextArea*>(QStringLiteral("aiSource"))->setPlainText(QStringLiteral("Alcance revisado"));
        d.findChild<QPushButton*>(QStringLiteral("aiCopyPrompt"))->click();
        const QString copied = QApplication::clipboard()->text();
        QVERIFY(copied.contains(QStringLiteral("Alcance revisado")));
        QVERIFY(!copied.contains(QStringLiteral("Alcance original")));
        QCOMPARE(d.findChild<QTabWidget*>(QStringLiteral("aiTabs"))->currentIndex(), 1);
    }

    void interpretsAndAddsOnlyTheCheckedCases() {
        AiCaseGenerationDialog d(request(), QStringLiteral("al plan PL-0002"), {}, {});
        auto* accept = d.findChild<QPushButton*>(QStringLiteral("aiAccept"));
        QVERIFY(!accept->isEnabled());
        d.findChild<TextArea*>(QStringLiteral("aiResponse"))->setPlainText(QStringLiteral(
            "```json\n[{\"title\":\"A\",\"steps\":[{\"action\":\"x\",\"expected\":\"y\"}]},{\"title\":\"B\",\"steps\":[{\"action\":\"x\"}]}]\n```"));
        d.findChild<QPushButton*>(QStringLiteral("aiInterpret"))->click();
        auto* list = d.findChild<QListWidget*>(QStringLiteral("aiCases"));
        QCOMPARE(list->count(), 2);
        QVERIFY(d.findChild<QLabel*>(QStringLiteral("aiStatus"))->text().contains(QStringLiteral("paso 1")));
        QVERIFY(accept->isEnabled());
        QCOMPARE(d.chosenCases().size(), 2);
        list->item(0)->setCheckState(Qt::Unchecked);
        QCOMPARE(d.chosenCases().size(), 1);
        QCOMPARE(d.chosenCases().first().title, QStringLiteral("B"));
        QVERIFY(accept->text().contains(QLatin1Char('1')));
        list->item(1)->setCheckState(Qt::Unchecked);
        QVERIFY(!accept->isEnabled());
    }

    void addsTheTextOfTheCheckedAttachments() {
        const RequirementAttachment spec{QStringLiteral("Archivo de respaldo"), QStringLiteral("spec.pdf"), QStringLiteral("http://g/doc1")};
        const RequirementAttachment scan{QStringLiteral("Anexo"), QStringLiteral("firma.png"), QStringLiteral("http://g/doc2")};
        const RequirementAttachment other{QStringLiteral("Otro"), QStringLiteral("otro.docx"), QStringLiteral("http://g/doc3")};
        QStringList asked;
        auto loader = [&asked](const RequirementAttachment& a, std::function<void(const DocumentText&)> done) {
            asked << a.fileName;
            if (a.fileName == QStringLiteral("spec.pdf")) done(DocumentText{true, QStringLiteral("La placa es obligatoria."), {}});
            else done(DocumentText{false, {}, QStringLiteral("QAflow no sabe leer el texto de «firma.png»")});
        };
        AiCaseGenerationDialog d(request(), QStringLiteral("al plan PL-0002"), {spec, scan, other}, loader);
        auto* list = d.findChild<QListWidget*>(QStringLiteral("aiAttachments"));
        QCOMPARE(list->count(), 3);
        list->item(2)->setCheckState(Qt::Unchecked);   // sólo se leen los marcados
        d.findChild<QPushButton*>(QStringLiteral("aiAddAttachments"))->click();

        QCOMPARE(asked, (QStringList{QStringLiteral("spec.pdf"), QStringLiteral("firma.png")}));
        const QString source = d.findChild<TextArea*>(QStringLiteral("aiSource"))->toPlainText();
        QVERIFY(source.startsWith(QStringLiteral("Alcance original")));
        QVERIFY(source.contains(QStringLiteral("=== Adjunto: spec.pdf ===\nLa placa es obligatoria.")));
        QVERIFY(d.prompt().contains(QStringLiteral("La placa es obligatoria.")));
        QCOMPARE(list->item(0)->checkState(), Qt::Unchecked);   // añadido: no se repite
        QVERIFY(list->item(1)->text().contains(QStringLiteral("no se pudo leer")));
        QVERIFY(d.findChild<QLabel*>(QStringLiteral("aiAttachmentStatus"))->text().contains(QStringLiteral("firma.png")));
        QVERIFY(d.findChild<QPushButton*>(QStringLiteral("aiAddAttachments"))->isEnabled());
    }

    void withoutConnectionAttachmentsAreCopiedByHand() {
        const RequirementAttachment spec{QStringLiteral("Archivo"), QStringLiteral("spec.pdf"), QStringLiteral("http://g/doc1")};
        AiCaseGenerationDialog d(request(), QStringLiteral("al plan PL-0002"), {spec}, {});
        QVERIFY(!d.findChild<QPushButton*>(QStringLiteral("aiAddAttachments"))->isEnabled());
        QVERIFY(d.findChild<QLabel*>(QStringLiteral("aiAttachmentStatus"))->text().contains(QStringLiteral("a mano")));
    }

    void warnsWhenThePromptIsTooLong() {
        AiCaseGenerationDialog d(request(), QStringLiteral("al plan PL-0002"), {}, {});
        auto* size = d.findChild<QLabel*>(QStringLiteral("aiSourceSize"));
        QVERIFY(!size->text().contains(QStringLiteral("no caber")));
        d.findChild<TextArea*>(QStringLiteral("aiSource"))->setPlainText(QString(AiCaseGenerationDialog::kLongPrompt, QLatin1Char('x')));
        QVERIFY(size->text().contains(QStringLiteral("no caber")));
    }

    void generatesWithTheConfiguredAiAndReviewsTheAnswer() {
        AiCaseGenerationDialog d(request(), QStringLiteral("al plan PL-0002"), {}, {});
        auto* generate = d.findChild<QPushButton*>(QStringLiteral("aiGenerate"));
        QVERIFY(generate->isHidden());   // sin IA configurada, sólo copiar
        QStringList sent;
        std::function<void(const AiCompletion&)> pending;
        d.setGenerator(QStringLiteral("Anthropic (Claude) · claude-sonnet-5"),
                       [&](const QString& prompt, std::function<void(const AiCompletion&)> done) { sent << prompt; pending = done; });
        QVERIFY(!generate->isHidden());
        QVERIFY(d.findChild<QLabel*>(QStringLiteral("aiDestination"))->text().contains(QStringLiteral("claude-sonnet-5")));
        d.findChild<TextArea*>(QStringLiteral("aiSource"))->setPlainText(QStringLiteral("Alcance revisado"));
        generate->click();
        QCOMPARE(sent.size(), 1);
        QVERIFY(sent.first().contains(QStringLiteral("Alcance revisado")));   // se envía lo revisado
        QVERIFY(!generate->isEnabled());
        generate->click();
        QCOMPARE(sent.size(), 1);   // mientras espera no se envía otra vez

        pending(AiCompletion{true, QStringLiteral("{\"cases\":[{\"title\":\"A\",\"steps\":[{\"action\":\"x\",\"expected\":\"y\"}]}]}"),
                             true, QStringLiteral("claude-sonnet-5"), {}, false});
        QVERIFY(generate->isEnabled());
        QCOMPARE(d.findChild<QTabWidget*>(QStringLiteral("aiTabs"))->currentIndex(), 1);
        QCOMPARE(d.findChild<QListWidget*>(QStringLiteral("aiCases"))->count(), 1);
        QVERIFY(d.findChild<QLabel*>(QStringLiteral("aiStatus"))->text().contains(QStringLiteral("cortada")));
    }

    void aGenerationErrorStaysOnThePromptTab() {
        AiCaseGenerationDialog d(request(), QStringLiteral("al plan PL-0002"), {}, {});
        d.setGenerator(QStringLiteral("OpenAI (ChatGPT) · gpt"), [](const QString&, std::function<void(const AiCompletion&)> done) {
            done(AiCompletion{false, {}, false, {}, QStringLiteral("OpenAI rechazó la clave de API"), false});
        });
        d.generate();
        QCOMPARE(d.findChild<QTabWidget*>(QStringLiteral("aiTabs"))->currentIndex(), 0);
        QVERIFY(d.findChild<QLabel*>(QStringLiteral("aiCopied"))->text().contains(QStringLiteral("rechazó la clave")));
        QVERIFY(d.findChild<QPushButton*>(QStringLiteral("aiGenerate"))->isEnabled());
    }

    void reportsAnUnreadableResponse() {
        AiCaseGenerationDialog d(request(), QStringLiteral("al plan PL-0002"), {}, {});
        d.findChild<TextArea*>(QStringLiteral("aiResponse"))->setPlainText(QStringLiteral("No puedo ayudarte"));
        d.findChild<QPushButton*>(QStringLiteral("aiInterpret"))->click();
        QCOMPARE(d.findChild<QListWidget*>(QStringLiteral("aiCases"))->count(), 0);
        QVERIFY(d.findChild<QLabel*>(QStringLiteral("aiStatus"))->text().contains(QStringLiteral("JSON")));
        QVERIFY(!d.findChild<QPushButton*>(QStringLiteral("aiAccept"))->isEnabled());
    }
};

QTEST_MAIN(AiCaseGenerationDialogTest)
#include "test_ai_case_generation_dialog.moc"
