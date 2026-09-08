#include "Settings.h"

#include <QCoreApplication>

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
        case TrackerKind::Jira: return QStringLiteral("https://acme.atlassian.net");
        case TrackerKind::GitHub: return QStringLiteral("https://api.github.com");
        case TrackerKind::GitLab: return QStringLiteral("https://gitlab.com");
        case TrackerKind::AzureDevOps: return QStringLiteral("https://dev.azure.com/acme");
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

CaptureMode captureModeFromString(const QString& s) {
    if (s == QStringLiteral("Pantalla completa")) return CaptureMode::FullScreen;
    if (s == QStringLiteral("Región")) return CaptureMode::Region;
    return CaptureMode::ActiveWindow;
}

} // namespace qaflow
