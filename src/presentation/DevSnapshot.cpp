#include "DevSnapshot.h"

#include "application/AppContext.h"
#include "application/EvidenceService.h"
#include "application/SettingsStore.h"
#include "core/models/IssueLink.h"
#include "presentation/theme/Theme.h"
#include "presentation/views/MainWindow.h"

#include <QApplication>
#include <QDir>
#include <QPainter>
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
    // Las capturas de ejemplo van al directorio de la sesión, no a la carpeta del usuario.
    settings.updateCapture([](CaptureSettings& c) {
        c.folder = QDir(qEnvironmentVariable("QAFLOW_SNAPSHOT_DIR")).filePath(QStringLiteral("capturas"));
    });
    const QString theme = qEnvironmentVariable("QAFLOW_SNAPSHOT_THEME");
    const QString lang = qEnvironmentVariable("QAFLOW_SNAPSHOT_LANG");
    if (theme.isEmpty() && lang.isEmpty()) return;
    settings.updateApp([&](AppSettings& a) {
        if (!theme.isEmpty()) a.theme = appThemeFromString(theme);
        if (!lang.isEmpty()) a.language = appLanguageFromString(lang);
    });
}

namespace {

/// Dibuja una "captura" de ejemplo (un carrito de la compra) para que la pantalla de ejecución
/// tenga evidencias que enseñar. `highlight` marca en rojo la línea del total.
QString sampleShot(const QString& dir, int n, bool highlight) {
    QImage img(1120, 700, QImage::Format_RGB32);
    img.fill(QColor(0xf6, 0xf7, 0xfb));
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(QRect(0, 0, img.width(), 64), QColor(0xff, 0xff, 0xff));
    p.setPen(QColor(0x1f, 0x29, 0x37));
    QFont f = p.font();
    f.setPixelSize(22);
    f.setBold(true);
    p.setFont(f);
    p.drawText(QRect(40, 0, 400, 64), Qt::AlignVCenter, QStringLiteral("acme shop · carrito"));
    p.setBrush(QColor(0xff, 0xff, 0xff));
    p.setPen(QColor(0xe5, 0xe7, 0xeb));
    p.drawRoundedRect(QRect(40, 104, 660, 460), 12, 12);
    p.drawRoundedRect(QRect(730, 104, 350, 260), 12, 12);
    f.setPixelSize(15);
    f.setBold(false);
    p.setFont(f);
    for (int i = 0; i < 4; ++i) {
        p.setPen(QColor(0xe5, 0xe7, 0xeb));
        p.drawLine(64, 180 + i * 88, 676, 180 + i * 88);
        p.setBrush(QColor(0xe8, 0xed, 0xf5));
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(QRect(64, 196 + i * 88, 64, 56), 8, 8);
        p.setPen(QColor(0x37, 0x41, 0x51));
        p.drawText(QRect(148, 196 + i * 88, 380, 56), Qt::AlignVCenter, QStringLiteral("Artículo %1 · talla M").arg(i + 1));
    }
    const QRect total(730 + 24, 104 + 180, 302, 52);
    p.setPen(Qt::NoPen);
    p.setBrush(highlight ? QColor(0xfe, 0xe2, 0xe2) : QColor(0xec, 0xfd, 0xf5));
    p.drawRoundedRect(total, 8, 8);
    f.setBold(true);
    p.setFont(f);
    p.setPen(highlight ? QColor(0xb9, 0x1c, 0x1c) : QColor(0x06, 0x5f, 0x46));
    p.drawText(total.adjusted(14, 0, -14, 0), Qt::AlignVCenter, QStringLiteral("Total"));
    p.drawText(total.adjusted(14, 0, -14, 0), Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("119,90 €"));
    p.end();
    const QString path = QDir(dir).filePath(QStringLiteral("muestra_%1.png").arg(n));
    img.save(path);
    return path;
}

/// Adjunta las capturas de ejemplo al caso en ejecución y las reparte entre sus dos primeros pasos.
void seedEvidence(AppContext& ctx) {
    const QString dir = QDir(qEnvironmentVariable("QAFLOW_SNAPSHOT_DIR")).filePath(QStringLiteral("muestras"));
    QDir().mkpath(dir);
    ctx.evidence->attachFiles({sampleShot(dir, 1, false), sampleShot(dir, 2, true)});
    const TestCase* c = ctx.cases->find(ctx.run->state().caseId);
    if (!c) return;
    for (int i = 0; i < c->shots.size(); ++i) ctx.cases->assignShotStep(c->id, c->shots[i].id, i + 1);
}

} // namespace

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
            seedEvidence(ctx);
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
