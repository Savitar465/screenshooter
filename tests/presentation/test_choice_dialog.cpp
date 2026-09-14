// ChoiceDialog (presentation/widgets): selector con búsqueda sobre una lista que se consulta a un sistema
// externo (proyectos de Jira, catálogo de sistemas de GESREQ). Se ejecuta con la plataforma "offscreen".

#include "presentation/widgets/ChoiceDialog.h"

#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSignalSpy>
#include <QtTest>

using namespace qaflow;

namespace {
QList<Choice> systems() {
    return {Choice{QStringLiteral("SEGRAN"), QStringLiteral("SISTEMA PARA LA ESTRATEGIA DE GESTIÓN DE RIESGOS"), QString()},
            Choice{QStringLiteral("SUMA TRANSITO"), QStringLiteral("TRANSITOS"), QStringLiteral("en tu bandeja")},
            Choice{QStringLiteral("ADUS"), QStringLiteral("SISTEMA DE GESTIÓN DOCUMENTAL Y ARCHIVO"), QString()}};
}

/// Consulta falsa: responde en el acto con la lista o con el error, o guarda la respuesta (`deferred`)
/// para darla después, como una petición que tarda.
struct Source {
    QList<Choice> choices = systems();
    QString error;
    bool deferred = false;
    int calls = 0;
    ChoiceDialog::Loaded pending;

    ChoiceDialog::Loader loader() {
        return [this](const ChoiceDialog::Loaded& done) {
            ++calls;
            if (deferred) pending = done;
            else done(error.isEmpty() ? choices : QList<Choice>(), error);
        };
    }
};

QStringList values(const ChoiceDialog& dialog) {
    QStringList out;
    const auto* list = dialog.findChild<QListWidget*>(QStringLiteral("choiceList"));
    for (int i = 0; i < list->count(); ++i) out << list->item(i)->data(Qt::UserRole).toString();
    return out;
}

QString markedValue(const ChoiceDialog& dialog) {
    const auto* item = dialog.findChild<QListWidget*>(QStringLiteral("choiceList"))->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}
} // namespace

class ChoiceDialogTest : public QObject {
    Q_OBJECT
private slots:
    void listsWhatTheSourceReturnsAndMarksTheCurrentValue() {
        Source source;
        ChoiceDialog dialog(QStringLiteral("Sistema de GESREQ"), QStringLiteral("Consultando…"), source.loader(), QStringLiteral("suma transito"));
        QCOMPARE(source.calls, 1);
        QCOMPARE(values(dialog), QStringList({QStringLiteral("SEGRAN"), QStringLiteral("SUMA TRANSITO"), QStringLiteral("ADUS")}));
        QCOMPARE(markedValue(dialog), QStringLiteral("SUMA TRANSITO"));
        const QString text = dialog.findChild<QListWidget*>(QStringLiteral("choiceList"))->currentItem()->text();
        QVERIFY2(text.contains(QStringLiteral("TRANSITOS")) && text.contains(QStringLiteral("en tu bandeja")), qPrintable(text));
        QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("choiceStatus"))->text().contains(QStringLiteral("3")));
    }

    // Se busca por código o nombre, palabra a palabra y sin tildes.
    void searchFiltersByCodeAndNameWordByWordWithoutAccents() {
        QVERIFY(ChoiceDialog::matches(systems()[0], QStringLiteral("gestion  RIESGOS")));
        QVERIFY(ChoiceDialog::matches(systems()[1], QStringLiteral("suma trans")));
        QVERIFY(!ChoiceDialog::matches(systems()[1], QStringLiteral("suma riesgos")));
        QVERIFY(ChoiceDialog::matches(systems()[2], QString()));

        Source source;
        ChoiceDialog dialog(QStringLiteral("Sistema de GESREQ"), QStringLiteral("Consultando…"), source.loader(), QString());
        // Con tilde: QTest sólo teclea ASCII, así que el texto se pone de una vez, como al pegarlo.
        dialog.findChild<QLineEdit*>(QStringLiteral("choiceSearch"))->setText(QStringLiteral("gestión"));
        QCOMPARE(values(dialog), QStringList({QStringLiteral("SEGRAN"), QStringLiteral("ADUS")}));
        QCOMPARE(markedValue(dialog), QStringLiteral("SEGRAN"));   // el primero, para elegir con Intro
        QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("choiceStatus"))->text().contains(QStringLiteral("2")));
    }

    void arrowsInTheSearchMoveTheSelection() {
        Source source;
        ChoiceDialog dialog(QStringLiteral("Sistema de GESREQ"), QStringLiteral("Consultando…"), source.loader(), QString());
        auto* search = dialog.findChild<QLineEdit*>(QStringLiteral("choiceSearch"));
        QCOMPARE(markedValue(dialog), QStringLiteral("SEGRAN"));
        QTest::keyClick(search, Qt::Key_Down);
        QCOMPARE(markedValue(dialog), QStringLiteral("SUMA TRANSITO"));
        QTest::keyClicks(search, "a");   // la búsqueda sigue recibiendo lo que se escribe
        QCOMPARE(search->text(), QStringLiteral("a"));
    }

    void choosingEmitsTheValueAndCloses() {
        Source source;
        ChoiceDialog dialog(QStringLiteral("Sistema de GESREQ"), QStringLiteral("Consultando…"), source.loader(), QString());
        dialog.show();
        QSignalSpy chosen(&dialog, &ChoiceDialog::chosen);
        auto* search = dialog.findChild<QLineEdit*>(QStringLiteral("choiceSearch"));
        QTest::keyClicks(search, "archivo");
        QTest::keyClick(search, Qt::Key_Return);
        QCOMPARE(chosen.count(), 1);
        QCOMPARE(chosen.first().first().toString(), QStringLiteral("ADUS"));
        QCOMPARE(dialog.result(), int(QDialog::Accepted));
    }

    void nothingIsChosenWhenTheSearchFindsNothing() {
        Source source;
        ChoiceDialog dialog(QStringLiteral("Sistema de GESREQ"), QStringLiteral("Consultando…"), source.loader(), QString());
        QSignalSpy chosen(&dialog, &ChoiceDialog::chosen);
        QTest::keyClicks(dialog.findChild<QLineEdit*>(QStringLiteral("choiceSearch")), "no existe");
        QVERIFY(values(dialog).isEmpty());
        QVERIFY(!dialog.findChild<QPushButton*>(QStringLiteral("choiceAccept"))->isEnabled());
        dialog.accept();
        QCOMPARE(chosen.count(), 0);
    }

    void aFailedQueryShowsTheErrorAndCanBeRetried() {
        Source source;
        source.error = QStringLiteral("GESREQ rechazó el usuario o la contraseña");
        ChoiceDialog dialog(QStringLiteral("Sistema de GESREQ"), QStringLiteral("Consultando…"), source.loader(), QString());
        auto* status = dialog.findChild<QLabel*>(QStringLiteral("choiceStatus"));
        auto* retry = dialog.findChild<QPushButton*>(QStringLiteral("choiceRetry"));
        QVERIFY2(status->text().contains(QStringLiteral("rechazó")), qPrintable(status->text()));
        QVERIFY(!retry->isHidden());
        QVERIFY(values(dialog).isEmpty());
        QVERIFY(!dialog.findChild<QPushButton*>(QStringLiteral("choiceAccept"))->isEnabled());
        // Escribir en la búsqueda no borra el error: no hay lista que filtrar.
        QTest::keyClicks(dialog.findChild<QLineEdit*>(QStringLiteral("choiceSearch")), "segran");
        QVERIFY(status->text().contains(QStringLiteral("rechazó")));

        source.error.clear();
        retry->click();
        QCOMPARE(source.calls, 2);
        QVERIFY(retry->isHidden());
        QCOMPARE(values(dialog), QStringList({QStringLiteral("SEGRAN")}));   // con la búsqueda que ya estaba escrita
    }

    // La respuesta que llega tarde (ventana cerrada, o una consulta que se repitió después) no toca nada.
    void aLateAnswerIsIgnored() {
        Source source;
        source.deferred = true;
        ChoiceDialog dialog(QStringLiteral("Proyecto de Jira"), QStringLiteral("Consultando los proyectos de Jira…"), source.loader(), QString());
        QCOMPARE(dialog.findChild<QLabel*>(QStringLiteral("choiceStatus"))->text(), QStringLiteral("Consultando los proyectos de Jira…"));
        const ChoiceDialog::Loaded first = source.pending;
        dialog.findChild<QPushButton*>(QStringLiteral("choiceRetry"))->click();   // otra consulta
        const ChoiceDialog::Loaded second = source.pending;
        first({Choice{QStringLiteral("VIEJO"), QString(), QString()}}, QString());
        QVERIFY(values(dialog).isEmpty());
        second(systems(), QString());
        QCOMPARE(values(dialog).size(), 3);

        source.pending = {};
        auto* closed = new ChoiceDialog(QStringLiteral("Proyecto de Jira"), QStringLiteral("Consultando…"), source.loader(), QString());
        const ChoiceDialog::Loaded orphan = source.pending;
        delete closed;
        orphan(systems(), QString());   // no hay ventana a la que llegar, y no pasa nada
    }
};

QTEST_MAIN(ChoiceDialogTest)
#include "test_choice_dialog.moc"
