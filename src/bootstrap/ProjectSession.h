#pragma once

#include "application/AppContext.h"
#include "infrastructure/capture/GifRecorder.h"
#include "infrastructure/capture/ScreenCaptureService.h"
#include "presentation/views/MainWindow.h"

namespace qaflow {
/// Una sesión completa por proyecto: ni los callbacks ni las escrituras diferidas cambian de dueño.
class ProjectSession {
public:
    ProjectSession(ProjectStore& projects, const QString& id, std::shared_ptr<ISecretStore> secrets);
    bool save();
    AppContext ctx;
    std::shared_ptr<ScreenCaptureService> capture;
    std::shared_ptr<GifRecorder> recorder;
    std::unique_ptr<TestCaseStore> cases;
    std::unique_ptr<SettingsStore> settings;
    std::unique_ptr<RunHistoryStore> history;
    std::unique_ptr<RunController> run;
    std::unique_ptr<PlanStore> plan;
    std::unique_ptr<BugStore> bugLedger;
    std::unique_ptr<BugReportService> bugs;
    std::unique_ptr<TestPublishService> publish;
    std::unique_ptr<EvidenceService> evidence;
    std::unique_ptr<CaseTransferService> transfer;
    std::unique_ptr<MainWindow> window;
};
} // namespace qaflow
