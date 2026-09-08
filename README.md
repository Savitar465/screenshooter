# QAflow

Aplicación de escritorio para equipos de QA: casos de prueba, plan de regresión, ejecución
manual paso a paso con capturas de pantalla y reporte de bugs a Jira, GitHub, GitLab o Azure DevOps. Qt 6 Widgets · C++20.

![Ejecución](docs/screenshots/03-ejecucion.png)

Interfaz en español o inglés, tema oscuro o claro, menú con atajos estándar e icono en la bandeja del sistema.

## Requisitos

* CMake ≥ 3.16
* Qt 6 (Core, Gui, Widgets, Network, Test) — probado con Qt 6.4 y 6.7
* Opcional: Qt LinguistTools (`lrelease`) para incrustar la traducción al inglés; sin él la app se compila sólo en español
* Compilador C++20 (GCC 12+, Clang 15+, MSVC 2022)
* Opcional (Linux/X11): `xdotool` para el modo de captura «Ventana activa»
* Opcional (Linux): `secret-tool` (paquete libsecret-tools) para guardar el token del gestor en el llavero; sin él queda en el fichero de ajustes y la app lo avisa

## Compilar y ejecutar

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/qaflow
```

Tests (un ejecutable por clase, etiquetados por capa; los de UI usan la plataforma `offscreen`):

```bash
ctest --test-dir build --output-on-failure
ctest --test-dir build -L core             # modelos y funciones puras
ctest --test-dir build -L application      # stores y servicios sobre repositorios en memoria
ctest --test-dir build -L infrastructure   # JSON en disco, QSettings, clientes REST contra un servidor HTTP falso
ctest --test-dir build -L presentation     # ventana principal completa (menú, atajos, toasts)
```

## Paquetes y CI

```bash
cpack --config build/CPackConfig.cmake      # .deb y .tar.gz (Linux), instalador NSIS y .zip (Windows), .dmg (macOS)
packaging/linux/build-appimage.sh build     # AppImage (descarga linuxdeploy si hace falta)
```

`.github/workflows/ci.yml` compila y pasa los tests en Linux, Windows y macOS en cada push, genera los paquetes
como artefactos y, en los tags `v*`, los adjunta a la release de GitHub.

## Atajos

| Atajo                | Acción                              |
|----------------------|-------------------------------------|
| Ctrl+N               | Nuevo caso                          |
| Ctrl+F               | Buscar caso                         |
| Ctrl+D               | Duplicar caso                       |
| Ctrl+Z               | Deshacer el último borrado          |
| F5                   | Ejecutar el caso seleccionado       |
| Ctrl+Shift+S         | Capturar pantalla (configurable)    |
| Ctrl+B               | Reportar bug                        |
| Ctrl+1 … Ctrl+6      | Cambiar de pantalla                 |
| P / F / B / S        | Veredicto del paso en ejecución     |
| Ctrl+Q               | Salir                               |

## Pantallas

| Pantalla          | Qué hace                                                                   |
|-------------------|----------------------------------------------------------------------------|
| Casos de prueba   | Lista filtrable por suite, estado, prioridad, última ejecución y texto (título, ID, etiquetas, componente, historia); editor con suites nuevas, etiquetas, componente, enlace a historia de Jira, pasos reordenables e insertables, evidencias e historial; duplicar y eliminar con confirmación y deshacer; importar y exportar en JSON, CSV y Markdown |
| Planes            | Varios planes (crear, duplicar, archivar, eliminar), casos en orden de ejecución propio, progreso del ciclo actual con enlace a su informe, estimación basada en las duraciones reales del historial (3 min/paso si no hay datos) y arranque de un ciclo nuevo |
| Ejecución         | Paso actual con Pasa / Falla / Bloqueado / Saltar (teclas P / F / B / S), paso anterior (Retroceso), corrección de veredictos desde el registro, cronómetro por paso y por caso, observaciones y capturas. Sobrevive al cierre de la aplicación |
| Historial         | Ejecuciones archivadas (pasos, resultados, notas, duración), informes de plan con exportación a Markdown y panel de métricas: tasa de éxito por suite y evolución entre ciclos |
| Reportar bug      | Formulario prellenado con el paso fallido y campos reales del gestor (tipo, prioridad, asignado, componentes, versión, etiquetas) cargados del proyecto; crea el issue en Jira, GitHub, GitLab o Azure DevOps y sube las capturas; lista de bugs reportados con su estado y cola offline con reintento |
| Ajustes           | Idioma (español / inglés / sistema), tema (oscuro / claro / sistema), cerrar a la bandeja; gestor de incidencias (Jira, GitHub, GitLab o Azure DevOps: URL, proyecto, token en el llavero del sistema) y preferencias de captura (atajo, formato, modo, carpeta) |

## Dónde se guardan los datos

* Casos, planes, historial y ejecución en curso: directorio de datos de la aplicación (`~/.local/share/QAflow/QAflow/` en Linux).
* Ajustes: `~/.config/QAflow/QAflow.conf`. El token del gestor va al llavero del sistema (secret-tool en Linux, Keychain en macOS, DPAPI en Windows).
* Bugs reportados y cola de envíos pendientes: `bugs.json` en el directorio de datos.
* Capturas: carpeta configurable, por defecto `~/QAflow/capturas`.

En el primer arranque se cargan casos de ejemplo. Si un guardado falla (disco lleno, sin permisos) la app lo avisa
con un botón «Reintentar» y mantiene los cambios en memoria hasta que se pueda escribir.

![Tema claro en inglés](docs/screenshots/11-tema-claro-en.png)

## Arquitectura

Ver [docs/architecture.md](docs/architecture.md).
