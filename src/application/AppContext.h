#pragma once

#include "application/AiService.h"
#include "application/AttachmentTextService.h"
#include "application/BugReportService.h"
#include "application/BugStore.h"
#include "application/CaseTransferService.h"
#include "application/EvidenceService.h"
#include "application/IssuePublishService.h"
#include "application/IssueStore.h"
#include "application/PlanStore.h"
#include "application/ProjectStore.h"
#include "application/QualityRecordService.h"
#include "application/RequirementSourceService.h"
#include "application/RevisionPublishService.h"
#include "application/RunController.h"
#include "application/RunHistoryStore.h"
#include "application/SettingsStore.h"
#include "application/TestCaseStore.h"
#include "application/TestPublishService.h"
#include "core/services/IGlobalHotkey.h"

#include <QString>

namespace qaflow {

/// Servicios de la capa de aplicación ya construidos (raíz de composición en main.cpp).
/// La presentación sólo depende de estos tipos; nunca de la infraestructura.
struct AppContext {
    ProjectStore* projects = nullptr;
    QString projectId;
    TestCaseStore* cases = nullptr;
    PlanStore* plan = nullptr;
    RunController* run = nullptr;
    RunHistoryStore* history = nullptr;
    SettingsStore* settings = nullptr;
    EvidenceService* evidence = nullptr;
    BugReportService* bugs = nullptr;
    BugStore* bugLedger = nullptr;
    CaseTransferService* transfer = nullptr;
    /// Issues del proyecto: el trabajo de QA de cada requerimiento, con los planes que lo prueban.
    IssueStore* issues = nullptr;
    /// Publicación del issue en el gestor (puede ser nullptr en tests).
    IssuePublishService* issuePublish = nullptr;
    /// Acta de control de calidad del issue (puede ser nullptr en tests).
    QualityRecordService* records = nullptr;
    /// Publicación del resultado de una revisión (Zephyr + gestor + GESREQ; puede ser nullptr en tests).
    RevisionPublishService* revisionPublish = nullptr;
    /// Publicación de ciclos en Zephyr (puede ser nullptr en tests).
    TestPublishService* publish = nullptr;
    /// Conexión con GESREQ, del que se importan los requerimientos (puede ser nullptr en tests).
    RequirementSourceService* requirements = nullptr;
    /// Texto de los adjuntos de GESREQ para generar casos con IA (puede ser nullptr en tests).
    AttachmentTextService* attachmentText = nullptr;
    /// IA con la clave del usuario, para generar casos sin salir de QAflow (puede ser nullptr en tests).
    AiService* ai = nullptr;
    /// Atajo global del sistema (puede ser nullptr en tests). Ajustes muestra su `status()`.
    IGlobalHotkey* hotkey = nullptr;
    /// Directorio de datos (cases.json, history.json…), para mostrarlo o abrirlo desde la UI.
    QString dataDir;
    /// Cómo se capturan las pantallas ("QScreen::grabWindow", "xdg-desktop-portal (Wayland)"), para Ajustes.
    QString captureBackend;
};

} // namespace qaflow
