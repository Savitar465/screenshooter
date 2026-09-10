#pragma once

#include <QString>

namespace qaflow {

/// Gestores de incidencias soportados. Todos comparten la misma estructura de ajustes.
enum class TrackerKind { Jira, GitHub, GitLab, AzureDevOps };

QString toString(TrackerKind k);
TrackerKind trackerKindFromString(const QString& s);

/// Cómo se autentica QAflow contra Jira (los demás gestores usan siempre un token):
///  - CloudToken:  Basic con el correo de la cuenta y un API token   → Jira Cloud.
///  - ServerBasic: Basic con usuario y contraseña                    → Jira Server / Data Center; la única
///                 opción en las versiones anteriores a la 8.14 (por ejemplo 8.5.1), que no tienen PAT.
///  - ServerToken: Bearer con un token personal (PAT)                → Jira Server / Data Center 8.14+.
enum class JiraAuth { CloudToken, ServerBasic, ServerToken };

/// Valor canónico (se persiste en los ajustes). No traducir.
QString toString(JiraAuth a);
JiraAuth jiraAuthFromString(const QString& s);
/// Texto para mostrar en el idioma de la interfaz.
QString label(JiraAuth a);

/// Conexión con el gestor de incidencias.
///  - Jira:         url de la instancia (con su context path si lo tiene), `project` = clave (SHOP),
///                  `user` y `token` según `jiraAuth`.
///  - GitHub:       url = https://api.github.com (o la de GitHub Enterprise), `project` = owner/repo, `token` = PAT.
///  - GitLab:       url = https://gitlab.com (o la propia), `project` = grupo/proyecto o id numérico, `token` = PAT.
///  - Azure DevOps: url = https://dev.azure.com/organizacion, `project` = nombre del proyecto, `token` = PAT.
struct TrackerSettings {
    TrackerKind kind = TrackerKind::Jira;
    JiraAuth jiraAuth = JiraAuth::CloudToken;   // sólo Jira
    QString url = QStringLiteral("https://acme.atlassian.net");
    QString project = QStringLiteral("SHOP");
    QString user;       // correo de la cuenta (Jira Cloud) o usuario (Jira Server); vacío en los demás gestores
    QString token;      // API token, contraseña o PAT: nunca se persiste en claro, va al llavero del sistema
    bool connected = false;

    /// Publicar los ciclos de plan en Zephyr for Jira (misma instancia y credenciales). Sólo con Jira.
    bool zephyr = false;
    QString zephyrVersion;   // versión del proyecto a la que van los ciclos; vacío = sin programar
    /// Tipo de incidencia con el que se crean los Tests que faltan; vacío = "Test", el que instala
    /// Zephyr (en un Jira traducido puede llamarse de otra manera).
    QString zephyrTestType;

    QString baseUrl() const;                    // url sin barra final
    QString issueUrl(const QString& key) const; // enlace al issue en el navegador
    /// Enlace a un ciclo de Zephyr en Jira: la búsqueda de ejecuciones (ZQL) del proyecto filtrada
    /// por el nombre del ciclo, que es la pantalla estable de Zephyr Server. Vacío si el gestor no
    /// es Jira o faltan la URL o el nombre.
    QString zephyrCycleUrl(const QString& cycleName) const;
    /// Qué es `project` para este gestor ("Clave del proyecto", "owner/repo", …).
    QString projectLabel() const;
    QString projectPlaceholder() const;
    QString defaultUrl() const;

    /// El gestor pide un usuario además del secreto (Jira salvo con PAT).
    bool needsUser() const;
    /// Las personas se identifican por `accountId` (sólo Jira Cloud) y no por su nombre de usuario.
    bool usesAccountId() const;
    /// Etiquetas y ejemplos de los dos campos de credenciales, que cambian con el gestor y el modo de autenticación.
    QString userLabel() const;
    QString userPlaceholder() const;
    QString secretLabel() const;
    QString secretPlaceholder() const;
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

/// Atajos globales de la ejecución: avanzar al paso siguiente dando su veredicto y volver al
/// anterior sin traer QAflow al frente, para no interrumpir la prueba entre captura y captura.
/// Se registran en el sistema junto a los de captura (`CaptureSettings::globalShortcut`).
struct RunShortcuts {
    QString passAndNext = QStringLiteral("Ctrl+Alt+P");
    QString failAndNext = QStringLiteral("Ctrl+Alt+F");
    QString previous = QStringLiteral("Ctrl+Alt+A");
};

struct CaptureSettings {
    QString shortcut = QStringLiteral("Ctrl+Shift+S");
    QString recordShortcut = QStringLiteral("Ctrl+Shift+G");   // iniciar / detener la grabación de GIF
    QString format = QStringLiteral("PNG");   // PNG, JPG, WebP
    CaptureMode mode = CaptureMode::ActiveWindow;
    QString folder;                            // por defecto ~/QAflow/capturas
    int delaySecs = 0;                         // cuenta atrás antes de capturar (0 = inmediata)
    bool globalShortcut = true;                // registrar el atajo en el sistema (funciona sin foco)
    bool openEditor = false;                   // abrir el editor de anotaciones tras cada captura
    bool copyToClipboard = false;              // copiar la imagen al portapapeles tras capturar
    int gifFps = 10;                           // fotogramas por segundo de la grabación (5-20)
    int gifMaxSecs = 30;                       // duración máxima de una grabación (5-120)

    QString extension() const { return format.toLower() == QStringLiteral("jpg") ? QStringLiteral("jpg") : format.toLower(); }
    /// Valores fuera de rango vuelven a un valor razonable (ajustes editados a mano).
    void clamp();
};

} // namespace qaflow
