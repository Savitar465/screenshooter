#pragma once

#include <QString>

namespace qaflow {

/// Gestores de incidencias soportados. Todos comparten la misma estructura de ajustes.
enum class TrackerKind { Jira, GitHub, GitLab, AzureDevOps };

QString toString(TrackerKind k);
TrackerKind trackerKindFromString(const QString& s);

/// Conexión con el gestor de incidencias.
///  - Jira:         url de la instancia, `project` = clave (SHOP), `email` sólo en Cloud, `token` = API token o PAT.
///  - GitHub:       url = https://api.github.com (o la de GitHub Enterprise), `project` = owner/repo, `token` = PAT.
///  - GitLab:       url = https://gitlab.com (o la propia), `project` = grupo/proyecto o id numérico, `token` = PAT.
///  - Azure DevOps: url = https://dev.azure.com/organizacion, `project` = nombre del proyecto, `token` = PAT.
struct TrackerSettings {
    TrackerKind kind = TrackerKind::Jira;
    QString url = QStringLiteral("https://acme.atlassian.net");
    QString project = QStringLiteral("SHOP");
    QString email;      // sólo Jira Cloud (Basic auth email:token)
    QString token;      // nunca se persiste en claro: va al llavero del sistema
    bool connected = false;

    QString baseUrl() const;                    // url sin barra final
    QString issueUrl(const QString& key) const; // enlace al issue en el navegador
    /// Qué es `project` para este gestor ("Clave del proyecto", "owner/repo", …).
    QString projectLabel() const;
    QString projectPlaceholder() const;
    QString defaultUrl() const;
};

enum class CaptureMode { FullScreen, ActiveWindow, Region };

/// Valor canónico (se persiste en los ajustes). No traducir.
QString toString(CaptureMode m);
CaptureMode captureModeFromString(const QString& s);
/// Texto para mostrar en el idioma de la interfaz.
QString label(CaptureMode m);

/// Preferencias generales de la aplicación (idioma, tema, bandeja).
enum class AppLanguage { System, Spanish, English };
enum class AppTheme { Dark, Light, System };

QString toString(AppLanguage l);
QString toString(AppTheme t);
AppLanguage appLanguageFromString(const QString& s);
AppTheme appThemeFromString(const QString& s);

struct AppSettings {
    AppLanguage language = AppLanguage::System;
    AppTheme theme = AppTheme::Dark;
    bool closeToTray = false;   // al cerrar la ventana, seguir en la bandeja del sistema
};

struct CaptureSettings {
    QString shortcut = QStringLiteral("Ctrl+Shift+S");
    QString format = QStringLiteral("PNG");   // PNG, JPG, WebP
    CaptureMode mode = CaptureMode::ActiveWindow;
    QString folder;                            // por defecto ~/QAflow/capturas

    QString extension() const { return format.toLower() == QStringLiteral("jpg") ? QStringLiteral("jpg") : format.toLower(); }
};

} // namespace qaflow
