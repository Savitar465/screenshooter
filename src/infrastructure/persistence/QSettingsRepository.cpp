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
    t.zephyr = s.value(QStringLiteral("zephyr"), false).toBool();
    t.zephyrVersion = s.value(QStringLiteral("zephyrVersion")).toString();
    t.zephyrTestType = s.value(QStringLiteral("zephyrTestType")).toString();
    s.endGroup();
    if (!m_projectId.isEmpty() && t.kind == TrackerKind::Jira) {
        // Conservar el código histórico del proyecto principal antes de usar otros gestores.
        const QString legacyKey = QStringLiteral("projects/default/tracker/project");
        if (!s.contains(legacyKey)) s.setValue(legacyKey, t.project);
        t.project = s.value(QStringLiteral("projects/%1/tracker/project").arg(m_projectId)).toString();
    }
    return t;
}

void QSettingsRepository::saveTracker(const TrackerSettings& t) {
    QSettings s;
    const TrackerKind previousKind = trackerKindFromString(s.value(QStringLiteral("tracker/kind"), toString(TrackerKind::Jira)).toString());
    s.beginGroup(QStringLiteral("tracker"));
    s.setValue(QStringLiteral("kind"), toString(t.kind));
    s.setValue(QStringLiteral("url"), t.url);
    if (m_projectId.isEmpty() || t.kind != TrackerKind::Jira)
        s.setValue(QStringLiteral("project"), t.project);
    s.setValue(QStringLiteral("user"), t.user);
    s.setValue(QStringLiteral("jiraAuth"), toString(t.jiraAuth));
    s.setValue(QStringLiteral("connected"), t.connected);
    s.setValue(QStringLiteral("zephyr"), t.zephyr);
    s.setValue(QStringLiteral("zephyrVersion"), t.zephyrVersion);
    s.setValue(QStringLiteral("zephyrTestType"), t.zephyrTestType);
    s.remove(QStringLiteral("email"));   // clave de versiones anteriores, ya migrada a "user"
    if (t.token.isEmpty()) s.remove(QStringLiteral("token"));
    else s.setValue(QStringLiteral("token"), t.token);   // sólo llega aquí sin llavero disponible
    s.endGroup();
    // El token en claro de versiones anteriores desaparece del fichero.
    s.remove(QStringLiteral("jira/token"));
    if (!m_projectId.isEmpty() && t.kind == TrackerKind::Jira && previousKind == TrackerKind::Jira)
        s.setValue(QStringLiteral("projects/%1/tracker/project").arg(m_projectId), t.project);
}

RequirementSourceSettings QSettingsRepository::loadRequirementSource() {
    QSettings s;
    s.beginGroup(QStringLiteral("gesreq"));
    RequirementSourceSettings r;
    r.url = s.value(QStringLiteral("url")).toString();
    r.user = s.value(QStringLiteral("user")).toString();
    r.connected = s.value(QStringLiteral("connected"), false).toBool();
    r.password = s.value(QStringLiteral("password")).toString();   // sólo sin llavero: SettingsStore la migra en cuanto lo hay
    return r;
}

void QSettingsRepository::saveRequirementSource(const RequirementSourceSettings& r) {
    QSettings s;
    s.beginGroup(QStringLiteral("gesreq"));
    s.setValue(QStringLiteral("url"), r.url);
    s.setValue(QStringLiteral("user"), r.user);
    s.setValue(QStringLiteral("connected"), r.connected);
    if (r.password.isEmpty()) s.remove(QStringLiteral("password"));
    else s.setValue(QStringLiteral("password"), r.password);   // sólo llega aquí sin llavero disponible
}

AiSettings QSettingsRepository::loadAi() {
    QSettings s;
    s.beginGroup(QStringLiteral("ai"));
    AiSettings a;
    a.provider = aiProviderFromString(s.value(QStringLiteral("provider"), toString(a.provider)).toString());
    a.maxTokens = s.value(QStringLiteral("maxTokens"), a.maxTokens).toInt();
    for (int i = 0; i < kAiProviders; ++i) {
        const auto provider = static_cast<AiProvider>(i);
        AiProviderSettings& p = a.of(provider);
        s.beginGroup(toString(provider));
        p.model = s.value(QStringLiteral("model")).toString();
        p.baseUrl = s.value(QStringLiteral("baseUrl")).toString();
        p.connected = s.value(QStringLiteral("connected"), false).toBool();
        p.apiKey = s.value(QStringLiteral("apiKey")).toString();   // sólo sin llavero: SettingsStore la migra en cuanto lo hay
        s.endGroup();
    }
    a.clamp();
    return a;
}

void QSettingsRepository::saveAi(const AiSettings& a) {
    QSettings s;
    s.beginGroup(QStringLiteral("ai"));
    s.setValue(QStringLiteral("provider"), toString(a.provider));
    s.setValue(QStringLiteral("maxTokens"), a.maxTokens);
    for (int i = 0; i < kAiProviders; ++i) {
        const auto provider = static_cast<AiProvider>(i);
        const AiProviderSettings& p = a.of(provider);
        s.beginGroup(toString(provider));
        s.setValue(QStringLiteral("model"), p.model);
        s.setValue(QStringLiteral("baseUrl"), p.baseUrl);
        s.setValue(QStringLiteral("connected"), p.connected);
        if (p.apiKey.isEmpty()) s.remove(QStringLiteral("apiKey"));
        else s.setValue(QStringLiteral("apiKey"), p.apiKey);   // sólo llega aquí sin llavero disponible
        s.endGroup();
    }
}

CaptureSettings QSettingsRepository::loadCapture() {
    QSettings s;
    s.beginGroup(QStringLiteral("capture"));
    CaptureSettings c;
    c.shortcut = s.value(QStringLiteral("shortcut"), c.shortcut).toString();
    c.recordShortcut = s.value(QStringLiteral("recordShortcut"), c.recordShortcut).toString();
    c.format = s.value(QStringLiteral("format"), c.format).toString();
    c.mode = captureModeFromString(s.value(QStringLiteral("mode"), toString(c.mode)).toString());
    c.screen = captureScreenFromString(s.value(QStringLiteral("screen"), toString(c.screen)).toString());
    c.screenName = s.value(QStringLiteral("screenName"), c.screenName).toString();
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
    s.setValue(QStringLiteral("screen"), toString(c.screen));
    s.setValue(QStringLiteral("screenName"), c.screenName);
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
    r.next = s.value(QStringLiteral("next"), r.next).toString();
    return r;
}

void QSettingsRepository::saveRunShortcuts(const RunShortcuts& s2) {
    QSettings s;
    s.beginGroup(QStringLiteral("run"));
    s.setValue(QStringLiteral("passAndNext"), s2.passAndNext);
    s.setValue(QStringLiteral("failAndNext"), s2.failAndNext);
    s.setValue(QStringLiteral("previous"), s2.previous);
    s.setValue(QStringLiteral("next"), s2.next);
}

} // namespace qaflow
