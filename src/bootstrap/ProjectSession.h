#pragma once

#include "application/AppContext.h"
#include "infrastructure/capture/GifRecorder.h"
#include "infrastructure/capture/ScreenCaptureService.h"
#include "presentation/views/MainWindow.h"

namespace qaflow {
/// Una sesión completa por proyecto: ni los callbacks ni las escrituras diferidas cambian de dueño.
class ProjectSession {
public:
    /// `requirementSource` es la conexión con GESREQ, que main.cpp comparte entre proyectos (una sola
    /// sesión del usuario); sin ella, la sesión crea la suya.
    ProjectSession(ProjectStore& projects, const QString& id, std::shared_ptr<ISecretStore> secrets,
                   std::shared_ptr<IRequirementSource> requirementSource = nullptr);
    bool save();
    /// Si se puede dejar ahora este proyecto. No se puede con una ejecución o una captura en curso:
    /// `reason` dice qué hay que terminar antes.
    bool canLeave(QString* reason = nullptr) const;
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
    std::unique_ptr<IssueStore> issues;
    std::unique_ptr<IssuePublishService> issuePublish;
    std::unique_ptr<QualityRecordService> records;
    std::unique_ptr<RequirementSourceService> requirements;
    std::unique_ptr<AiService> ai;
    std::unique_ptr<AttachmentTextService> attachmentText;   // tras `requirements`: se destruye antes que el servicio que usa
    std::unique_ptr<RevisionPublishService> revisionPublish;
    std::unique_ptr<MainWindow> window;
};
} // namespace qaflow
