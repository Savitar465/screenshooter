# Arquitectura de QAflow

QAflow es una aplicación de escritorio (Qt 6 Widgets, C++20) para equipos de QA: gestiona
casos de prueba, planes de regresión, ejecuciones manuales paso a paso con evidencias
(capturas de pantalla) y reporte de defectos a Jira.

## Capas

```
src/
├── core/            Modelos y contratos. Sin Qt Widgets, sin red, sin disco.
│   ├── models/      TestCase, TestRun, TestPlan, BugReport, Settings
│   └── services/    ITestCaseRepository, ISettingsRepository, IScreenCapture, IIssueTracker
├── application/     Casos de uso y estado observable (QObject + señales). Sin UI.
│   ├── TestCaseStore      fuente de verdad de los casos; toda mutación pasa por aquí
│   ├── RunController      ejecución paso a paso (y cola de casos para el plan)
│   ├── PlanStore          selección de casos y estimación
│   ├── SettingsStore      ajustes de Jira y de captura
│   ├── EvidenceService    captura → guarda fichero → adjunta al caso/paso activo
│   ├── BugReportService   borrador de bug desde la ejecución y envío al tracker
│   ├── SeedData           datos de ejemplo del primer arranque
│   └── AppContext         agrupa los servicios ya construidos para la presentación
├── infrastructure/  Implementaciones concretas de las interfaces de core.
│   ├── persistence/ JsonTestCaseRepository (cases.json, plan.json), QSettingsRepository
│   ├── capture/     ScreenCaptureService (QScreen::grabWindow), RegionSelector (overlay)
│   └── jira/        JiraClient (REST API v2: myself, issue, attachments)
└── presentation/    Widgets Qt. Depende de application; nunca de infrastructure.
    ├── theme/       Paleta (Theme.h) — los mismos valores viven en resources/styles/app.qss
    ├── widgets/     Piezas reutilizables: Ui (fábricas), LayoutButton, FlowLayout, Toast,
    │                FlashOverlay, ProgressCells, Thumbnail, TextArea, ShotCard
    ├── views/       Una clase por pantalla: Sidebar, CasesView, PlanView, RunView,
    │                BugView, SettingsView y MainWindow (navegación + avisos globales)
    └── DevSnapshot  herramienta de desarrollo (renderiza cada pantalla a PNG)
```

### Flujo de dependencias

```
presentation ──► application ──► core ◄── infrastructure
                      ▲                          │
                      └──────── main.cpp ────────┘   (raíz de composición)
```

* `main.cpp` es el único sitio que conoce las clases de `infrastructure/`: crea los
  repositorios, el cliente Jira y el servicio de captura, y los inyecta en los servicios
  de `application/` a través de las interfaces de `core/services/`.
* Las vistas reciben referencias a los stores por `AppContext` y **nunca** mutan el modelo
  directamente: llaman a métodos del store y se redibujan al recibir su señal.
* Los stores emiten señales de grano fino (`caseChanged(id)`, `runChanged()`,
  `planChanged()`, `jiraChanged()`, `captureChanged()`) para que cada vista refresque
  sólo lo que le afecta. Las vistas usan una bandera `m_selfEdit` para no reconstruir
  campos que el usuario está escribiendo.

## Estilo visual

El tema oscuro está en `resources/styles/app.qss` y se selecciona por **roles**:
propiedades dinámicas (`role`, `active`, `running`, `invalid`) que se fijan con
`ui::setRole()` / `ui::setFlag()` y que el QSS resuelve con selectores
`QPushButton[role="primary"]`. Así las vistas no contienen colores salvo los que
dependen de datos (prioridad, veredicto), que salen de `theme::`.

## Persistencia

| Dato                   | Dónde                                                       |
|------------------------|-------------------------------------------------------------|
| Casos y capturas       | `$XDG_DATA_HOME/QAflow/QAflow/cases.json` (guardado diferido 400 ms) |
| Plan                   | `$XDG_DATA_HOME/QAflow/QAflow/plan.json`                    |
| Ajustes Jira/captura   | QSettings (`~/.config/QAflow/QAflow.conf`)                  |
| Imágenes de capturas   | Carpeta configurable (por defecto `~/QAflow/capturas`)      |

## Captura de pantalla

`ScreenCaptureService` oculta la ventana principal, espera al compositor y usa
`QScreen::grabWindow(0)`.

* **Pantalla completa**: la pantalla bajo el cursor.
* **Ventana activa**: en X11 usa `xdotool getactivewindow getwindowgeometry` para recortar;
  si no está instalado devuelve la pantalla completa.
* **Región**: overlay `RegionSelector` a pantalla completa; arrastrar para elegir, Esc cancela.

El atajo configurado (por defecto `Ctrl+Shift+S`) es un `QShortcut` de ámbito aplicación:
funciona mientras QAflow tiene el foco. Un atajo global de sistema requeriría código
específico por plataforma (X11/Wayland portal) y queda fuera de esta versión.

## Jira

`JiraClient` habla con la REST API v2:

* `GET /rest/api/2/myself` para probar la conexión (botón Conectado/Desconectado).
* `POST /rest/api/2/issue` con `issuetype=Bug`, descripción en formato wiki y etiquetas.
* `POST /rest/api/2/issue/{key}/attachments` (multipart, `X-Atlassian-Token: no-check`)
  por cada captura del caso.

Autenticación: si hay correo configurado → `Basic email:token` (Jira Cloud);
si no → `Bearer token` (PAT de Jira Server/Data Center).

## Tests

`tests/` compila sólo `core` + `application` contra un repositorio en memoria
(`MemoryRepo`), sin UI ni red: modelos, etiquetas de última ejecución, flujo de
ejecución/veredictos, cola del plan, reasignación de capturas al borrar pasos.

```
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

## Herramienta de desarrollo: capturas de pantallas

```
QT_QPA_PLATFORM=offscreen QAFLOW_SNAPSHOT_DIR=/tmp/qaflow-shots ./build/qaflow
```

Renderiza cada pantalla (incluida una ejecución con un paso fallido y el bug prellenado)
a PNG y cierra. Útil para revisar el diseño sin interacción.
