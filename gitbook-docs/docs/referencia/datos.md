---
description: Dónde guarda QAflow los casos, planes, historial, ajustes, secretos y evidencias.
---

# Dónde se guardan los datos

## Directorio de datos

| Sistema | Ruta |
|---------|------|
| Linux | `~/.local/share/QAflow/QAflow/` |
| Windows | `%APPDATA%\QAflow\QAflow\` |
| macOS | `~/Library/Application Support/QAflow/QAflow/` |

El **Proyecto principal** usa el directorio directamente; los proyectos nuevos, `projects/<id>/` dentro de él.

| Fichero | Contenido |
|---------|-----------|
| `projects.json` | Catálogo de proyectos, su sistema de GESREQ y sus fases |
| `cases.json` | Casos de prueba |
| `plans.json` | Planes y plan activo |
| `history.json` | Ejecuciones archivadas y ciclos |
| `session.json` | Ejecución en curso (se borra al terminar) |
| `sessions/<PR-…>.json` | Ciclos en pausa |
| `bugs.json` | Libro de bugs y cola de envíos pendientes |
| `issues.json` | Issues, lo importado de GESREQ, sus revisiones, actas y publicación |

## Ajustes y secretos

* **Ajustes**: `~/.config/QAflow/QAflow.conf` en Linux (registro en Windows, `.plist` en macOS).
* **Tokens, contraseñas y claves de API**: llavero del sistema (`secret-tool`, Keychain o DPAPI). Sin
  llavero, en el fichero de ajustes, con aviso.

## Evidencias

Carpeta configurable, por defecto `~/QAflow/capturas`, con una subcarpeta por proyecto. Ficheros
`cap_NNN.png` (capturas), `rec_NNN.gif` (grabaciones) y `adj_NNN_nombre.ext` (adjuntos).

## Actas

Donde las guardes al generarlas. El issue recuerda la ruta de cada revisión.

## Copias de seguridad

Basta con copiar el directorio de datos y la carpeta de capturas. Si un guardado falla (disco lleno, sin
permisos), QAflow lo avisa con **Reintentar** y mantiene los cambios en memoria hasta poder escribir.
