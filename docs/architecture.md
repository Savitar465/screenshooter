# Arquitectura de QAflow

QAflow es una aplicación de escritorio (Qt 6 Widgets, C++20) para equipos de QA: gestiona
casos de prueba, planes de regresión, ejecuciones manuales paso a paso con evidencias
(capturas de pantalla) y reporte de defectos a Jira.

## Capas

```
src/
├── core/            Modelos y contratos. Sin Qt Widgets, sin red, sin disco.
│   ├── models/      TestCase, TestRun, TestPlan, BugReport, Settings,
│   │                RunHistory (RunRecord, PlanRun), PlanReport (informe calculado + Markdown),
│   │                CaseFilter (criterios de la lista), CaseFormats (JSON / CSV / Markdown)
│   └── services/    ITestCaseRepository, IRunHistoryRepository, IRunSessionRepository,
│                    ISettingsRepository, IScreenCapture, IIssueTracker
├── application/     Casos de uso y estado observable (QObject + señales). Sin UI.
│   ├── TestCaseStore      fuente de verdad de los casos; toda mutación pasa por aquí;
│   │                      deshacer de un nivel para borrados
│   ├── CaseTransferService importación y exportación de casos a ficheros
│   ├── RunController      ejecución paso a paso (y cola de casos para el plan); archiva cada
│   │                      ejecución terminada en el historial y guarda la que está en curso
│   ├── RunHistoryStore    historial de ejecuciones y de planes; genera el PlanReport
│   ├── PlanStore          colección de planes, plan activo, orden de ejecución, ciclos y estimación
│   ├── SettingsStore      ajustes de Jira y de captura
│   ├── EvidenceService    captura → guarda fichero → adjunta al caso/paso activo
│   ├── BugReportService   borrador de bug desde la ejecución y envío al tracker
│   ├── SeedData           datos de ejemplo del primer arranque
│   └── AppContext         agrupa los servicios ya construidos para la presentación
├── infrastructure/  Implementaciones concretas de las interfaces de core.
│   ├── persistence/ JsonTestCaseRepository (cases.json, plans.json), JsonRunHistoryRepository
│   │                (history.json), JsonRunSessionRepository (session.json), QSettingsRepository
│   ├── capture/     ScreenCaptureService (QScreen::grabWindow), RegionSelector (overlay)
│   └── jira/        JiraClient (REST API v2: myself, issue, attachments)
└── presentation/    Widgets Qt. Depende de application; nunca de infrastructure.
    ├── theme/       Paleta (Theme.h) — los mismos valores viven en resources/styles/app.qss
    ├── widgets/     Piezas reutilizables: Ui (fábricas), LayoutButton, FlowLayout, Toast,
    │                FlashOverlay, ProgressCells, Thumbnail, TextArea, ShotCard
    ├── views/       Una clase por pantalla: Sidebar, CasesView, PlanView, RunView, HistoryView,
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
  `planChanged()`, `historyChanged()`, `jiraChanged()`, `captureChanged()`) para que cada vista refresque
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
| Planes                 | `$XDG_DATA_HOME/QAflow/QAflow/plans.json` (colección + plan activo; migra `plan.json` antiguo) |
| Historial              | `$XDG_DATA_HOME/QAflow/QAflow/history.json` (se escribe al cerrar cada ejecución) |
| Ejecución en curso     | `$XDG_DATA_HOME/QAflow/QAflow/session.json` (se borra al terminar; notas con retardo de 300 ms) |
| Ajustes Jira/captura   | QSettings (`~/.config/QAflow/QAflow.conf`)                  |
| Imágenes de capturas   | Carpeta configurable (por defecto `~/QAflow/capturas`)      |

## Gestión de casos

* **Suites** no son una lista aparte: `TestCaseStore::suites()` devuelve las que usa algún caso.
  Crear una suite es asignar un nombre nuevo al caso seleccionado; desaparece cuando ningún caso
  la usa. Los datos de ejemplo sólo se cargan en el primer arranque.
* **Metadatos**: `tags`, `component` y `jiraKey` viven en `TestCase` y viajan en todos los
  formatos. `TestCase::searchText()` es lo que consulta la búsqueda libre. `CaseFilter` (core)
  combina texto, suite, estado, prioridad y resultado de la última ejecución; la vista sólo
  rellena la estructura y pregunta `matches()`.
* **Pasos**: `insertStep`, `moveStep` y `removeStep` renumeran las capturas asignadas para que
  sigan a su paso (`remapShotSteps`).
* **Duplicar** copia contenido y metadatos, no capturas ni resultado; el nuevo caso queda en
  Borrador justo después del original.
* **Deshacer**: borrar un caso, un paso o una captura guarda una instantánea de la lista
  (`pushUndo`). `undo()` la restaura mientras no haya otra mutación ni pasen 20 s; después
  `commitUndo()` emite `filesReleased()` con los ficheros de capturas que ya nadie referencia y
  `EvidenceService` los borra del disco. El store nunca toca ficheros. La ventana muestra el
  aviso «Deshacer»; borrar un caso pide además confirmación.
* **Importar / exportar** (`CaseTransferService`): JSON (nativo, sin capturas al compartir),
  CSV (una fila por paso, RFC 4180) y Markdown (sólo exportación). Al importar, los ids
  existentes se actualizan conservando capturas y última ejecución locales; el resto se añaden.
  La serialización está en `core/models/CaseFormats` y la reutiliza `JsonTestCaseRepository`.

## Planes y ciclos

`TestPlan` es una lista **ordenada** de ids de caso con nombre, fecha y bandera `archived`.
`PlanStore` guarda la colección completa y el plan activo (`PlanCollection`) en `plans.json`;
las mutaciones de contenido (`toggle`, `moveCase`, `sortByPriority`, …) actúan sobre el activo y
`orderedCaseIds()` devuelve su orden saltando obsoletos e inexistentes (los obsoletos siguen en
el plan por si vuelven a estar listos; los borrados se retiran de todos los planes).

Un **ciclo** es una ejecución del plan: `RunController::startSequence()` abre un `PlanRun` en el
historial con el `planId` del plan. `PlanStore::latestCycle(planId)` devuelve el `PlanReport` del
ciclo más reciente (terminado o en curso), que es lo que muestran la pantalla de planes y la
tarjeta «Plan activo» del sidebar como progreso. Archivar un plan sólo lo oculta y bloquea
«Iniciar ciclo»; eliminarlo no toca el historial. Sin planes guardados se crea uno por defecto con
los casos de ejemplo.

## Ejecución paso a paso

`RunState` guarda el caso, el índice del paso actual, un `StepRecord` por paso marcado (resultado,
nota y segundos que estuvo en pantalla) y el cronómetro del paso actual. Resultados: `Pass`,
`Fail`, `Block` (termina la ejecución en ese punto) y `Skip` (N/A: no cuenta para el veredicto).

* `mark()` registra el paso y avanza; `back()` deshace el último veredicto y devuelve su nota al
  campo (también reabre una ejecución ya terminada); `setResult(i, r)` corrige un veredicto
  anterior y, si con ello desaparece el bloqueo, la ejecución continúa. Nada se archiva hasta que
  la ejecución termina y se cierra, así que estas correcciones no dejan rastro en el historial.
* **Persistencia de la sesión.** Tras cada cambio (`changed()`) el controlador guarda un
  `RunSession` (estado, cola del plan, id del plan) mediante `IRunSessionRepository`. Al arrancar,
  `load()` la restaura si el caso sigue existiendo, recorta resultados si el caso perdió pasos y
  reinicia el cronómetro del paso actual: el tiempo con la aplicación cerrada no cuenta, pero lo
  acumulado antes (`stepElapsedSecs`) sí. `MainWindow` abre directamente la pantalla de ejecución.
* **Duración real.** Cada `StepRecord` mide su tiempo; `RunRecord::durationSecs` es la suma. La
  vista muestra un reloj por paso y por caso (un `QTimer` de un segundo sólo actualiza etiquetas).
* **Estimación del plan.** `PlanStore::estimatedSecs()` usa la media real por paso de cada caso
  según su historial; para los casos sin historial, la media global; sin datos, 3 min por paso.
  `estimateBasis()` explica en la vista de qué datos sale.

## Historial de ejecuciones e informes de plan

Cada ejecución que termina (todos los pasos marcados, o un paso bloqueado) se convierte en un
`RunRecord`: instantánea del texto de los pasos, resultado y nota de cada uno, veredicto, inicio y
fin. `RunController::commitIfFinished()` la archiva en `RunHistoryStore` y actualiza la "última
ejecución" del caso (`Passed`, `Failed` o `Blocked`). Se archiva al pulsar «Finalizar», pero también
si el usuario arranca otro caso, repite o abandona con la ejecución ya terminada, para que nada se
pierda.

Al iniciar un plan se abre un `PlanRun` (nombre, casos en orden, inicio) y todos los `RunRecord`
que genera llevan su `planRunId`. Cuando la cola se vacía (o se abandona) el plan se cierra y
`RunController` emite `planCompleted(id)`. El `PlanReport` no se persiste: `PlanReport::build()`
lo calcula a partir del `PlanRun` y sus registros (si un caso se repitió dentro del plan cuenta la
última ejecución; los casos que quedaron sin ejecutar aparecen como pendientes). `toMarkdown()`
produce el informe exportable; la vista sólo abre el diálogo de guardado o copia al portapapeles.

`HistoryView` lista planes y ejecuciones sueltas (las de un plan se ven dentro de su informe, o con
el filtro «Casos»). `MainWindow::finishRun()` es la acción «Finalizar»: continúa con el siguiente
caso del plan, abre el informe cuando el plan termina o vuelve a la lista de casos.

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
(`MemoryRepo`, `MemoryHistoryRepo`), sin UI ni red: modelos, etiquetas de última ejecución, flujo de
ejecución/veredictos, paso anterior y corrección de veredictos, saltos N/A, cola del plan,
reasignación de capturas al borrar pasos, archivado en el historial, informe de plan (conteos,
pendientes, Markdown), continuidad de ids entre sesiones, restauración de la sesión, estimación
con duraciones reales, borrar/duplicar/deshacer, reordenación de pasos con capturas, filtros y
formatos JSON/CSV/Markdown (ida y vuelta), colección de planes (crear, duplicar, archivar,
borrar, persistir), orden propio del plan y ciclos enlazados a su plan.

```
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

## Herramienta de desarrollo: capturas de pantallas

```
QT_QPA_PLATFORM=offscreen QAFLOW_SNAPSHOT_DIR=/tmp/qaflow-shots ./build/qaflow
```

Renderiza cada pantalla (incluida una ejecución con un paso fallido, el bug prellenado y el
informe de un plan) a PNG y cierra. Usa un directorio de datos aislado (`$QAFLOW_SNAPSHOT_DIR/data`)
para no tocar los datos reales. Útil para revisar el diseño sin interacción.
