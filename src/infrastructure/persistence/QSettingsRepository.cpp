#include "QSettingsRepository.h"

#include <QSettings>

namespace qaflow {

TrackerSettings QSettingsRepository::loadTracker() {
    QSettings s;
    TrackerSettings t;
    // Versiones anteriores guardaban un único gestor (Jira) en el grupo "jira".
    const QString group = s.contains(QStringLiteral("tracker/url")) ? QStringLiteral("tracker") : QStringLiteral("jira");
    s.beginGroup(group);
    t.kind = trackerKindFromString(s.value(QStringLiteral("kind"), toString(TrackerKind::Jira)).toString());
    t.url = s.value(QStringLiteral("url"), t.url).toString();
    t.project = s.value(QStringLiteral("project"), t.project).toString();
    // "email" es la clave de versiones anteriores, cuando el campo sólo valía para Jira Cloud.
    t.user = s.value(QStringLiteral("user"), s.value(QStringLiteral("email"), t.user).toString()).toString();
    // Sin modo guardado se deduce del ajuste antiguo: con correo era Cloud; sin él, un PAT de Server.
    t.jiraAuth = s.contains(QStringLiteral("jiraAuth"))
                     ? jiraAuthFromString(s.value(QStringLiteral("jiraAuth")).toString())
                     : (t.user.trimmed().isEmpty() ? JiraAuth::ServerToken : JiraAuth::CloudToken);
    t.token = s.value(QStringLiteral("token")).toString();   // sólo heredado: SettingsStore lo migra al llavero
    t.connected = s.value(QStringLiteral("connected"), false).toBool();
    return t;
}

void QSettingsRepository::saveTracker(const TrackerSettings& t) {
    QSettings s;
    s.beginGroup(QStringLiteral("tracker"));
    s.setValue(QStringLiteral("kind"), toString(t.kind));
    s.setValue(QStringLiteral("url"), t.url);
    s.setValue(QStringLiteral("project"), t.project);
    s.setValue(QStringLiteral("user"), t.user);
    s.setValue(QStringLiteral("jiraAuth"), toString(t.jiraAuth));
    s.setValue(QStringLiteral("connected"), t.connected);
    s.remove(QStringLiteral("email"));   // clave de versiones anteriores, ya migrada a "user"
    if (t.token.isEmpty()) s.remove(QStringLiteral("token"));
    else s.setValue(QStringLiteral("token"), t.token);   // sólo llega aquí sin llavero disponible
    s.endGroup();
    // El token en claro de versiones anteriores desaparece del fichero.
    s.remove(QStringLiteral("jira/token"));
}

CaptureSettings QSettingsRepository::loadCapture() {
    QSettings s;
    s.beginGroup(QStringLiteral("capture"));
    CaptureSettings c;
    c.shortcut = s.value(QStringLiteral("shortcut"), c.shortcut).toString();
    c.recordShortcut = s.value(QStringLiteral("recordShortcut"), c.recordShortcut).toString();
    c.format = s.value(QStringLiteral("format"), c.format).toString();
    c.mode = captureModeFromString(s.value(QStringLiteral("mode"), toString(c.mode)).toString());
    c.folder = s.value(QStringLiteral("folder"), c.folder).toString();
    c.delaySecs = s.value(QStringLiteral("delaySecs"), c.delaySecs).toInt();
    c.globalShortcut = s.value(QStringLiteral("globalShortcut"), c.globalShortcut).toBool();
    c.openEditor = s.value(QStringLiteral("openEditor"), c.openEditor).toBool();
    c.copyToClipboard = s.value(QStringLiteral("copyToClipboard"), c.copyToClipboard).toBool();
    c.gifFps = s.value(QStringLiteral("gifFps"), c.gifFps).toInt();
    c.gifMaxSecs = s.value(QStringLiteral("gifMaxSecs"), c.gifMaxSecs).toInt();
    c.clamp();
    return c;
}

void QSettingsRepository::saveCapture(const CaptureSettings& c) {
    QSettings s;
    s.beginGroup(QStringLiteral("capture"));
    s.setValue(QStringLiteral("shortcut"), c.shortcut);
    s.setValue(QStringLiteral("recordShortcut"), c.recordShortcut);
    s.setValue(QStringLiteral("format"), c.format);
    s.setValue(QStringLiteral("mode"), toString(c.mode));
    s.setValue(QStringLiteral("folder"), c.folder);
    s.setValue(QStringLiteral("delaySecs"), c.delaySecs);
    s.setValue(QStringLiteral("globalShortcut"), c.globalShortcut);
    s.setValue(QStringLiteral("openEditor"), c.openEditor);
    s.setValue(QStringLiteral("copyToClipboard"), c.copyToClipboard);
    s.setValue(QStringLiteral("gifFps"), c.gifFps);
    s.setValue(QStringLiteral("gifMaxSecs"), c.gifMaxSecs);
}

AppSettings QSettingsRepository::loadApp() {
    QSettings s;
    s.beginGroup(QStringLiteral("app"));
    AppSettings a;
    a.language = appLanguageFromString(s.value(QStringLiteral("language"), toString(a.language)).toString());
    a.theme = appThemeFromString(s.value(QStringLiteral("theme"), toString(a.theme)).toString());
    a.closeToTray = s.value(QStringLiteral("closeToTray"), a.closeToTray).toBool();
    return a;
}

void QSettingsRepository::saveApp(const AppSettings& a) {
    QSettings s;
    s.beginGroup(QStringLiteral("app"));
    s.setValue(QStringLiteral("language"), toString(a.language));
    s.setValue(QStringLiteral("theme"), toString(a.theme));
    s.setValue(QStringLiteral("closeToTray"), a.closeToTray);
}

RunShortcuts QSettingsRepository::loadRunShortcuts() {
    QSettings s;
    s.beginGroup(QStringLiteral("run"));
    RunShortcuts r;
    r.passAndNext = s.value(QStringLiteral("passAndNext"), r.passAndNext).toString();
    r.failAndNext = s.value(QStringLiteral("failAndNext"), r.failAndNext).toString();
    r.previous = s.value(QStringLiteral("previous"), r.previous).toString();
    return r;
}

void QSettingsRepository::saveRunShortcuts(const RunShortcuts& s2) {
    QSettings s;
    s.beginGroup(QStringLiteral("run"));
    s.setValue(QStringLiteral("passAndNext"), s2.passAndNext);
    s.setValue(QStringLiteral("failAndNext"), s2.failAndNext);
    s.setValue(QStringLiteral("previous"), s2.previous);
}

} // namespace qaflow
