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

Tests unitarios:

```bash
ctest --test-dir build --output-on-failure
```

## Pantallas

| Pantalla          | Qué hace                                                                   |
|-------------------|----------------------------------------------------------------------------|
| Casos de prueba   | Lista filtrable por suite y búsqueda; editor de título, metadatos, precondiciones, pasos y evidencias |
| Plan de pruebas   | Selección de casos, estimación (3 min/paso) y arranque de la ejecución encadenada |
| Ejecución         | Paso actual con Pasa / Falla / Bloqueado (teclas P / F / B), observaciones, registro y capturas |
| Reportar bug      | Formulario prellenado con el paso fallido; crea el issue en Jira y sube las capturas |
| Ajustes           | Conexión Jira (URL, proyecto, correo, token) y preferencias de captura (atajo, formato, modo, carpeta) |

## Dónde se guardan los datos

* Casos y plan: directorio de datos de la aplicación (`~/.local/share/QAflow/QAflow/` en Linux).
* Ajustes: `~/.config/QAflow/QAflow.conf`.
* Capturas: carpeta configurable, por defecto `~/QAflow/capturas`.

En el primer arranque se cargan casos de ejemplo.

## Arquitectura

Ver [docs/architecture.md](docs/architecture.md).
