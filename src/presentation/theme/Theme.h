#pragma once

#include <QColor>
#include <QString>

/// Paleta del diseño de referencia (tema oscuro). Los mismos valores viven en resources/styles/app.qss.
namespace qaflow::theme {

inline const char* Bg        = "#0e1116";
inline const char* Panel     = "#161b22";
inline const char* Elevated  = "#1c232d";
inline const char* Border    = "#2a3441";
inline const char* Text      = "#e6edf3";
inline const char* Muted     = "#9aa7b4";
inline const char* Blue      = "#6ea8fe";
inline const char* Green     = "#10b981";
inline const char* Red       = "#ef4444";
inline const char* RedSoft   = "#ff8f8f";
inline const char* Amber     = "#f59e0b";
inline const char* AmberSoft = "#fbbf24";
inline const char* Cyan      = "#06b6d4";
inline const char* Violet    = "#8b5cf6";

struct Pill { QString bg; QString fg; };

inline Pill priorityPill(const QString& priority) {
    if (priority == QStringLiteral("Alta")) return {QStringLiteral("rgba(239,68,68,38)"), RedSoft};
    if (priority == QStringLiteral("Media")) return {QStringLiteral("rgba(245,158,11,38)"), AmberSoft};
    return {QStringLiteral("rgba(154,167,180,38)"), Muted};
}

} // namespace qaflow::theme
