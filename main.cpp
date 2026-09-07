// QAflow · punto de entrada y raíz de composición.
// Aquí se eligen las implementaciones concretas de infraestructura y se inyectan
// en los servicios de aplicación; las vistas reciben sólo el AppContext.

#include "application/AppContext.h"
#include "infrastructure/capture/ScreenCaptureService.h"
#include "infrastructure/jira/JiraClient.h"
#include "infrastructure/persistence/JsonRunHistoryRepository.h"
#include "infrastructure/persistence/JsonTestCaseRepository.h"
#include "infrastructure/persistence/QSettingsRepository.h"
#include "presentation/DevSnapshot.h"
#include "presentation/views/MainWindow.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QStandardPaths>
#include <QStyleFactory>

namespace {

void applyTheme(QApplication& app) {
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QFont f = app.font();
    f.setFamilies({QStringLiteral("Segoe UI"), QStringLiteral("Inter"), QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans"), QStringLiteral("sans-serif")});
    f.setPixelSize(13);
    app.setFont(f);
    QFile qss(QStringLiteral(":/styles/app.qss"));
    if (qss.open(QFile::ReadOnly)) app.setStyleSheet(QString::fromUtf8(qss.readAll()));
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("QAflow"));
    QApplication::setOrganizationName(QStringLiteral("QAflow"));
    QApplication::setApplicationVersion(QStringLiteral("0.4.0"));
    applyTheme(app);

    using namespace qaflow;

    // Infraestructura
    const QString dataDir = devsnapshot::requested() ? devsnapshot::dataDir()
                                                     : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    auto caseRepo = std::make_shared<JsonTestCaseRepository>(dataDir);
    auto historyRepo = std::make_shared<JsonRunHistoryRepository>(dataDir);
    auto settingsRepo = std::make_shared<QSettingsRepository>();
    auto jira = std::make_shared<JiraClient>();
    auto capture = std::make_shared<ScreenCaptureService>();

    // Aplicación
    TestCaseStore cases(caseRepo);
    SettingsStore settings(settingsRepo);
    RunHistoryStore history(historyRepo, cases);
    RunController run(cases, history);
    PlanStore plan(caseRepo, cases);
    BugReportService bugs(jira, cases, run, settings);
    EvidenceService evidence(capture, cases, run, settings);
    cases.load();
    settings.load();
    plan.load();
    history.load();

    AppContext ctx;
    ctx.cases = &cases;
    ctx.plan = &plan;
    ctx.run = &run;
    ctx.history = &history;
    ctx.settings = &settings;
    ctx.bugs = &bugs;
    ctx.evidence = &evidence;

    // Presentación (la captura oculta la ventana principal mientras captura)
    MainWindow window(ctx);
    capture->setAppWindow(&window);
    window.show();
    if (devsnapshot::requested()) devsnapshot::run(window, ctx);

    return app.exec();
}
