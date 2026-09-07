# QAflow

Aplicación de escritorio para equipos de QA: casos de prueba, plan de regresión, ejecución
manual paso a paso con capturas de pantalla y reporte de bugs a Jira. Qt 6 Widgets · C++20.

![Ejecución](docs/screenshots/03-ejecucion.png)

## Requisitos

* CMake ≥ 3.16
* Qt 6 (Core, Gui, Widgets, Network, Test) — probado con Qt 6.4
* Compilador C++20 (GCC 12+, Clang 15+, MSVC 2022)
* Opcional (Linux/X11): `xdotool` para el modo de captura «Ventana activa»

## Compilar y ejecutar

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/qaflow
```

Tests unitarios (un ejecutable por clase, etiquetados por capa):

```bash
ctest --test-dir build --output-on-failure
ctest --test-dir build -L core           # sólo core/
ctest --test-dir build -L application    # sólo application/
```

## Pantallas

| Pantalla          | Qué hace                                                                   |
|-------------------|----------------------------------------------------------------------------|
| Casos de prueba   | Lista filtrable por suite, estado, prioridad, última ejecución y texto (título, ID, etiquetas, componente, historia); editor con suites nuevas, etiquetas, componente, enlace a historia de Jira, pasos reordenables e insertables, evidencias e historial; duplicar y eliminar con confirmación y deshacer; importar y exportar en JSON, CSV y Markdown |
| Planes            | Varios planes (crear, duplicar, archivar, eliminar), casos en orden de ejecución propio, progreso del ciclo actual con enlace a su informe, estimación basada en las duraciones reales del historial (3 min/paso si no hay datos) y arranque de un ciclo nuevo |
| Ejecución         | Paso actual con Pasa / Falla / Bloqueado / Saltar (teclas P / F / B / S), paso anterior (Retroceso), corrección de veredictos desde el registro, cronómetro por paso y por caso, observaciones y capturas. Sobrevive al cierre de la aplicación |
| Historial         | Ejecuciones archivadas (pasos, resultados, notas, duración) e informes de plan con exportación a Markdown |
| Reportar bug      | Formulario prellenado con el paso fallido; crea el issue en Jira y sube las capturas |
| Ajustes           | Conexión Jira (URL, proyecto, correo, token) y preferencias de captura (atajo, formato, modo, carpeta) |

## Dónde se guardan los datos

* Casos, planes, historial y ejecución en curso: directorio de datos de la aplicación (`~/.local/share/QAflow/QAflow/` en Linux).
* Ajustes: `~/.config/QAflow/QAflow.conf`.
* Capturas: carpeta configurable, por defecto `~/QAflow/capturas`.

En el primer arranque se cargan casos de ejemplo.

## Arquitectura

Ver [docs/architecture.md](docs/architecture.md).
