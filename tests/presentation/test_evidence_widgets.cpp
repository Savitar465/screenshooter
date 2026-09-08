// Widgets de evidencias (presentation/widgets): renderizado de anotaciones, editor de
// anotaciones, visor a tamaño completo y miniaturas de ficheros que no son imagen.
// Se ejecuta con la plataforma "offscreen".

#include "presentation/widgets/AnnotationEditor.h"
#include "presentation/widgets/ImageViewer.h"
#include "presentation/widgets/ShotCard.h"
#include "presentation/widgets/Thumbnail.h"

#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QtTest>

using namespace qaflow;

namespace {
QImage white(int w, int h) {
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(Qt::white);
    return img;
}
QString save(QTemporaryDir& dir, const QString& name, const QImage& img) {
    const QString path = dir.filePath(name);
    img.save(path);
    return path;
}
} // namespace

class EvidenceWidgetsTest : public QObject {
    Q_OBJECT
private slots:
    // ---- renderAnnotations ---------------------------------------------------------------

    void rectangleAndArrowDrawWithTheirColor() {
        Annotation rect;
        rect.tool = Annotation::Tool::Rectangle;
        rect.from = QPointF(10, 10); rect.to = QPointF(50, 40);
        rect.color = Qt::red; rect.width = 4;
        Annotation arrow;
        arrow.tool = Annotation::Tool::Arrow;
        arrow.from = QPointF(60, 60); arrow.to = QPointF(90, 60);
        arrow.color = Qt::blue; arrow.width = 3;
        const QImage out = renderAnnotations(white(100, 100), {rect, arrow});
        QCOMPARE(out.size(), QSize(100, 100));
        QCOMPARE(out.pixelColor(30, 10), QColor(Qt::red));      // borde superior
        QCOMPARE(out.pixelColor(30, 25), QColor(Qt::white));    // interior sin relleno
        QCOMPARE(out.pixelColor(70, 60), QColor(Qt::blue));     // cuerpo de la flecha
        QCOMPARE(out.pixelColor(5, 5), QColor(Qt::white));      // fuera de todo
    }

    void blurPixelatesWithoutLeakingOutside() {
        // Tablero fino: tras pixelar, el bloque queda uniforme (gris) en lugar de alternar.
        QImage img(80, 80, QImage::Format_ARGB32);
        for (int y = 0; y < 80; ++y) for (int x = 0; x < 80; ++x) img.setPixelColor(x, y, ((x + y) % 2) ? Qt::black : Qt::white);
        Annotation blur;
        blur.tool = Annotation::Tool::Blur;
        blur.from = QPointF(20, 20); blur.to = QPointF(60, 60);
        const QImage out = renderAnnotations(img, {blur});
        const QColor a = out.pixelColor(30, 30), b = out.pixelColor(31, 30);
        QVERIFY(std::abs(a.red() - b.red()) < 40);        // ya no alterna
        QVERIFY(a.red() > 60 && a.red() < 200);            // gris intermedio
        QCOMPARE(out.pixelColor(5, 5), img.pixelColor(5, 5));   // fuera intacto
        QCOMPARE(out.pixelColor(6, 5), img.pixelColor(6, 5));
    }

    void textDrawsABackgroundBox() {
        Annotation text;
        text.tool = Annotation::Tool::Text;
        text.from = text.to = QPointF(10, 10);
        text.text = QStringLiteral("Bug aquí");
        text.color = Qt::yellow;
        const QImage out = renderAnnotations(white(200, 100), {text});
        QVERIFY(out.pixelColor(14, 14) != QColor(Qt::white));   // caja oscura tras el texto
        QCOMPARE(out.pixelColor(190, 90), QColor(Qt::white));
    }

    void emptyListLeavesTheImageUntouched() {
        const QImage base = white(20, 20);
        QCOMPARE(renderAnnotations(base, {}).pixelColor(3, 3), QColor(Qt::white));
    }

    // ---- AnnotationEditor ------------------------------------------------------------------

    void editorAppliesAnnotationsAndUndoRemovesTheLast() {
        AnnotationEditor editor(white(120, 80));
        editor.show();
        QVERIFY(editor.annotations().isEmpty());
        Annotation a;
        a.tool = Annotation::Tool::Highlight;
        a.from = QPointF(0, 0); a.to = QPointF(120, 80);
        a.color = Qt::red;
        editor.addAnnotation(a);
        Annotation b = a;
        b.tool = Annotation::Tool::Rectangle;
        b.from = QPointF(10, 10); b.to = QPointF(30, 30);
        editor.addAnnotation(b);
        QCOMPARE(editor.annotations().size(), 2);
        editor.undo();
        QCOMPARE(editor.annotations().size(), 1);
        const QImage out = editor.result();
        const QColor c = out.pixelColor(60, 40);
        QVERIFY(c.red() > 200 && c.green() < 220);   // blanco teñido de rojo
        editor.undo();
        editor.undo();   // sin nada que deshacer no falla
        QVERIFY(editor.annotations().isEmpty());
    }

    void editorToolShortcutsSwitchTools() {
        AnnotationEditor editor(white(50, 50));
        editor.show();
        QTest::keyClick(&editor, Qt::Key_D);
        bool blurActive = false;
        for (auto* b : editor.findChildren<QPushButton*>())
            if (b->property("tool").isValid() && b->property("active").toBool()) blurActive = b->property("tool").toInt() == static_cast<int>(Annotation::Tool::Blur);
        QVERIFY(blurActive);
    }

    // ---- ImageViewer ---------------------------------------------------------------------

    void viewerNavigatesAndZooms() {
        QTemporaryDir dir;
        QList<Screenshot> shots;
        shots << Screenshot{1, 1, QStringLiteral("cap_001.png"), save(dir, QStringLiteral("cap_001.png"), white(400, 300))};
        shots << Screenshot{2, 0, QStringLiteral("cap_002.png"), save(dir, QStringLiteral("cap_002.png"), white(40, 30))};
        shots << Screenshot{3, 2, QStringLiteral("adj_003_app.log"), dir.filePath(QStringLiteral("adj_003_app.log"))};
        { QFile f(shots[2].path); f.open(QIODevice::WriteOnly); f.write("log"); }

        auto* viewer = new ImageViewer(shots, 1);
        viewer->resize(800, 600);
        viewer->show();
        QVERIFY(QTest::qWaitForWindowExposed(viewer));
        QCOMPARE(viewer->current().id, 2);
        QCOMPARE(viewer->zoom(), 1.0);   // ajustar nunca amplía una imagen pequeña
        QTest::keyClick(viewer, Qt::Key_Left);
        QCOMPARE(viewer->current().id, 1);
        QVERIFY(viewer->zoom() <= 1.0);
        viewer->setZoom(2.0);
        QCOMPARE(viewer->zoom(), 2.0);
        QTest::keyClick(viewer, Qt::Key_0);
        QVERIFY(viewer->zoom() < 2.0);
        QTest::keyClick(viewer, Qt::Key_Left);   // ya en la primera
        QCOMPARE(viewer->current().id, 1);
        QTest::keyClick(viewer, Qt::Key_End);
        QCOMPARE(viewer->current().id, 3);   // fichero que no es imagen: panel con el nombre
        bool nameShown = false;
        for (auto* l : viewer->findChildren<QLabel*>()) if (l->isVisible() && l->text() == QStringLiteral("adj_003_app.log")) nameShown = true;
        QVERIFY(nameShown);

        QSignalSpy annotate(viewer, &ImageViewer::annotateRequested);
        QSignalSpy copy(viewer, &ImageViewer::copyRequested);
        QTest::keyClick(viewer, Qt::Key_Home);
        QTest::keyClick(viewer, Qt::Key_E, Qt::ControlModifier);
        QTest::keyClick(viewer, Qt::Key_C, Qt::ControlModifier);
        QCOMPARE(annotate.count(), 1);
        QCOMPARE(annotate.first().at(0).toInt(), 1);
        QCOMPARE(copy.count(), 1);
        QTest::keyClick(viewer, Qt::Key_Escape);
        QTRY_VERIFY(!viewer->isVisible());
    }

    // ---- Thumbnail y ShotCard --------------------------------------------------------------

    void thumbnailClickOpensAndCardOffersAnnotateOnlyForImages() {
        QTemporaryDir dir;
        const Screenshot image{1, 0, QStringLiteral("cap_001.png"), save(dir, QStringLiteral("cap_001.png"), white(40, 30))};
        const Screenshot log{2, 0, QStringLiteral("adj_002_x.log"), dir.filePath(QStringLiteral("adj_002_x.log"))};
        { QFile f(log.path); f.open(QIODevice::WriteOnly); f.write("x"); }

        ShotCard card(image, {}, ShotCard::Layout::Grid);
        card.show();
        QSignalSpy open(&card, &ShotCard::openRequested);
        auto* thumb = card.findChild<Thumbnail*>();
        QVERIFY(thumb);
        QTest::mouseClick(thumb, Qt::LeftButton);
        QCOMPARE(open.count(), 1);
        QCOMPARE(open.first().at(0).toInt(), 1);
        int annotateButtons = 0;
        for (auto* b : card.findChildren<QPushButton*>()) if (b->text() == QStringLiteral("✎") && !b->isHidden()) ++annotateButtons;
        QCOMPARE(annotateButtons, 1);

        ShotCard logCard(log, {}, ShotCard::Layout::Compact);
        logCard.show();
        int hiddenAnnotate = 0;
        for (auto* b : logCard.findChildren<QPushButton*>()) if (b->text() == QStringLiteral("✎") && b->isHidden()) ++hiddenAnnotate;
        QCOMPARE(hiddenAnnotate, 1);
        QVERIFY(!log.isImage());
        QVERIFY(!logCard.findChild<Thumbnail*>()->toolTip().isEmpty());
    }
};

QTEST_MAIN(EvidenceWidgetsTest)
#include "test_evidence_widgets.moc"
