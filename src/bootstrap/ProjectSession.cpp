#include "ProjectSession.h"
#include "infrastructure/persistence/JsonTestCaseRepository.h"
#include "infrastructure/persistence/JsonRunHistoryRepository.h"
#include "infrastructure/persistence/JsonRunSessionRepository.h"
#include "infrastructure/persistence/JsonBugRepository.h"
#include "infrastructure/persistence/QSettingsRepository.h"
#include "infrastructure/testmgmt/ZephyrClient.h"
#include "infrastructure/tracker/TrackerRouter.h"

namespace qaflow {
ProjectSession::ProjectSession(ProjectStore& projects, const QString& id, std::shared_ptr<ISecretStore> secrets) {
    const QString dir = projects.dataDir(id);
    auto caseRepo = std::make_shared<JsonTestCaseRepository>(dir);
    settings = std::make_unique<SettingsStore>(std::make_shared<QSettingsRepository>(id), secrets);
    cases = std::make_unique<TestCaseStore>(caseRepo);
    cases->setSuiteCatalog(&projects);
    history = std::make_unique<RunHistoryStore>(std::make_shared<JsonRunHistoryRepository>(dir), *cases);
    run = std::make_unique<RunController>(*cases, *history, std::make_shared<JsonRunSessionRepository>(dir));
    plan = std::make_unique<PlanStore>(caseRepo, *cases, *history);
    bugLedger = std::make_unique<BugStore>(std::make_shared<JsonBugRepository>(dir));
    bugs = std::make_unique<BugReportService>(std::make_shared<TrackerRouter>(), *cases, *run, *settings, *bugLedger);
    publish = std::make_unique<TestPublishService>(std::make_shared<ZephyrClient>(), *cases, *history, *settings);
    capture = std::make_shared<ScreenCaptureService>();
    recorder = std::make_shared<GifRecorder>();
    evidence = std::make_unique<EvidenceService>(capture, *cases, *run, *settings);
    evidence->setRecorder(recorder);
    evidence->setProjectId(id);
    transfer = std::make_unique<CaseTransferService>(*cases);
    QObject::connect(settings.get(), &SettingsStore::saved, &projects, [&projects, source = settings.get()]() {
        emit projects.settingsChanged(source);
    });
    QObject::connect(&projects, &ProjectStore::settingsChanged, settings.get(), [store = settings.get()](QObject* source) {
        if (source != store) store->load();
    });
    settings->load();
    cases->load();
    bugLedger->load();
    plan->load();
    history->load();
    run->load();
    history->adoptLooseEvidence(run->isRunning() ? run->state().caseId : QString());
    ctx.projects = &projects; ctx.projectId = id; ctx.dataDir = dir;
    ctx.cases = cases.get(); ctx.settings = settings.get(); ctx.history = history.get();
    ctx.run = run.get(); ctx.plan = plan.get(); ctx.bugLedger = bugLedger.get();
    ctx.bugs = bugs.get(); ctx.publish = publish.get(); ctx.evidence = evidence.get(); ctx.transfer = transfer.get();
    ctx.captureBackend = capture->backendName();
}
bool ProjectSession::save() {
    // No cambiar de proyecto si queda alguna escritura pendiente que no pudo guardarse.
    const bool casesSaved = cases->save();
    const bool plansSaved = plan->save();
    const bool historySaved = history->save();
    const bool bugsSaved = bugLedger->save();
    const bool sessionSaved = run->persistSessionNow();
    return casesSaved && plansSaved && historySaved && bugsSaved && sessionSaved;
}
} // namespace qaflow
