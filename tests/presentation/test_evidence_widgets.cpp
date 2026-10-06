// Widgets de evidencias (presentation/widgets): renderizado de anotaciones, editor de
// anotaciones, visor a tamaño completo y miniaturas de ficheros que no son imagen.
// Se ejecuta con la plataforma "offscreen".

#include "presentation/widgets/AnnotationEditor.h"
#include "presentation/widgets/ImageViewer.h"
#include "presentation/widgets/ShotCard.h"
#include "presentation/widgets/Thumbnail.h"

#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
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

    void textDrawsOnlyTheLettersWithoutBackground() {
        Annotation text;
        text.tool = Annotation::Tool::Text;
        text.from = text.to = QPointF(10, 10);
        text.text = QStringLiteral("Bug aquí");
        text.color = Qt::yellow;
        const QImage out = renderAnnotations(white(200, 100), {text});
        const QRect box = textBounds(text).toRect().intersected(out.rect());
        int painted = 0;
        for (int y = box.top(); y <= box.bottom(); ++y) {
            for (int x = box.left(); x <= box.right(); ++x) {
                const QColor c = out.pixelColor(x, y);
                QVERIFY(c.red() > 200 && c.green() > 200);   // sólo blanco o amarillo: sin caja oscura
                if (c != QColor(Qt::white)) ++painted;
            }
        }
        QVERIFY(painted > 0);
        QCOMPARE(out.pixelColor(190, 90), QColor(Qt::white));
    }

    void textSizeGrowsTheBounds() {
        Annotation text;
        text.tool = Annotation::Tool::Text;
        text.text = QStringLiteral("Hola");
        text.fontSize = 20;
        const QRectF small = textBounds(text);
        text.fontSize = 40;
        const QRectF big = textBounds(text);
        QVERIFY(big.width() > small.width() * 1.5);
        QVERIFY(big.height() > small.height() * 1.5);
    }

    void editorMovesResizesAndUndoesText() {
        AnnotationEditor editor(white(400, 300));
        editor.show();
        QVERIFY(QTest::qWaitForWindowExposed(&editor));
        editor.setTool(Annotation::Tool::Text);
        Annotation t;
        t.tool = Annotation::Tool::Text;
        t.from = t.to = QPointF(20, 20);
        t.text = QStringLiteral("Texto");
        t.fontSize = 20;
        editor.addAnnotation(t);
        auto* canvas = editor.findChild<QWidget*>(QStringLiteral("annotationCanvas"));
        QVERIFY(canvas);
        QTRY_VERIFY(canvas->width() > 0);
        const double zoom = canvas->width() / 400.0;
        auto send = [&](QEvent::Type type, QPointF imagePos, Qt::MouseButtons buttons) {
            const QPointF pos = imagePos * zoom;
            QMouseEvent ev(type, pos, canvas->mapToGlobal(pos), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton, buttons, Qt::NoModifier);
            QApplication::sendEvent(canvas, &ev);
        };

        // Mover: arrastrar desde dentro del texto.
        const QPointF inside = textBounds(t).center();
        send(QEvent::MouseButtonPress, inside, Qt::LeftButton);
        send(QEvent::MouseMove, inside + QPointF(50, 30), Qt::LeftButton);
        send(QEvent::MouseButtonRelease, inside + QPointF(50, 30), Qt::NoButton);
        QCOMPARE(editor.annotations().size(), 1);
        const QPointF moved = editor.annotations().first().from;
        QVERIFY(std::abs(moved.x() - 70) < 2 && std::abs(moved.y() - 50) < 2);

        // Redimensionar: arrastrar el tirador de la esquina inferior derecha hacia abajo.
        const QRectF box = textBounds(editor.annotations().first());
        const QPointF handle = box.bottomRight() + QPointF(4, 4) / zoom;
        send(QEvent::MouseButtonPress, handle, Qt::LeftButton);
        send(QEvent::MouseMove, handle + QPointF(0, box.height()), Qt::LeftButton);
        send(QEvent::MouseButtonRelease, handle + QPointF(0, box.height()), Qt::NoButton);
        QVERIFY(editor.annotations().first().fontSize > 30);

        // Deshacer revierte el tamaño y luego la posición, sin borrar el texto.
        editor.undo();
        QCOMPARE(editor.annotations().first().fontSize, 20);
        editor.undo();
        QCOMPARE(editor.annotations().first().from, QPointF(20, 20));
        editor.undo();
        QVERIFY(editor.annotations().isEmpty());
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

    void editorOpensWithTheImageFittedToTheWindow() {
        AnnotationEditor editor(white(4000, 3000));
        editor.resize(900, 700);
        editor.show();
        QVERIFY(QTest::qWaitForWindowExposed(&editor));
        auto* scroll = editor.findChild<QScrollArea*>();
        QVERIFY(scroll);
        const QSize viewport = scroll->viewport()->size();
        QTRY_VERIFY(scroll->widget()->width() <= viewport.width() && scroll->widget()->height() <= viewport.height());
        // Ajustada: ocupa todo el ancho o todo el alto disponible, no una fracción.
        QVERIFY(scroll->widget()->width() >= viewport.width() - 4 || scroll->widget()->height() >= viewport.height() - 4);
    }

    void editorToolbarFitsASmallScreen() {
        AnnotationEditor editor(white(3000, 400));   // captura muy apaisada
        editor.show();
        // La barra (sólo iconos) cabe en una pantalla de 800 px y la ayuda no ensancha la ventana.
        QVERIFY2(editor.minimumSizeHint().width() <= 800, qPrintable(QString::number(editor.minimumSizeHint().width())));
        auto* save = editor.findChild<QPushButton*>(QStringLiteral("annotationSave"));
        QVERIFY(save);
        QCOMPARE(save->property("role").toString(), QStringLiteral("primary"));
        // Ningún contenedor del editor pisa el estilo de los botones con un fondo sin selector.
        for (auto* w = save->parentWidget(); w && w != &editor; w = w->parentWidget()) QVERIFY(w->styleSheet().isEmpty());
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

    void thumbnailClickAnnotatesImagesAndOpensOtherFiles() {
        QTemporaryDir dir;
        const Screenshot image{1, 0, QStringLiteral("cap_001.png"), save(dir, QStringLiteral("cap_001.png"), white(40, 30))};
        const Screenshot log{2, 0, QStringLiteral("adj_002_x.log"), dir.filePath(QStringLiteral("adj_002_x.log"))};
        { QFile f(log.path); f.open(QIODevice::WriteOnly); f.write("x"); }

        ShotCard card(image, {}, ShotCard::Layout::Grid);
        card.show();
        QSignalSpy open(&card, &ShotCard::openRequested);
        QSignalSpy annotate(&card, &ShotCard::annotateRequested);
        auto* thumb = card.findChild<Thumbnail*>();
        QVERIFY(thumb);
        QTest::mouseClick(thumb, Qt::LeftButton);
        // Una imagen fija va directa al editor de anotaciones.
        QCOMPARE(annotate.count(), 1);
        QCOMPARE(annotate.first().at(0).toInt(), 1);
        QCOMPARE(open.count(), 0);
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
        // Lo que no se puede anotar se abre en el visor.
        QSignalSpy openLog(&logCard, &ShotCard::openRequested);
        QTest::mouseClick(logCard.findChild<Thumbnail*>(), Qt::LeftButton);
        QCOMPARE(openLog.count(), 1);
    }

    // La imagen se lee en segundo plano la primera vez que la miniatura se enseña; una segunda
    // miniatura del mismo fichero la toma de la caché sin esperar. Un fichero que dice ser imagen y
    // no se puede leer acaba como fichero, sin colgar la miniatura en «cargando».
    void thumbnailsDecodeInTheBackgroundAndShareTheCache() {
        QTemporaryDir dir;
        const QString path = save(dir, QStringLiteral("cap_010.png"), white(1600, 1000));
        Thumbnail hidden(path, 1, 1);
        QVERIFY(!hidden.isLoaded());   // sin enseñarse no se lee nada
        QTest::qWait(20);
        QVERIFY(!hidden.isLoaded());

        Thumbnail first(path, 1, 1);
        first.setWidthHint(200);
        first.show();
        QTRY_VERIFY(first.isLoaded());
        Thumbnail second(path, 1, 2);
        QVERIFY(second.isLoaded());   // ya en la caché
        QTRY_VERIFY(hidden.isLoaded());   // y la que esperaba también se entera

        const QString broken = dir.filePath(QStringLiteral("cap_011.png"));
        { QFile f(broken); f.open(QIODevice::WriteOnly); f.write("no es un png"); }
        Thumbnail bad(broken, 1, 3);
        bad.show();
        QTRY_VERIFY(bad.isLoaded());
    }

    // La tarjeta del historial es de sólo lectura desde que se construye: sin selector de paso ni
    // controles de mover o borrar, con el paso escrito y la miniatura que sigue abriendo el editor.
    void archiveCardIsReadOnly() {
        QTemporaryDir dir;
        const Screenshot image{1, 2, QStringLiteral("cap_001.png"), save(dir, QStringLiteral("cap_001.png"), white(40, 30))};
        const QList<TestStep> steps{TestStep{QStringLiteral("Abrir"), {}, {}}, TestStep{QStringLiteral("Pagar con tarjeta"), {}, {}}};
        ShotCard card(image, steps, ShotCard::Layout::Archive);
        card.show();
        QVERIFY(!card.findChild<QComboBox*>());
        for (auto* b : card.findChildren<QPushButton*>())
            QVERIFY2(b->isHidden() || b->text() == QStringLiteral("✎"), qPrintable(b->text()));
        bool stepShown = false;
        for (auto* l : card.findChildren<QLabel*>()) stepShown = stepShown || l->text().contains(QStringLiteral("Paso 2 · Pagar con tarjeta"));
        QVERIFY(stepShown);
        QSignalSpy annotate(&card, &ShotCard::annotateRequested);
        QTest::mouseClick(card.findChild<Thumbnail*>(), Qt::LeftButton);
        QCOMPARE(annotate.count(), 1);
    }
};

QTEST_MAIN(EvidenceWidgetsTest)
#include "test_evidence_widgets.moc"
