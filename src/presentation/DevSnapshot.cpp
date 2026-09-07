#include "DevSnapshot.h"

#include "application/AppContext.h"
#include "presentation/views/MainWindow.h"

#include <QApplication>
#include <QDir>
#include <QTimer>

#include <functional>

namespace qaflow::devsnapshot {

bool requested() { return qEnvironmentVariableIsSet("QAFLOW_SNAPSHOT_DIR"); }

QString dataDir() { return QDir(qEnvironmentVariable("QAFLOW_SNAPSHOT_DIR")).filePath(QStringLiteral("data")); }

void run(MainWindow& window, AppContext& ctx) {
    const QString dir = qEnvironmentVariable("QAFLOW_SNAPSHOT_DIR");
    QDir().mkpath(dir);

    struct Shot { const char* name; std::function<void()> prepare; };
    const QList<Shot> shots = {
        {"01-casos", [&] { window.navigate(Screen::Casos); }},
        {"02-plan", [&] { window.navigate(Screen::Plan); }},
        {"03-ejecucion", [&] {
            ctx.run->start(QStringLiteral("TC-104"));
            ctx.run->mark(StepResult::Pass);
            ctx.run->setNote(QStringLiteral("El total no cambia al aplicar el cupón"));
            ctx.run->mark(StepResult::Fail);
            ctx.run->mark(StepResult::Skip);
            ctx.run->back();   // reabre el paso 3
            window.navigate(Screen::Run);
        }},
        {"03b-ejecucion-fin", [&] {
            ctx.run->mark(StepResult::Pass);
            ctx.run->mark(StepResult::Pass);
            window.navigate(Screen::Run);
        }},
        {"04-bug", [&] { window.navigate(Screen::Bug); }},
        {"07-historial-plan", [&] {
            ctx.run->finish();   // archiva TC-104 como ejecución suelta
            ctx.run->startSequence({QStringLiteral("TC-102"), QStringLiteral("TC-103"), QStringLiteral("TC-107")}, QStringLiteral("Regresión Sprint 14"), ctx.plan->activeId());
            ctx.run->mark(StepResult::Pass);
            ctx.run->setNote(QStringLiteral("El descuento no se refleja en el resumen"));
            ctx.run->mark(StepResult::Fail);
            ctx.run->finish();
            ctx.run->mark(StepResult::Pass);
            ctx.run->finish();
            ctx.run->setNote(QStringLiteral("El servicio de notificaciones está caído en staging"));
            ctx.run->mark(StepResult::Block);
            window.finishRun();   // termina el plan y abre su informe
        }},
        {"08-historial-caso", [&] { window.navigate(Screen::Casos); ctx.cases->select(QStringLiteral("TC-102")); }},
        {"09-planes", [&] { ctx.plan->createPlan(QStringLiteral("Smoke release 2.3")); ctx.plan->toggle(QStringLiteral("TC-101")); ctx.plan->setActive(QStringLiteral("PL-0001")); window.navigate(Screen::Plan); }},
        {"05-ajustes", [&] { window.navigate(Screen::Ajustes); window.showToast(QStringLiteral("Captura guardada en ~/QAflow/capturas"), QStringLiteral("#06b6d4")); }},
        {"06-casos-en-ejecucion", [&] { window.navigate(Screen::Casos); }},
    };

    auto* timer = new QTimer(&window);
    QObject::connect(timer, &QTimer::timeout, [&window, dir, timer, shots, i = 0]() mutable {
        if (i >= shots.size()) { timer->stop(); QApplication::quit(); return; }
        shots[i].prepare();
        QApplication::processEvents();
        window.grab().save(QDir(dir).filePath(QString::fromLatin1(shots[i].name) + QStringLiteral(".png")));
        ++i;
    });
    timer->start(150);
}

} // namespace qaflow::devsnapshot
