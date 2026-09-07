#pragma once

#include <QString>

namespace qaflow {

struct JiraSettings {
    QString url = QStringLiteral("https://acme.atlassian.net");
    QString project = QStringLiteral("SHOP");
    QString email;      // opcional: para Jira Cloud (Basic auth email:token)
    QString token;
    bool connected = false;
};

enum class CaptureMode { FullScreen, ActiveWindow, Region };

QString toString(CaptureMode m);
CaptureMode captureModeFromString(const QString& s);

struct CaptureSettings {
    QString shortcut = QStringLiteral("Ctrl+Shift+S");
    QString format = QStringLiteral("PNG");   // PNG, JPG, WebP
    CaptureMode mode = CaptureMode::ActiveWindow;
    QString folder;                            // por defecto ~/QAflow/capturas

    QString extension() const { return format.toLower() == QStringLiteral("jpg") ? QStringLiteral("jpg") : format.toLower(); }
};

} // namespace qaflow
