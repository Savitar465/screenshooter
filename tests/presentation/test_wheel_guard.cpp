// WheelGuard (presentation/widgets): la rueda del ratón no cambia combos ni spin boxes al pasar por
// encima; el evento sigue al contenedor, que desplaza la pantalla. Se ejecuta con la plataforma "offscreen".

#include "presentation/widgets/WheelGuard.h"

#include <QComboBox>
#include <QDateEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtTest/qtestspontaneevent.h>
#include <QtTest>

using namespace qaflow;

namespace {
/// Rueda hacia abajo sobre el centro del widget, marcada como del sistema: sólo esas suben al
/// contenedor cuando el widget no las acepta.
void wheelDown(QWidget* w) {
    const QPointF pos = QRectF(w->rect()).center();
    QWheelEvent e(pos, w->mapToGlobal(pos), QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QSpontaneKeyEvent::setSpontaneous(&e);
    qApp->notify(w, &e);   // sendEvent le quitaría la marca de «del sistema»
}
} // namespace

class TestWheelGuard : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() { WheelGuard::install(); }

    void wheelScrollsThePageInsteadOfChangingFields() {
        QScrollArea area;
        auto* content = new QWidget;
        auto* layout = new QVBoxLayout(content);
        auto* combo = new QComboBox;
        combo->addItems({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
        auto* spin = new QSpinBox;
        spin->setRange(0, 10);
        spin->setValue(5);
        auto* date = new QDateEdit(QDate(2026, 9, 22));
        layout->addWidget(combo);
        layout->addWidget(spin);
        layout->addWidget(date);
        layout->addSpacing(2000);
        area.setWidget(content);
        area.resize(300, 200);
        area.show();
        QVERIFY(QTest::qWaitForWindowExposed(&area));

        for (QWidget* w : {static_cast<QWidget*>(combo), static_cast<QWidget*>(spin), static_cast<QWidget*>(date)}) {
            const int before = area.verticalScrollBar()->value();
            wheelDown(w);
            QVERIFY(area.verticalScrollBar()->value() > before);
        }
        QCOMPARE(combo->currentIndex(), 0);
        QCOMPARE(spin->value(), 5);
        QCOMPARE(date->date(), QDate(2026, 9, 22));
    }
};

QTEST_MAIN(TestWheelGuard)
#include "test_wheel_guard.moc"
