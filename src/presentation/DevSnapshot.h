#pragma once

#include <QString>

namespace qaflow {

class MainWindow;
class SettingsStore;
struct AppContext;

/// Herramienta de desarrollo: si la variable de entorno QAFLOW_SNAPSHOT_DIR está definida,
/// renderiza cada pantalla a PNG en ese directorio y cierra la aplicación.
/// Útil para revisar el diseño sin interacción (p. ej. con QT_QPA_PLATFORM=offscreen).
namespace devsnapshot {
bool requested();
/// Directorio de datos aislado para que las capturas no toquen los datos reales del usuario.
QString dataDir();
/// QSettings en $QAFLOW_SNAPSHOT_DIR/config para no tocar los ajustes reales.
void isolateSettings();
/// Aplica QAFLOW_SNAPSHOT_THEME (dark/light) y QAFLOW_SNAPSHOT_LANG (es/en) si están definidas.
void applyRequestedAppSettings(SettingsStore& settings);
void run(MainWindow& window, AppContext& ctx);
} // namespace devsnapshot

} // namespace qaflow
