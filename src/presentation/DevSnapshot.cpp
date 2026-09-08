#include "DevSnapshot.h"

#include "application/AppContext.h"
#include "application/SettingsStore.h"
#include "core/models/IssueLink.h"
#include "presentation/theme/Theme.h"
#include "presentation/views/MainWindow.h"

#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QTimer>

#include <functional>

namespace qaflow::devsnapshot {

bool requested() { return qEnvironmentVariableIsSet("QAFLOW_SNAPSHOT_DIR"); }

QString dataDir() { return QDir(qEnvironmentVariable("QAFLOW_SNAPSHOT_DIR")).filePath(QStringLiteral("data")); }

void isolateSettings() {
    const QString dir = QDir(qEnvironmentVariable("QAFLOW_SNAPSHOT_DIR")).filePath(QStringLiteral("config"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir);
}

void applyRequestedAppSettings(SettingsStore& settings) {
    const QString theme = qEnvironmentVariable("QAFLOW_SNAPSHOT_THEME");
    const QString lang = qEnvironmentVariable("QAFLOW_SNAPSHOT_LANG");
    if (theme.isEmpty() && lang.isEmpty()) return;
    settings.updateApp([&](AppSettings& a) {
        if (!theme.isEmpty()) a.theme = appThemeFromString(theme);
        if (!lang.isEmpty()) a.language = appLanguageFromString(lang);
    });
}

void run(MainWindow& window, AppContext& ctx) {
    const QString dir = qEnvironmentVariable("QAFLOW_SNAPSHOT_DIR");
    QDir().mkpath(dir);

    // `target` (opcional) devuelve la ventana que hay que fotografiar; por defecto, la principal.
    struct Shot { const char* name; std::function<void()> prepare; std::function<QWidget*()> target = {}; };
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
        {"04-bug", [&] {
            IssueLink link;
            link.key = QStringLiteral("SHOP-143"); link.url = QStringLiteral("https://acme.atlassian.net/browse/SHOP-143");
            link.title = QStringLiteral("[Checkout] El cupón QA10 no descuenta"); link.caseId = QStringLiteral("TC-104");
            link.tracker = QStringLiteral("Jira"); link.severity = QStringLiteral("Mayor"); link.status = QStringLiteral("In Progress");
            link.createdAt = QDateTime::currentDateTime().addDays(-2);
            ctx.bugLedger->recordIssue(link);
            BugReport queued = ctx.bugs->draftFromCurrentContext();
            queued.title = QStringLiteral("[Checkout] Error 500 al pagar con Amex");
            queued.actual = QStringLiteral("Pantalla en blanco");
            ctx.bugLedger->enqueue(queued, QStringLiteral("Host not found"));
            window.navigate(Screen::Bug);
        }},
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
        {"10-metricas", [&] {
            // Segundo ciclo del mismo plan, mejor que el primero, para que haya evolución.
            ctx.run->startSequence({QStringLiteral("TC-102"), QStringLiteral("TC-103"), QStringLiteral("TC-107")}, QStringLiteral("Regresión Sprint 14"), ctx.plan->activeId());
            ctx.run->mark(StepResult::Pass); ctx.run->mark(StepResult::Pass); ctx.run->finish();
            ctx.run->mark(StepResult::Pass); ctx.run->finish();
            ctx.run->mark(StepResult::Pass);
            window.finishRun();
            window.showMetrics();
        }},
        {"09-planes", [&] { ctx.plan->createPlan(QStringLiteral("Smoke release 2.3")); ctx.plan->toggle(QStringLiteral("TC-101")); ctx.plan->setActive(QStringLiteral("PL-0001")); window.navigate(Screen::Plan); }},
        {"05-ajustes", [&] { window.openSettings(); }, [&] { return window.settingsWindow(); }},
        {"06-casos-en-ejecucion", [&] {
            if (QWidget* settings = window.settingsWindow()) settings->close();
            window.navigate(Screen::Casos);
            window.showToast(QStringLiteral("Captura guardada en ~/QAflow/capturas"), theme::Cyan);
        }},
    };

    auto* timer = new QTimer(&window);
    QObject::connect(timer, &QTimer::timeout, [&window, dir, timer, shots, i = 0]() mutable {
        if (i >= shots.size()) { timer->stop(); QApplication::quit(); return; }
        shots[i].prepare();
        QApplication::processEvents();
        QWidget* target = shots[i].target ? shots[i].target() : &window;
        if (!target) target = &window;
        target->grab().save(QDir(dir).filePath(QString::fromLatin1(shots[i].name) + QStringLiteral(".png")));
        ++i;
    });
    timer->start(150);
}

} // namespace qaflow::devsnapshot
