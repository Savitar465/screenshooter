#include "Settings.h"

#include <QCoreApplication>
#include <QUrl>

#include <algorithm>

namespace qaflow {

QString toString(TrackerKind k) {
    switch (k) {
        case TrackerKind::Jira: return QStringLiteral("Jira");
        case TrackerKind::GitHub: return QStringLiteral("GitHub");
        case TrackerKind::GitLab: return QStringLiteral("GitLab");
        case TrackerKind::AzureDevOps: return QStringLiteral("Azure DevOps");
    }
    return {};
}

TrackerKind trackerKindFromString(const QString& s) {
    if (s.compare(QStringLiteral("GitHub"), Qt::CaseInsensitive) == 0) return TrackerKind::GitHub;
    if (s.compare(QStringLiteral("GitLab"), Qt::CaseInsensitive) == 0) return TrackerKind::GitLab;
    if (s.compare(QStringLiteral("Azure DevOps"), Qt::CaseInsensitive) == 0 || s.compare(QStringLiteral("AzureDevOps"), Qt::CaseInsensitive) == 0) return TrackerKind::AzureDevOps;
    return TrackerKind::Jira;
}

QString toString(JiraAuth a) {
    switch (a) {
        case JiraAuth::CloudToken: return QStringLiteral("cloud-token");
        case JiraAuth::ServerBasic: return QStringLiteral("server-basic");
        case JiraAuth::ServerToken: return QStringLiteral("server-token");
    }
    return {};
}

JiraAuth jiraAuthFromString(const QString& s) {
    if (s.compare(QStringLiteral("server-basic"), Qt::CaseInsensitive) == 0) return JiraAuth::ServerBasic;
    if (s.compare(QStringLiteral("server-token"), Qt::CaseInsensitive) == 0) return JiraAuth::ServerToken;
    return JiraAuth::CloudToken;
}

QString label(JiraAuth a) {
    switch (a) {
        case JiraAuth::CloudToken: return QCoreApplication::translate("core", "Jira Cloud · correo y API token");
        case JiraAuth::ServerBasic: return QCoreApplication::translate("core", "Jira Server · usuario y contraseña");
        case JiraAuth::ServerToken: return QCoreApplication::translate("core", "Jira Server · token personal (PAT)");
    }
    return {};
}

QString TrackerSettings::baseUrl() const {
    QString base = url.trimmed();
    while (base.endsWith(QLatin1Char('/'))) base.chop(1);
    return base;
}

QString TrackerSettings::issueUrl(const QString& key) const {
    const QString base = baseUrl();
    const QString proj = project.trimmed();
    switch (kind) {
        case TrackerKind::Jira: return base + QStringLiteral("/browse/") + key;
        case TrackerKind::GitHub: {
            // La API vive en api.github.com; la web en github.com. En Enterprise, la API cuelga de /api/v3.
            QString web = base;
            if (web == QStringLiteral("https://api.github.com")) web = QStringLiteral("https://github.com");
            else if (web.endsWith(QStringLiteral("/api/v3"))) web.chop(7);
            return web + QLatin1Char('/') + proj + QStringLiteral("/issues/") + QString(key).remove(QLatin1Char('#'));
        }
        case TrackerKind::GitLab: return base + QLatin1Char('/') + proj + QStringLiteral("/-/issues/") + QString(key).remove(QLatin1Char('#'));
        case TrackerKind::AzureDevOps: return base + QLatin1Char('/') + proj + QStringLiteral("/_workitems/edit/") + key;
    }
    return {};
}

QString TrackerSettings::zephyrCycleUrl(const QString& cycleName) const {
    const QString base = baseUrl();
    const QString name = cycleName.trimmed();
    if (kind != TrackerKind::Jira || base.isEmpty() || name.isEmpty()) return {};
    auto quoted = [](QString s) { return QLatin1Char('"') + s.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('"'), QStringLiteral("\\\"")) + QLatin1Char('"'); };
    QString zql = QStringLiteral("cycleName = ") + quoted(name);
    if (!project.trimmed().isEmpty()) zql = QStringLiteral("project = ") + quoted(project.trimmed()) + QStringLiteral(" AND ") + zql;
    // La consulta viaja en el fragmento, como la escribe el propio Zephyr: `#?query=project%20%3D%20…`.
    return base + QStringLiteral("/secure/enav/#?query=") + QString::fromLatin1(QUrl::toPercentEncoding(zql));
}

QString TrackerSettings::projectLabel() const {
    switch (kind) {
        case TrackerKind::Jira: return QCoreApplication::translate("core", "Clave del proyecto");
        case TrackerKind::GitHub: return QCoreApplication::translate("core", "Repositorio (owner/repo)");
        case TrackerKind::GitLab: return QCoreApplication::translate("core", "Proyecto (grupo/proyecto o id)");
        case TrackerKind::AzureDevOps: return QCoreApplication::translate("core", "Proyecto");
    }
    return {};
}

QString TrackerSettings::projectPlaceholder() const {
    switch (kind) {
        case TrackerKind::Jira: return QStringLiteral("SHOP");
        case TrackerKind::GitHub: return QStringLiteral("acme/tienda");
        case TrackerKind::GitLab: return QStringLiteral("acme/tienda");
        case TrackerKind::AzureDevOps: return QStringLiteral("Tienda");
    }
    return {};
}

QString TrackerSettings::defaultUrl() const {
    switch (kind) {
        // Jira Server vive en el dominio de la empresa, no en atlassian.net.
        case TrackerKind::Jira: return jiraAuth == JiraAuth::CloudToken ? QStringLiteral("https://acme.atlassian.net")
                                                                       : QStringLiteral("https://jira.acme.com");
        case TrackerKind::GitHub: return QStringLiteral("https://api.github.com");
        case TrackerKind::GitLab: return QStringLiteral("https://gitlab.com");
        case TrackerKind::AzureDevOps: return QStringLiteral("https://dev.azure.com/acme");
    }
    return {};
}

bool TrackerSettings::needsUser() const {
    return kind == TrackerKind::Jira && jiraAuth != JiraAuth::ServerToken;
}

bool TrackerSettings::usesAccountId() const {
    return kind == TrackerKind::Jira && jiraAuth == JiraAuth::CloudToken;
}

QString TrackerSettings::userLabel() const {
    if (kind != TrackerKind::Jira) return {};
    return jiraAuth == JiraAuth::CloudToken ? QCoreApplication::translate("core", "Correo de la cuenta")
                                            : QCoreApplication::translate("core", "Usuario");
}

QString TrackerSettings::userPlaceholder() const {
    if (kind != TrackerKind::Jira) return {};
    return jiraAuth == JiraAuth::CloudToken ? QStringLiteral("qa@acme.com") : QStringLiteral("aperez");
}

QString TrackerSettings::secretLabel() const {
    if (kind != TrackerKind::Jira) return QCoreApplication::translate("core", "Token de acceso");
    switch (jiraAuth) {
        case JiraAuth::CloudToken: return QCoreApplication::translate("core", "Token de API");
        case JiraAuth::ServerBasic: return QCoreApplication::translate("core", "Contraseña");
        case JiraAuth::ServerToken: return QCoreApplication::translate("core", "Token personal (PAT)");
    }
    return {};
}

QString TrackerSettings::secretPlaceholder() const {
    if (kind != TrackerKind::Jira) return QCoreApplication::translate("core", "Personal access token");
    switch (jiraAuth) {
        case JiraAuth::CloudToken: return QCoreApplication::translate("core", "API token de id.atlassian.com");
        case JiraAuth::ServerBasic: return QCoreApplication::translate("core", "Contraseña de Jira");
        case JiraAuth::ServerToken: return QCoreApplication::translate("core", "Token personal (Jira 8.14 o superior)");
    }
    return {};
}

QString toString(AppLanguage l) {
    switch (l) {
        case AppLanguage::System: return QStringLiteral("system");
        case AppLanguage::Spanish: return QStringLiteral("es");
        case AppLanguage::English: return QStringLiteral("en");
    }
    return {};
}

QString toString(AppTheme t) {
    switch (t) {
        case AppTheme::Dark: return QStringLiteral("dark");
        case AppTheme::Light: return QStringLiteral("light");
        case AppTheme::System: return QStringLiteral("system");
    }
    return {};
}

AppLanguage appLanguageFromString(const QString& s) {
    if (s.startsWith(QStringLiteral("es"), Qt::CaseInsensitive)) return AppLanguage::Spanish;
    if (s.startsWith(QStringLiteral("en"), Qt::CaseInsensitive)) return AppLanguage::English;
    return AppLanguage::System;
}

AppTheme appThemeFromString(const QString& s) {
    if (s.compare(QStringLiteral("light"), Qt::CaseInsensitive) == 0) return AppTheme::Light;
    if (s.compare(QStringLiteral("system"), Qt::CaseInsensitive) == 0) return AppTheme::System;
    return AppTheme::Dark;
}

QString toString(CaptureMode m) {
    switch (m) {
        case CaptureMode::FullScreen: return QStringLiteral("Pantalla completa");
        case CaptureMode::ActiveWindow: return QStringLiteral("Ventana activa");
        case CaptureMode::Region: return QStringLiteral("Región");
    }
    return {};
}

QString label(CaptureMode m) {
    switch (m) {
        case CaptureMode::FullScreen: return QCoreApplication::translate("core", "Pantalla completa");
        case CaptureMode::ActiveWindow: return QCoreApplication::translate("core", "Ventana activa");
        case CaptureMode::Region: return QCoreApplication::translate("core", "Región");
    }
    return {};
}

QString toString(CaptureScreen s) {
    switch (s) {
        case CaptureScreen::UnderCursor: return QStringLiteral("cursor");
        case CaptureScreen::AwayFromApp: return QStringLiteral("away");
        case CaptureScreen::Fixed: return QStringLiteral("fixed");
    }
    return {};
}

CaptureScreen captureScreenFromString(const QString& s) {
    if (s == QStringLiteral("away")) return CaptureScreen::AwayFromApp;
    if (s == QStringLiteral("fixed")) return CaptureScreen::Fixed;
    return CaptureScreen::UnderCursor;
}

QString label(CaptureScreen s) {
    switch (s) {
        case CaptureScreen::UnderCursor: return QCoreApplication::translate("core", "Bajo el cursor");
        case CaptureScreen::AwayFromApp: return QCoreApplication::translate("core", "Donde no está QAflow");
        case CaptureScreen::Fixed: return QCoreApplication::translate("core", "Pantalla fija");
    }
    return {};
}

void CaptureSettings::clamp() {
    delaySecs = std::clamp(delaySecs, 0, 60);
    gifFps = std::clamp(gifFps, 5, 20);
    gifMaxSecs = std::clamp(gifMaxSecs, 5, 120);
    if (shortcut.trimmed().isEmpty()) shortcut = QStringLiteral("Ctrl+Shift+S");
    if (recordShortcut.trimmed().isEmpty()) recordShortcut = QStringLiteral("Ctrl+Shift+G");
}

CaptureMode captureModeFromString(const QString& s) {
    if (s == QStringLiteral("Pantalla completa")) return CaptureMode::FullScreen;
    if (s == QStringLiteral("Región")) return CaptureMode::Region;
    return CaptureMode::ActiveWindow;
}

QString toString(AiProvider p) {
    switch (p) {
        case AiProvider::Anthropic: return QStringLiteral("anthropic");
        case AiProvider::OpenAI: return QStringLiteral("openai");
        case AiProvider::Gemini: return QStringLiteral("gemini");
    }
    return {};
}

AiProvider aiProviderFromString(const QString& s) {
    if (s.compare(QStringLiteral("openai"), Qt::CaseInsensitive) == 0) return AiProvider::OpenAI;
    if (s.compare(QStringLiteral("gemini"), Qt::CaseInsensitive) == 0) return AiProvider::Gemini;
    return AiProvider::Anthropic;
}

QString label(AiProvider p) {
    switch (p) {
        case AiProvider::Anthropic: return QStringLiteral("Anthropic (Claude)");
        case AiProvider::OpenAI: return QStringLiteral("OpenAI (ChatGPT)");
        case AiProvider::Gemini: return QStringLiteral("Google (Gemini)");
    }
    return {};
}

QString AiSettings::defaultModel(AiProvider p) {
    switch (p) {
        case AiProvider::Anthropic: return QStringLiteral("claude-sonnet-5");
        case AiProvider::OpenAI: return QStringLiteral("gpt-4.1");
        case AiProvider::Gemini: return QStringLiteral("gemini-2.5-flash");
    }
    return {};
}

QString AiSettings::defaultBaseUrl(AiProvider p) {
    switch (p) {
        case AiProvider::Anthropic: return QStringLiteral("https://api.anthropic.com");
        case AiProvider::OpenAI: return QStringLiteral("https://api.openai.com/v1");
        case AiProvider::Gemini: return QStringLiteral("https://generativelanguage.googleapis.com/v1beta");
    }
    return {};
}

QString AiSettings::model() const {
    const QString m = active().model.trimmed();
    return m.isEmpty() ? defaultModel(provider) : m;
}

QString AiSettings::baseUrl() const {
    QString url = active().baseUrl.trimmed();
    if (url.isEmpty()) url = defaultBaseUrl(provider);
    while (url.endsWith(QLatin1Char('/'))) url.chop(1);
    return url;
}

void AiSettings::clamp() {
    if (maxTokens < 1024 || maxTokens > 64000) maxTokens = 8192;
}

} // namespace qaflow
