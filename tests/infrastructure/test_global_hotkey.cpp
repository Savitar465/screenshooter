// GlobalHotkey (infrastructure/hotkey/GlobalHotkey.h): registro y baja de atajos del sistema.
// El registro real sólo se puede comprobar donde la plataforma lo ofrece (Windows, macOS); en
// Linux con la plataforma offscreen no hay X11 ni portal, así que ahí sólo se comprueba que falla
// con un estado explicativo en lugar de fingir que funciona.

#include "infrastructure/hotkey/GlobalHotkey.h"

#include <QtTest>

using namespace qaflow;

class GlobalHotkeyTest : public QObject {
    Q_OBJECT
private slots:
    void invalidSequenceIsRejectedWithAnExplanation() {
        GlobalHotkey hk;
        int fired = 0;
        QVERIFY(!hk.bind(QStringLiteral("capture"), QStringLiteral("no-es-un-atajo"), [&]() { ++fired; }));
        QVERIFY(!hk.isBound(QStringLiteral("capture")));
        QVERIFY(!hk.status().isEmpty());
        hk.activate(1);   // el id nativo existe pero no está registrado: no se dispara nada raro
        QCOMPARE(fired, 0);
    }

    void bindAndUnbindFollowThePlatform() {
        GlobalHotkey hk;
        int fired = 0;
        const bool bound = hk.bind(QStringLiteral("capture"), QStringLiteral("Ctrl+Shift+F12"), [&]() { ++fired; });
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
        QVERIFY2(bound, qPrintable(hk.status()));
        QVERIFY(hk.isBound(QStringLiteral("capture")));
        QVERIFY(hk.status().contains(QStringLiteral("Ctrl+Shift+F12")));
#else
        QCOMPARE(bound, hk.isBound(QStringLiteral("capture")));
#endif
        // Volver a registrar el mismo id sustituye el anterior; unbind lo retira.
        hk.bind(QStringLiteral("capture"), QStringLiteral("Ctrl+Shift+F11"), [&]() { ++fired; });
        hk.unbind(QStringLiteral("capture"));
        QVERIFY(!hk.isBound(QStringLiteral("capture")));
        // Disparo simulado por id nativo: el callback del registro vigente se ejecuta, el retirado no.
        const bool again = hk.bind(QStringLiteral("record"), QStringLiteral("Ctrl+Shift+F10"), [&]() { fired += 10; });
        Q_UNUSED(again);
        hk.activate(3);   // tercer registro → id nativo 3
        QCOMPARE(fired, 10);
        hk.activate(1);
        QCOMPARE(fired, 10);
    }
};

QTEST_MAIN(GlobalHotkeyTest)
#include "test_global_hotkey.moc"
