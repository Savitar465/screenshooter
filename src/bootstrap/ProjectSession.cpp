#include "ProjectSession.h"
#include "infrastructure/persistence/JsonTestCaseRepository.h"
#include "infrastructure/persistence/JsonRunHistoryRepository.h"
#include "infrastructure/persistence/JsonRunSessionRepository.h"
#include "infrastructure/persistence/JsonBugRepository.h"
#include "infrastructure/persistence/JsonIssueRepository.h"
#include "infrastructure/persistence/QSettingsRepository.h"
#include "infrastructure/report/QualityRecordDocx.h"
#include "infrastructure/requirements/GesreqClient.h"
#include "infrastructure/testmgmt/ZephyrClient.h"
#include "infrastructure/tracker/TrackerRouter.h"

namespace qaflow {
ProjectSession::ProjectSession(ProjectStore& projects, const QString& id, std::shared_ptr<ISecretStore> secrets,
                               std::shared_ptr<IRequirementSource> requirementSource) {
    const QString dir = projects.dataDir(id);
    auto caseRepo = std::make_shared<JsonTestCaseRepository>(dir);
    settings = std::make_unique<SettingsStore>(std::make_shared<QSettingsRepository>(id), secrets);
    cases = std::make_unique<TestCaseStore>(caseRepo);
    cases->setSuiteCatalog(&projects);
    history = std::make_unique<RunHistoryStore>(std::make_shared<JsonRunHistoryRepository>(dir), *cases);
    run = std::make_unique<RunController>(*cases, *history, std::make_shared<JsonRunSessionRepository>(dir));
    plan = std::make_unique<PlanStore>(caseRepo, *cases, *history);
    bugLedger = std::make_unique<BugStore>(std::make_shared<JsonBugRepository>(dir));
    // El informe de cada ciclo trae los bugs que se reportaron mientras corría; el libro se crea
    // después que el historial, así que se le pasa aquí.
    history->setBugs(bugLedger.get());
    bugs = std::make_unique<BugReportService>(std::make_shared<TrackerRouter>(), *cases, *run, *settings, *bugLedger);
    publish = std::make_unique<TestPublishService>(std::make_shared<ZephyrClient>(), *cases, *history, *settings, *bugLedger);
    capture = std::make_shared<ScreenCaptureService>();
    recorder = std::make_shared<GifRecorder>();
    // Capturas y grabaciones siguen la pantalla elegida en Ajustes (conectado antes de `load()`).
    QObject::connect(settings.get(), &SettingsStore::captureChanged, capture.get(),
                     [store = settings.get(), cap = capture.get(), rec = recorder.get()]() {
                         const CaptureSettings& c = store->capture();
                         cap->setScreenTarget(c.screen, c.screenName);
                         rec->setScreenTarget(c.screen, c.screenName);
                     });
    evidence = std::make_unique<EvidenceService>(capture, *cases, *run, *settings);
    evidence->setRecorder(recorder);
    evidence->setProjectId(id);
    transfer = std::make_unique<CaseTransferService>(*cases);
    issues = std::make_unique<IssueStore>(std::make_shared<JsonIssueRepository>(dir));
    issuePublish = std::make_unique<IssuePublishService>(std::make_shared<TrackerRouter>(), *issues, *settings);
    // Los ciclos se publican con el requerimiento y la revisión de su control de calidad en el nombre.
    publish->setIssues(issues.get());
    records = std::make_unique<QualityRecordService>(*issues, *cases, *plan, *history, *bugLedger, *settings,
                                                     std::make_shared<QualityRecordDocx>(), publish.get());
    requirements = std::make_unique<RequirementSourceService>(requirementSource ? requirementSource : std::make_shared<GesreqClient>(), *settings);
    revisionPublish = std::make_unique<RevisionPublishService>(*issues, *history, *records, publish.get(), issuePublish.get(),
                                                               requirements.get());
    // El issue sigue a sus pruebas: arrancar un ciclo de uno de sus planes lo pasa a «En pruebas» y
    // abre su revisión (la primera, o la siguiente si la anterior ya se cerró).
    QObject::connect(run.get(), &RunController::planStarted, issues.get(),
                     [store = issues.get(), log = history.get()](const QString& planRunId, const QString& planId) {
                         // Y el ciclo se queda con el issue y la revisión que abre: así cada ejecución
                         // dice de qué ronda del control de calidad es, hoy y al publicarla en Zephyr.
                         const IssueStore::RevisionRef started = store->notePlanStarted(planId);
                         if (!started.isEmpty()) log->noteCycleRevision(planRunId, started.issueId, started.revision);
                     });
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
    issues->load();
    history->adoptLooseEvidence(run->isRunning() ? run->state().caseId : QString());
    ctx.projects = &projects; ctx.projectId = id; ctx.dataDir = dir;
    ctx.cases = cases.get(); ctx.settings = settings.get(); ctx.history = history.get();
    ctx.run = run.get(); ctx.plan = plan.get(); ctx.bugLedger = bugLedger.get();
    ctx.bugs = bugs.get(); ctx.publish = publish.get(); ctx.evidence = evidence.get(); ctx.transfer = transfer.get();
    ctx.issues = issues.get(); ctx.issuePublish = issuePublish.get(); ctx.requirements = requirements.get();
    ctx.records = records.get();
    ctx.revisionPublish = revisionPublish.get();
    ctx.captureBackend = capture->backendName();
}
bool ProjectSession::canLeave(QString* reason) const {
    const bool executing = !run->state().caseId.isEmpty();
    const bool capturing = evidence->isRecording() || evidence->isCountingDown() || evidence->isBusy();
    if (!executing && !capturing) return true;
    if (reason) *reason = QObject::tr("Finaliza o detén la ejecución y las capturas antes de cambiar de proyecto");
    return false;
}

bool ProjectSession::save() {
    // No cambiar de proyecto si queda alguna escritura pendiente que no pudo guardarse. Unos issues que no
    // se pudieron leer no se escriben (se perderían), y eso no debe impedir cambiar de proyecto.
    const bool casesSaved = cases->save();
    const bool plansSaved = plan->save();
    const bool historySaved = history->save();
    const bool bugsSaved = bugLedger->save();
    const bool issuesSaved = issues->isReadOnly() || issues->save();
    const bool sessionSaved = run->persistSessionNow();
    return casesSaved && plansSaved && historySaved && bugsSaved && issuesSaved && sessionSaved;
}
} // namespace qaflow
