// elideTitle (core/Text.h): los títulos que QAflow deriva del issue (el del gestor y el del plan) se
// acortan con «…» cuando no caben, cortando por palabras enteras.

#include "core/Text.h"

#include <QtTest>

using namespace qaflow;

class TextTest : public QObject {
    Q_OBJECT
private slots:
    void aShortTitleIsLeftAlone() {
        QCOMPARE(elideTitle(QStringLiteral("Desarrollo del laboratorio"), 40), QStringLiteral("Desarrollo del laboratorio"));
        QCOMPARE(elideTitle(QStringLiteral("  Espacios   de   sobra  "), 40), QStringLiteral("Espacios de sobra"));
        QVERIFY(elideTitle(QString(), 40).isEmpty());
    }

    void aLongTitleIsCutByWholeWordsAndEndsInEllipsis() {
        const QString title = QStringLiteral("Desarrollo complementario del laboratorio de control de calidad");
        const QString elided = elideTitle(title, 30);
        QCOMPARE(elided, QStringLiteral("Desarrollo complementario…"));
        QVERIFY(elided.size() <= 30);
    }

    void punctuationLeftHangingIsRemoved() {
        QCOMPARE(elideTitle(QStringLiteral("Alta de usuarios, bajas y cambios de perfil"), 20), QStringLiteral("Alta de usuarios…"));
    }

    void aSingleLongWordIsCutWhereItFits() {
        // Sin espacio por el que cortar (o demasiado al principio) se corta la palabra: es preferible a
        // devolver un título vacío.
        QCOMPARE(elideTitle(QStringLiteral("Supercalifragilisticoespialidoso"), 10), QStringLiteral("Supercali…"));
        QCOMPARE(elideTitle(QStringLiteral("A supercalifragilisticoespialidoso"), 10), QStringLiteral("A superca…"));
    }
};

QTEST_APPLESS_MAIN(TextTest)
#include "test_text.moc"
