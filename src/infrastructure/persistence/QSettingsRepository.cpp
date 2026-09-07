#include "QSettingsRepository.h"

#include <QSettings>

namespace qaflow {

JiraSettings QSettingsRepository::loadJira() {
    QSettings s;
    s.beginGroup(QStringLiteral("jira"));
    JiraSettings j;
    j.url = s.value(QStringLiteral("url"), j.url).toString();
    j.project = s.value(QStringLiteral("project"), j.project).toString();
    j.email = s.value(QStringLiteral("email"), j.email).toString();
    j.token = s.value(QStringLiteral("token"), j.token).toString();
    j.connected = s.value(QStringLiteral("connected"), false).toBool();
    return j;
}

void QSettingsRepository::saveJira(const JiraSettings& j) {
    QSettings s;
    s.beginGroup(QStringLiteral("jira"));
    s.setValue(QStringLiteral("url"), j.url);
    s.setValue(QStringLiteral("project"), j.project);
    s.setValue(QStringLiteral("email"), j.email);
    s.setValue(QStringLiteral("token"), j.token);
    s.setValue(QStringLiteral("connected"), j.connected);
}

CaptureSettings QSettingsRepository::loadCapture() {
    QSettings s;
    s.beginGroup(QStringLiteral("capture"));
    CaptureSettings c;
    c.shortcut = s.value(QStringLiteral("shortcut"), c.shortcut).toString();
    c.format = s.value(QStringLiteral("format"), c.format).toString();
    c.mode = captureModeFromString(s.value(QStringLiteral("mode"), toString(c.mode)).toString());
    c.folder = s.value(QStringLiteral("folder"), c.folder).toString();
    return c;
}

void QSettingsRepository::saveCapture(const CaptureSettings& c) {
    QSettings s;
    s.beginGroup(QStringLiteral("capture"));
    s.setValue(QStringLiteral("shortcut"), c.shortcut);
    s.setValue(QStringLiteral("format"), c.format);
    s.setValue(QStringLiteral("mode"), toString(c.mode));
    s.setValue(QStringLiteral("folder"), c.folder);
}

} // namespace qaflow
