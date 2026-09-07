#pragma once

#include <QString>

namespace qaflow {

class MainWindow;
struct AppContext;

/// Herramienta de desarrollo: si la variable de entorno QAFLOW_SNAPSHOT_DIR está definida,
/// renderiza cada pantalla a PNG en ese directorio y cierra la aplicación.
/// Útil para revisar el diseño sin interacción (p. ej. con QT_QPA_PLATFORM=offscreen).
namespace devsnapshot {
bool requested();
/// Directorio de datos aislado para que las capturas no toquen los datos reales del usuario.
QString dataDir();
void run(MainWindow& window, AppContext& ctx);
} // namespace devsnapshot

} // namespace qaflow
