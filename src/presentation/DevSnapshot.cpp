#include "DevSnapshot.h"

#include "application/AppContext.h"
#include "presentation/views/MainWindow.h"

#include <QApplication>
#include <QDir>
#include <QTimer>

#include <functional>

namespace qaflow::devsnapshot {

bool requested() { return qEnvironmentVariableIsSet("QAFLOW_SNAPSHOT_DIR"); }

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
            window.navigate(Screen::Run);
        }},
        {"03b-ejecucion-fin", [&] {
            ctx.run->mark(StepResult::Pass);
            ctx.run->mark(StepResult::Pass);
            window.navigate(Screen::Run);
        }},
        {"04-bug", [&] { window.navigate(Screen::Bug); }},
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
