# QAflow

Aplicación de escritorio para equipos de QA: casos de prueba, plan de regresión, ejecución
manual paso a paso con evidencias (capturas anotadas, grabaciones GIF, logs y vídeos adjuntos) y reporte de bugs a
Jira, GitHub, GitLab o Azure DevOps. Qt 6 Widgets · C++20.

![Ejecución](docs/screenshots/03-ejecucion.png)

Interfaz en español o inglés, tema oscuro o claro, menú con atajos estándar e icono en la bandeja del sistema.

## Requisitos

* CMake ≥ 3.16
* Qt 6 (Core, Gui, Widgets, Network, Test) — probado con Qt 6.4 y 6.7
* Opcional: Qt LinguistTools (`lrelease`) para incrustar la traducción al inglés; sin él la app se compila sólo en español
* Compilador C++20 (GCC 12+, Clang 15+, MSVC 2022)
* Opcional (Linux): QtDBus (viene con Qt) para capturar y registrar el atajo global en **Wayland** a través de
  `xdg-desktop-portal`; `libx11-dev` y `libxcb1-dev` para el atajo global en **X11**. Sin ellos la app compila y
  Ajustes indica que el atajo sólo funciona con la ventana en primer plano
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
| Ctrl+Shift+S         | Capturar pantalla (configurable; global, funciona sin el foco en la app; una segunda pulsación cancela la cuenta atrás) |
| Ctrl+Shift+G         | Iniciar o detener la grabación de GIF (configurable, global) |
| Ctrl+Shift+A         | Adjuntar archivos como evidencia (o arrastrarlos a la ventana) |
| Clic en una miniatura | Abrir la evidencia a tamaño completo (← → navegan, Ctrl+E anota, Ctrl+C copia) |
| Ctrl+B               | Reportar bug                        |
| Ctrl+1 … Ctrl+6      | Cambiar de pantalla                 |
| P / F / B / S        | Veredicto del paso en ejecución     |
| Ctrl+Q               | Salir                               |

## Pantallas

| Pantalla          | Qué hace                                                                   |
|-------------------|----------------------------------------------------------------------------|
| Casos de prueba   | Lista filtrable por suite, estado, prioridad, última ejecución y texto (título, ID, etiquetas, componente, historia); editor con suites nuevas, etiquetas, componente, enlace a historia de Jira, pasos reordenables e insertables, evidencias (capturar, grabar GIF, adjuntar archivos; abrir, anotar, copiar) e historial; duplicar y eliminar con confirmación y deshacer; importar y exportar en JSON, CSV y Markdown |
| Planes            | Varios planes (crear, duplicar, archivar, eliminar), casos en orden de ejecución propio, progreso del ciclo actual con enlace a su informe, estimación basada en las duraciones reales del historial (3 min/paso si no hay datos) y arranque de un ciclo nuevo |
| Ejecución         | Paso actual con Pasa / Falla / Bloqueado / Saltar (teclas P / F / B / S), paso anterior (Retroceso), corrección de veredictos desde el registro, cronómetro por paso y por caso, observaciones y capturas. Sobrevive al cierre de la aplicación |
| Historial         | Ejecuciones archivadas (pasos, resultados, notas, duración), informes de plan con exportación a Markdown y panel de métricas: tasa de éxito por suite y evolución entre ciclos |
| Reportar bug      | Formulario prellenado con el paso fallido y campos reales del gestor (tipo, prioridad, asignado, componentes, versión, etiquetas) cargados del proyecto; crea el issue en Jira, GitHub, GitLab o Azure DevOps y sube las capturas; lista de bugs reportados con su estado y cola offline con reintento |
| Ajustes           | Idioma (español / inglés / sistema), tema (oscuro / claro / sistema), cerrar a la bandeja; gestor de incidencias (Jira, GitHub, GitLab o Azure DevOps: URL, proyecto, token en el llavero del sistema) y preferencias de captura (atajos de captura y grabación, formato, modo, retardo, carpeta, atajo global, editor tras capturar, copia al portapapeles, fps y duración del GIF) |

## Evidencias

* **Captura** (`Ctrl+Shift+S`): pantalla completa, ventana activa o región, con **retardo** opcional (3, 5 o 10 s con
  cuenta atrás en pantalla) para abrir menús o tooltips. El atajo es **global**: se registra en el sistema
  (`RegisterHotKey` en Windows, `XGrabKey` en X11, portal `GlobalShortcuts` en Wayland, Carbon en macOS) y funciona
  aunque QAflow esté en la bandeja. Si el sistema lo rechaza, Ajustes lo indica y el atajo sigue funcionando con la app en primer plano.
* **Wayland**: las capturas van por `xdg-desktop-portal` (interfaz `Screenshot`), porque `QScreen::grabWindow`
  devuelve negro. «Ventana activa» abre el selector del compositor; «Región» recorta sobre la captura del portal.
* **Editor de anotaciones**: flechas, rectángulos, elipses, marcador, texto y **difuminado** (pixelado) de datos
  sensibles, con colores, grosor y deshacer. Se abre desde la miniatura (✎), el visor o, si se activa en Ajustes,
  automáticamente tras cada captura. Guardar sustituye el fichero.
* **Visor a tamaño completo**: clic en cualquier miniatura; navegación entre las evidencias del caso, zoom,
  arrastre, copiar al portapapeles, anotar y mostrar en la carpeta.
* **Adjuntar ficheros existentes** (logs, vídeos, HAR, imágenes…): botón «Adjuntar archivo», `Ctrl+Shift+A` o
  arrastrándolos a la ventana. Se copian a la carpeta de capturas, se asignan al paso en ejecución y se suben al
  gestor con el bug como cualquier captura.
* **Grabación de GIF** (`Ctrl+Shift+G`): pantalla completa o región, con control flotante (tiempo, Detener, Esc
  cancela), fps y duración máxima configurables. Codificador GIF propio (median cut + LZW), sin dependencias.
  No disponible en Wayland (el portal no ofrece captura continua sin PipeWire).
* **Copiar al portapapeles**: desde el visor, el menú contextual de la miniatura o automáticamente tras cada captura (Ajustes).

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
