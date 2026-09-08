// GifEncoder (infrastructure/capture/GifEncoder.h): cuantización y escritura de GIF animados,
// comprobadas leyendo el resultado con el plugin GIF de Qt.

#include "infrastructure/capture/GifEncoder.h"

#include <QImageReader>
#include <QtTest>

using namespace qaflow;

namespace {
QImage solid(int w, int h, const QColor& c) {
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(c);
    return img;
}
/// Degradado con muchos más de 256 colores.
QImage gradient(int w, int h) {
    QImage img(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) img.setPixelColor(x, y, QColor(x * 255 / std::max(1, w - 1), y * 255 / std::max(1, h - 1), (x + y) % 256));
    return img;
}
int distance(const QColor& a, const QColor& b) {
    return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue());
}
} // namespace

class GifEncoderTest : public QObject {
    Q_OBJECT
private slots:
    void quantizeKeepsExactColorsWhenTheyFit() {
        QImage img = solid(10, 4, Qt::red);
        for (int x = 0; x < 5; ++x) img.setPixelColor(x, 0, Qt::blue);
        QVector<QRgb> palette;
        QByteArray indices;
        GifEncoder::quantize(img, &palette, &indices);
        QCOMPARE(palette.size(), 2);
        QCOMPARE(indices.size(), 40);
        QCOMPARE(palette[static_cast<uint8_t>(indices[0])], qRgb(0, 0, 255));
        QCOMPARE(palette[static_cast<uint8_t>(indices[39])], qRgb(255, 0, 0));
    }

    void quantizeReducesManyColorsToAtMost256CloseOnes() {
        const QImage img = gradient(120, 80);
        QVector<QRgb> palette;
        QByteArray indices;
        GifEncoder::quantize(img, &palette, &indices);
        QVERIFY(palette.size() > 64);
        QVERIFY(palette.size() <= 256);
        int worst = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x)
                worst = std::max(worst, distance(img.pixelColor(x, y), QColor(palette[static_cast<uint8_t>(indices[y * img.width() + x])])));
        QVERIFY2(worst < 96, qPrintable(QStringLiteral("error máximo %1").arg(worst)));
    }

    void writesAnAnimatedGifThatQtReadsBack() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("anim.gif"));
        GifEncoder enc;
        QVERIFY(enc.open(path, QSize(64, 40)));
        QVERIFY(enc.addFrame(solid(64, 40, Qt::red), 100));
        QVERIFY(enc.addFrame(solid(64, 40, QColor(0, 128, 255)), 250));
        QVERIFY(enc.addFrame(gradient(64, 40), 100));
        QVERIFY(enc.addFrame(solid(128, 80, Qt::green), 100));   // se reescala al tamaño del GIF
        QCOMPARE(enc.frameCount(), 4);
        QVERIFY(enc.finish());

        QImageReader reader(path);
        QVERIFY2(reader.canRead(), qPrintable(reader.errorString()));
        QVERIFY(reader.supportsAnimation());
        QCOMPARE(reader.imageCount(), 4);
        QCOMPARE(reader.loopCount(), -1);   // infinito
        const QImage f1 = reader.read();
        QCOMPARE(f1.size(), QSize(64, 40));
        QCOMPARE(f1.pixelColor(10, 10), QColor(Qt::red));
        QCOMPARE(reader.nextImageDelay(), 100);   // retardo del fotograma recién leído
        const QImage f2 = reader.read();
        QCOMPARE(f2.pixelColor(10, 10), QColor(0, 128, 255));
        QCOMPARE(reader.nextImageDelay(), 250);
        const QImage f3 = reader.read();
        QVERIFY(distance(f3.pixelColor(0, 0), gradient(64, 40).pixelColor(0, 0)) < 48);
        QVERIFY(distance(f3.pixelColor(63, 39), gradient(64, 40).pixelColor(63, 39)) < 48);
        const QImage f4 = reader.read();
        QCOMPARE(f4.size(), QSize(64, 40));
        QCOMPARE(f4.pixelColor(5, 5), QColor(Qt::green));
    }

    void largeFrameExercisesLzwTableResets() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("big.gif"));
        GifEncoder enc;
        QVERIFY(enc.open(path, QSize(400, 300)));
        QVERIFY(enc.addFrame(gradient(400, 300), 100));   // > 4096 códigos LZW: fuerza varios CLEAR
        QVERIFY(enc.finish());
        QImageReader reader(path);
        const QImage f = reader.read();
        QVERIFY2(!f.isNull(), qPrintable(reader.errorString()));
        QCOMPARE(f.size(), QSize(400, 300));
        // Un degradado 3D de 120 000 colores en 256 entradas: se mide el error medio, no el máximo.
        const QImage src = gradient(400, 300);
        long total = 0, samples = 0;
        int worst = 0;
        for (int y = 0; y < src.height(); y += 7)
            for (int x = 0; x < src.width(); x += 7) {
                const int d = distance(f.pixelColor(x, y), src.pixelColor(x, y));
                total += d; ++samples;
                worst = std::max(worst, d);
            }
        QVERIFY2(total / samples < 40, qPrintable(QStringLiteral("error medio %1").arg(total / samples)));
        QVERIFY2(worst < 200, qPrintable(QStringLiteral("error máximo %1").arg(worst)));   // el degradado 3D es el peor caso para 256 colores
    }

    void openFailsOnUnwritablePath() {
        GifEncoder enc;
        QVERIFY(!enc.open(QStringLiteral("/no/existe/x.gif"), QSize(4, 4)));
        QVERIFY(!enc.error().isEmpty());
        QVERIFY(!enc.addFrame(solid(4, 4, Qt::red), 100));
        QVERIFY(!enc.finish());
    }
};

QTEST_GUILESS_MAIN(GifEncoderTest)
#include "test_gif_encoder.moc"
