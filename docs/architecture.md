# Arquitectura de QAflow

QAflow es una aplicación de escritorio (Qt 6 Widgets, C++20) para equipos de QA: gestiona
casos de prueba, planes de regresión, ejecuciones manuales paso a paso con evidencias
(capturas anotadas, grabaciones GIF y ficheros adjuntos) y reporte de defectos a Jira, GitHub,
GitLab o Azure DevOps.

## Capas

```
src/
├── core/            Modelos y contratos. Sin Qt Widgets, sin red, sin disco.
│   ├── models/      TestCase, TestRun, TestPlan, BugReport, Settings (TrackerSettings, AppSettings), IssueLink,
│   │                RunHistory (RunRecord, PlanRun), PlanReport (informe calculado + Markdown),
│   │                Metrics (tasa por suite, evolución entre ciclos), CaseFilter, CaseFormats (JSON / CSV / Markdown),
│   │                Requirement (requerimientos de GESREQ: fila de la bandeja, ficha y motivos de fallo),
│   │                Issue (issue de QA, lo importado del requerimiento, cambios entre lecturas, filtro)
│   └── services/    ITestCaseRepository, IRunHistoryRepository, IRunSessionRepository, IBugRepository,
│                    ISettingsRepository, ISecretStore, IScreenCapture, IScreenRecorder, IGlobalHotkey, IIssueTracker,
│                    ITestManagement, IRequirementSource, IIssueRepository
├── application/     Casos de uso y estado observable (QObject + señales). Sin UI.
│   ├── TestCaseStore      fuente de verdad de los casos; toda mutación pasa por aquí;
│   │                      deshacer de un nivel para borrados
│   ├── CaseTransferService importación y exportación de casos a ficheros
│   ├── RunController      ejecución paso a paso (y cola de casos para el plan); archiva cada
│   │                      ejecución terminada en el historial y guarda la que está en curso
│   ├── RunHistoryStore    historial de ejecuciones y de planes; genera el PlanReport
│   ├── PlanStore          colección de planes, plan activo, orden de ejecución, ciclos y estimación
│   ├── SettingsStore      ajustes del gestor (token en ISecretStore), de captura y generales (idioma, tema, bandeja)
│   ├── EvidenceService    evidencias: captura (con cuenta atrás), grabación GIF, ficheros adjuntos,
│   │                      portapapeles y sustitución de la imagen anotada; único sitio que toca ficheros
│   ├── BugReportService   borrador de bug, envío al gestor, cola offline, estados y metadatos
│   ├── BugStore           libro de bugs: issues enlazados a su caso y cola de pendientes
│   ├── IssueStore         issues de QA: asociaciones con casos y planes, importación de GESREQ sin duplicados
│   ├── IssuePublishService publicación del issue en el gestor: crear, vincular, actualizar y estado
│   ├── RequirementSourceService conexión con GESREQ: probarla, bandeja, fichas y catálogo de sistemas
│   ├── SeedData           datos de ejemplo del primer arranque
│   └── AppContext         agrupa los servicios ya construidos para la presentación
├── infrastructure/  Implementaciones concretas de las interfaces de core.
│   ├── persistence/ JsonTestCaseRepository (cases.json, plans.json), JsonRunHistoryRepository
│   │                (history.json), JsonRunSessionRepository (session.json), JsonBugRepository (bugs.json),
│   │                JsonIssueRepository (issues.json),
│   │                QSettingsRepository (tracker, captura y app; sin token)
│   ├── capture/     ScreenCaptureService (QScreen::grabWindow o PortalScreenshot en Wayland), RegionSelector
│   │                (overlay), GifRecorder + GifEncoder (grabación a GIF), RecorderOverlay (control flotante)
│   ├── hotkey/      GlobalHotkey (RegisterHotKey / XGrabKey / Carbon), PortalShortcuts (portal de Wayland)
│   ├── secrets/     SecretStores: secret-tool (Linux), Keychain (macOS), DPAPI (Windows), fichero en claro
│   ├── http/        HttpClient: base HTTP (JSON, multipart, formularios, cookies, errores, reintentables) de tracker/, testmgmt/ y requirements/
│   ├── testmgmt/    ZephyrClient: ciclos, ejecuciones y evidencias en Zephyr for Jira
│   ├── requirements/ GesreqClient (sesión y lectura de GESREQ) y GesreqParser (sus páginas HTML → modelos)
│   └── tracker/     HttpTrackerClient (base) → JiraClient, GitHubClient, GitLabClient, AzureDevOpsClient;
│                    TrackerRouter despacha por TrackerSettings::kind
└── presentation/    Widgets Qt. Depende de application; nunca de infrastructure.
    ├── theme/       Paletas oscura y clara (Theme.h); resources/styles/app.qss usa tokens (@bg, @tint(green,30))
    ├── widgets/     Piezas reutilizables: Ui (fábricas, icono), Icons (glifos del rail), LayoutButton, FlowLayout, Toast,
    │                FlashOverlay, ProgressCells, MetricBars (RateBar, TrendChart), Thumbnail, TextArea, ShotCard,
    │                EvidencePreview (visor de la ejecución), ImageViewer (visor a tamaño completo),
    │                AnnotationEditor (anotaciones), EvidenceActions (acciones compartidas)
    ├── views/       Una clase por pantalla: IssuesView (+ RequirementImportDialog, JiraPublishDialog), CasesView, PlanView, RunView, HistoryView, BugView,
    │                SettingsView (+ SettingsDialog, su ventana); Sidebar (rail de iconos),
    │                StatusStrip (barra de estado) y MainWindow (menú, atajos, bandeja,
    │                navegación, avisos)
    └── DevSnapshot  herramienta de desarrollo (renderiza cada pantalla a PNG)
```

### Flujo de dependencias

```
presentation ──► application ──► core ◄── infrastructure
                      ▲                          │
                      └──────── main.cpp ────────┘   (raíz de composición)
```

Cada capa es una biblioteca estática en `CMakeLists.txt` (`qaflow_core`, `qaflow_application`,
`qaflow_infrastructure`, `qaflow_presentation`) que sólo enlaza con las capas de las que puede
depender: una dependencia en sentido contrario (p. ej. una vista que incluya un repositorio JSON)
falla al enlazar. El ejecutable `qaflow` es sólo `main.cpp` + recursos.

* `main.cpp` es el único sitio que conoce las clases de `infrastructure/`: crea los
  repositorios, el router de gestores, el llavero y el servicio de captura, y los inyecta en los servicios
  de `application/` a través de las interfaces de `core/services/`.
* Las vistas reciben referencias a los stores por `AppContext` y **nunca** mutan el modelo
  directamente: llaman a métodos del store y se redibujan al recibir su señal.
* Los stores emiten señales de grano fino (`caseChanged(id)`, `runChanged()`,
  `planChanged()`, `historyChanged()`, `bugsChanged()`, `trackerChanged()`, `captureChanged()`) para que cada vista refresque
  sólo lo que le afecta. Las vistas usan una bandera `m_selfEdit` para no reconstruir
  campos que el usuario está escribiendo.

## Estilo visual, tema e idioma

`resources/styles/app.qss` selecciona los estilos por **roles**: propiedades dinámicas (`role`,
`active`, `running`, `invalid`) que se fijan con `ui::setRole()` / `ui::setFlag()` y que el QSS
resuelve con selectores `QPushButton[role="primary"]`. Los colores del QSS son **tokens**
(`@bg`, `@text`, `@tint(green,30)`…) que `theme::stylesheet()` sustituye por la paleta activa.

`theme::` tiene dos paletas (`darkPalette()`, la del diseño de referencia, y `lightPalette()`);
`theme::apply()` fija la activa antes de construir las vistas, que leen `theme::Green`, etc. para
los colores que dependen de datos. Con `AppTheme::System` se consulta el esquema del sistema
(`QStyleHints::colorScheme` en Qt ≥ 6.5; antes, la luminosidad de la paleta de la plataforma).

**Idioma.** El código fuente está en español y todas las cadenas visibles pasan por `tr()` (vistas y
stores) o `QCoreApplication::translate("core"/"infrastructure", …)` (funciones libres y modelos).
Los valores que se persisten (`toString(Priority)` → "Alta", `toString(Verdict)`, `toString(CaptureMode)`,
severidades del bug) **no** se traducen: cada enum tiene además `label()` para mostrar. Los combos
guardan el enum como dato del ítem y muestran la etiqueta traducida. `resources/i18n/qaflow_en.ts`
es la traducción al inglés; `qt_add_lrelease` la compila e incrusta en `:/i18n` y el target
`qaflow_lupdate` la actualiza con las cadenas nuevas.

Cambiar idioma o tema emite `SettingsStore::appChanged`; `main.cpp` instala los traductores, aplica
la paleta y **reconstruye la ventana** (diferido con `QTimer::singleShot(0)` porque la señal sale de
un widget de la ventana anterior), conservando geometría y pantalla. Así ninguna vista necesita
implementar retraducción dinámica.

## Navegación: rail y barra de estado

La navegación imita a los IDE de JetBrains: `Sidebar` es un **rail** de 60 px con un icono por
pantalla (`icons::pixmap()` los dibuja con QPainter, sin depender del plugin SVG), el nombre y el
atajo en el tooltip, y una insignia sólo donde hay algo que atender (progreso de la ejecución en
curso, bugs pendientes de enviar o issues abiertos). El icono se redibuja en el color de su pantalla
cuando está activa y en `@muted` cuando no. Debajo de todo, separado, el botón de métricas.

Lo que antes eran las tarjetas del sidebar («En ejecución», «Tasa de éxito», «Plan activo») es ahora
`StatusStrip`, la barra del pie: los mismos datos en una línea y con los mismos destinos al hacer
clic. Ambas vistas se refrescan con las señales de los stores, como el resto.

## Menú, atajos y bandeja

`MainWindow::buildMenus()` crea el menú (Archivo, Editar, Ver, Ejecución, Ayuda) con `QAction`
y los atajos estándar (`QKeySequence::New`, `Find`, `Undo`, `Quit`, F5, Ctrl+1…5). Los atajos de
captura, de grabación y de la ejecución («Pasa y siguiente», «Falla y siguiente», «Paso anterior»)
son acciones de ámbito aplicación cuya tecla sigue a Ajustes; además `main.cpp` los registra en el
sistema con `IGlobalHotkey` (ver «Captura de pantalla»), para poder avanzar de paso y seguir
capturando sin traer QAflow al frente. Tras cada atajo de la ejecución,
`MainWindow::announceRunStep()` dice en qué paso ha quedado: en la bandeja si la ventana no está al
frente, y con el aviso de siempre si lo está. Las acciones
que operan sobre el caso seleccionado se habilitan según `TestCaseStore::selectedId()` y «Deshacer»
sigue a `canUndo()`. Con `QSystemTrayIcon` disponible hay icono en la bandeja (mostrar/ocultar,
capturar, grabar GIF, salir); si `AppSettings::closeToTray` está activo, cerrar la ventana la oculta
en lugar de salir. Arrastrar ficheros a la ventana (`dropEvent` → `attachFiles`) los adjunta al caso.

Los ajustes **no** son una pantalla de la pila: «Archivo → Ajustes» (Ctrl+,) abre `SettingsDialog`,
una ventana no modal, hija de la principal, que aloja la `SettingsView` y se crea la primera vez que
se abre (`MainWindow::openSettings()`). No es modal porque los cambios se guardan al momento y el de
idioma o tema reconstruye la ventana principal mientras la de ajustes sigue abierta; por eso
`main.cpp` la vuelve a abrir tras reconstruir si `MainWindow::settingsWindow()` decía que lo estaba.

## Errores de guardado

Todos los repositorios devuelven `bool` y los stores lo comprueban: `TestCaseStore::save()`,
`PlanStore::save()`, `RunHistoryStore::save()`, `BugStore::save()` y
`RunController::persistSessionNow()` emiten `saveFailed(what)` si el disco no acepta la escritura.
`TestCaseStore` mantiene `m_dirty` para que el siguiente guardado diferido lo reintente. `MainWindow`
muestra un aviso persistente con «Reintentar» que llama al `save()` correspondiente.

## Métricas

`core/models/Metrics` calcula al vuelo, sin persistir nada: `metrics::summary()` y
`metrics::bySuite()` a partir de la última ejecución de cada caso, y `metrics::cycles()` como la
lista cronológica de ciclos terminados (un `CycleMetrics` por `PlanRun` cerrado, construido con
`PlanReport::build`). `metrics::trend()` es la diferencia de tasa entre los dos últimos ciclos. El
barra de estado muestra la tasa global y la tendencia del plan activo; el panel «Métricas» del historial
muestra la tabla por suite (`RateBar`) y el gráfico de evolución (`TrendChart`, con un chip por plan).

## Persistencia

| Dato                   | Dónde                                                       |
|------------------------|-------------------------------------------------------------|
| Casos y capturas       | `$XDG_DATA_HOME/QAflow/QAflow/cases.json` (guardado diferido 400 ms) |
| Planes                 | `$XDG_DATA_HOME/QAflow/QAflow/plans.json` (colección + plan activo; migra `plan.json` antiguo) |
| Historial              | `$XDG_DATA_HOME/QAflow/QAflow/history.json` (se escribe al cerrar cada ejecución) |
| Ejecución en curso     | `$XDG_DATA_HOME/QAflow/QAflow/session.json` (se borra al terminar; notas con retardo de 300 ms) |
| Ajustes (gestor, captura, atajos de la ejecución) | QSettings (`~/.config/QAflow/QAflow.conf`), sin el token |
| Token del gestor       | `ISecretStore`: llavero del sistema; si no hay, QSettings en claro con aviso en Ajustes |
| Conexión con GESREQ    | QSettings (grupo `gesreq`: URL, usuario, conectado); la contraseña, en `ISecretStore` (`gesreq/password`) |
| Proyectos y su sistema de GESREQ | `$XDG_DATA_HOME/QAflow/QAflow/projects.json` (`requirementSystem` de cada proyecto) |
| Bugs y cola offline    | `$XDG_DATA_HOME/QAflow/QAflow/bugs.json`                    |
| Issues (asociaciones, lo importado de GESREQ, sus cambios y la publicación en el gestor) | `issues.json` en el directorio de datos de cada proyecto |
| Capturas, GIF y adjuntos | Carpeta configurable (por defecto `~/QAflow/capturas`)     |

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
* **Últimas ejecuciones.** El editor lista las cinco últimas del caso —veredicto, fecha, pasos,
  duración, cuántas evidencias dejó y de qué plan salió— y cada fila abre sus resultados en el
  historial (`openRunRequested` → `HistoryView::showRun()`), que es donde están sus pasos con su
  veredicto, sus evidencias y con qué está enlazada en Jira y Zephyr.
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
ciclo más reciente (terminado o en curso), que es lo que muestran la pantalla de planes y el
bloque «Plan» de la barra de estado como progreso; `cycles(planId)` devuelve todos los ciclos del
plan, del más reciente al más antiguo, con los que `PlanView` pinta el «Historial de ciclos»: una
fila por ciclo con su veredicto, cuántos casos se ejecutaron y cómo acabaron, la variación de la
tasa de éxito respecto al ciclo anterior terminado y las celdas de cada caso (el tooltip lista los
resultados caso a caso; el clic abre el informe en el historial). Se muestran los cinco últimos y un
botón despliega el resto. Archivar un plan sólo lo oculta y bloquea
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
* **La pantalla, en tres columnas.** `RunView` se construye con una función por columna:
  `buildCasePanel()` (estado del caso, progreso, la lista de pasos con su veredicto —la pastilla de
  cada paso ya marcado abre el menú para corregirlo— y «Cerrar ejecución»), `buildStepPanel()` (el
  paso activo, sus veredictos, el visor grande de la evidencia elegida con la barra «Asignar a» y
  las observaciones del paso) y `buildFilmPanel()` (la columna «Capturas», con todas las evidencias del caso).
  El visor es `EvidencePreview`, que dibuja la imagen ajustada al hueco con la etiqueta del paso y
  el nombre del fichero; las tarjetas de la columna de capturas son `ShotCard` con `Layout::Film`,
  que en vez de abrir el visor a tamaño completo emiten `selectRequested` para elegir qué se ve en
  grande. Una captura nueva se abre sola y la lista se desplaza hasta ella.
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
el filtro «Casos»). Un informe se puede eliminar desde su cabecera: `RunHistoryStore::removePlanRun()`
quita el `PlanRun` y sus `RunRecord`, suelta las evidencias de esas ejecuciones
(`TestCaseStore::releaseShotsOfRuns()`, que emite `filesReleased` para que `EvidenceService` borre
los ficheros) y, a los casos cuya «última ejecución» era una de las borradas, les deja la más
reciente que quede. Es definitivo, sin deshacer, y la vista no lo ofrece para el ciclo en curso
(`RunController::planRunId()`), que sigue recibiendo ejecuciones. `MainWindow::finishRun()` es la acción «Finalizar»: continúa con el siguiente
caso del plan, abre el informe cuando el plan termina o vuelve a la lista de casos.

## Captura de pantalla

`ScreenCaptureService` oculta la ventana principal, espera al compositor y usa
`QScreen::grabWindow(0)`.

* **Pantalla completa**: la pantalla bajo el cursor.
* **Ventana activa**: en X11 usa `xdotool getactivewindow getwindowgeometry` para recortar;
  si no está instalado devuelve la pantalla completa.
* **Región**: overlay `RegionSelector` a pantalla completa; arrastrar para elegir, Esc cancela.

**Wayland.** `grabWindow` devuelve negro, así que si la sesión es Wayland y hay un
`xdg-desktop-portal` con la interfaz `org.freedesktop.portal.Screenshot`, `ScreenCaptureService`
delega en `PortalScreenshot` (QtDBus, `QAFLOW_HAS_DBUS`): «Pantalla completa» y «Región» piden una
captura silenciosa del escritorio (se recorta la pantalla bajo el cursor y, para la región, se
reutiliza el overlay); «Ventana activa» pide la captura interactiva, en la que el compositor deja
elegir pantalla, ventana o zona. `AppContext::captureBackend` lleva el nombre del método a Ajustes.

**Cuenta atrás.** `EvidenceService::captureForSelectedCase()` respeta `CaptureSettings::delaySecs`:
emite `countdown(n)` cada segundo (la ventana lo muestra como aviso) y captura al llegar a 0; una
segunda llamada durante la cuenta atrás la cancela. Tras guardar, según los ajustes, copia la
imagen al portapapeles y `MainWindow` abre el editor de anotaciones (`shotAdded`).

**Atajo global.** `IGlobalHotkey` (core) se implementa en `infrastructure/hotkey/GlobalHotkey`:
`RegisterHotKey` + `WM_HOTKEY` en Windows, `XGrabKey` sobre la ventana raíz + filtro de eventos xcb
en X11 (`QAFLOW_HOTKEY_X11`, con y sin Bloq Num / Bloq Mayús), `RegisterEventHotKey` (Carbon) en
macOS y, en Wayland, `PortalShortcuts` sobre `org.freedesktop.portal.GlobalShortcuts` (crea una
sesión, pide los atajos con su combinación preferida y escucha `Activated`; el compositor puede
pedir confirmación). `main.cpp` registra «capture» y «record» con las teclas de Ajustes y los vuelve
a registrar al cambiarlas; si `bind()` falla o `globalShortcut` está desactivado, quedan los
`QAction` de ámbito aplicación. `IGlobalHotkey::status()` explica la situación en Ajustes.

## Evidencias: grabación, adjuntos, anotaciones y visor

**La evidencia es de la ejecución.** Se captura ejecutando, así que pertenece a la ejecución en la que
se tomó y no al caso: `Screenshot::runId` dice a cuál. Mientras la ejecución está en curso todavía no
tiene id —lo pone el historial al archivarla—, así que `runId` va vacío y `RunController::archive()`
lo sella con el de la ejecución (`TestCaseStore::sealShots()`). Sin ejecución en curso no se captura
nada: `EvidenceService` lo rechaza con un aviso, porque no habría ejecución a la que atarla.

Los ficheros los sigue guardando el caso (`TestCase::shots`): tienen un ciclo de vida —anotar, borrar,
deshacer, liberar del disco— que vive en `TestCaseStore`, y el historial son instantáneas que no se
reescriben. Pero se enseñan y se publican **por ejecución**: la pantalla de Ejecución muestra las de la
que está en curso (`shotsOfRun("")`), el historial las de cada ejecución archivada —en su ficha y en el
informe del plan, en tarjetas de sólo lectura: se abren, se anotan y se copian, pero no se reordenan ni
se borran—, «Reportar bug» adjunta las de la ejecución más reciente (`latestEvidence()`) y el ciclo de
Zephyr sube las de la ejecución que publica. La pantalla de Casos no las enseña.

`RunHistoryStore::adoptLooseEvidence()` migra los datos anteriores: las evidencias sueltas de cada caso
pasan a su última ejecución (conservando su paso) y las de un caso que nunca se ejecutó se sueltan de
la lista sin tocar los ficheros. Se llama al arrancar, después de restaurar la ejecución interrumpida,
cuya evidencia sigue siendo suya y todavía no puede sellarse.

* **Grabación a GIF.** `IScreenRecorder` (core) → `GifRecorder` (infrastructure): oculta la ventana,
  pide la región con `RegionSelector` (o toma la pantalla entera), muestra `RecorderOverlay`
  (tiempo, «Detener», Esc cancela) y captura con `grabWindow` a `fps` fotogramas por segundo hasta
  `stop()` o `maxSecs`. Cada fotograma se reduce a `maxWidth` (1280) y se pasa a `GifEncoder`, un
  codificador GIF89a propio: paleta por fotograma con median cut sobre un histograma de 15 bits
  (exacta si hay ≤ 256 colores) y LZW incremental con la misma política de tamaño de código que
  giflib. El retardo de cada fotograma es el tiempo real transcurrido, así el GIF mantiene el ritmo
  aunque capturar sea lento. `EvidenceService::toggleRecording()` crea `rec_NNN.gif` en la carpeta
  de capturas y lo adjunta como una evidencia más. No hay grabación en Wayland (haría falta el portal
  ScreenCast con PipeWire).
* **Ficheros adjuntos.** `EvidenceService::attachFiles()` copia cada fichero a la carpeta de
  capturas como `adj_NNN_<nombre>` y lo añade al caso (asignado al paso en ejecución). `Screenshot`
  distingue con `isImage()` / `isAnimation()` / `extension()`: las imágenes se muestran y anotan, los
  demás ficheros salen con su extensión en la miniatura y se abren con la aplicación del sistema.
  Los gestores los suben por su tipo MIME (GitHub sigue sin admitir adjuntos).
* **Anotaciones.** `AnnotationEditor` (presentation/widgets) dibuja sobre la imagen: flecha,
  rectángulo, elipse, marcador, texto y difuminado (pixelado por bloques). `renderAnnotations()` es
  una función pura que aplica la lista de `Annotation` en coordenadas de la imagen original; el
  lienzo sólo escala. Al guardar, `EvidenceService::replaceImage()` sobrescribe el fichero y
  `TestCaseStore::notifyShotFileChanged()` hace que las vistas recarguen la miniatura.
* **Visor.** `ImageViewer` muestra las evidencias del caso a tamaño completo con navegación,
  zoom (ajustar nunca amplía), arrastre y acciones de copiar, anotar y mostrar en la carpeta.
  `EvidenceActions` reúne estas acciones para que las tres vistas con miniaturas (Casos, Ejecución,
  Reportar bug) sólo conecten sus `ShotCard` con `wireCard()`.

## Bugs y gestores de incidencias

`IIssueTracker` (core) tiene cuatro operaciones asíncronas: probar conexión, crear issue,
consultar estado y leer metadatos del proyecto (tipos, prioridades, componentes, versiones,
asignables). `TrackerSettings` describe la conexión y su `kind` elige el gestor; `TrackerRouter`
(infrastructure) despacha al cliente correspondiente, todos sobre `HttpTrackerClient`, que
centraliza peticiones JSON/multipart, mensajes de error y la detección de fallos **reintentables**
(errores de red y 5xx, no rechazos del contenido).

| Gestor        | Crear                                   | Adjuntos                       | Campos mapeados |
|---------------|-----------------------------------------|--------------------------------|-----------------|
| Jira (v2)     | `POST /rest/api/2/issue`                | `/issue/{key}/attachments`     | issuetype, priority, assignee (accountId en Cloud, name en Server), components, versions, labels |
| GitHub        | `POST /repos/{owner}/{repo}/issues`     | no (se listan por nombre)      | labels (componentes + prioridad), assignees |
| GitLab (v4)   | `POST /projects/{id}/issues`            | `/uploads` antes, enlazados en Markdown | labels, assignee_ids, issue_type |
| Azure DevOps  | `POST /{proj}/_apis/wit/workitems/$Tipo` (JSON Patch) | `/_apis/wit/attachments` + relación AttachedFile | Priority (1-4), AssignedTo, Tags, FoundIn |

**Proyecto de Jira de cada proyecto.** `IIssueTracker::canListProjects()` y `fetchProjects()` son
opcionales y sólo Jira los implementa (`GET /rest/api/2/project`: los proyectos que ve el usuario, ordenados
por nombre); `BugReportService` los expone a la presentación y Ajustes los ofrece en «Buscar…», junto al
código Jira de «Configuración del proyecto». En los demás gestores el proyecto es un ajuste general que se
escribe.

**Los issues de QAflow en el gestor.** `canPublishIssues()`, `publishIssue()`, `fetchIssue()` y
`updateIssue()` son opcionales y sólo Jira los implementa: crean el issue (`POST /rest/api/2/issue` con
tipo, etiquetas y descripción), leen uno existente para vincularlo (`GET …/issue/{clave}`, donde un 404 se
cuenta como «esa clave no existe») y reescriben sólo título y descripción (`PUT …/issue/{clave}`), sin tocar
el resto de campos. Son otra cosa que `createIssue()`, que crea **defectos** desde la pantalla de bugs.

**Autenticación de Jira.** La API v2 la hablan tanto Jira Cloud como Jira Server / Data Center, pero
las credenciales cambian, así que `TrackerSettings::jiraAuth` (core) lo dice explícitamente en vez de
deducirlo del campo de usuario:

| `JiraAuth`    | Cabecera                          | Para                                              | Identidad de las personas |
|---------------|-----------------------------------|---------------------------------------------------|---------------------------|
| `CloudToken`  | `Basic base64(correo:API token)`  | Jira Cloud                                        | `accountId`               |
| `ServerBasic` | `Basic base64(usuario:contraseña)`| Jira Server / Data Center, incluida la **8.5.1**  | `name` (nombre de usuario)|
| `ServerToken` | `Bearer <PAT>`                    | Jira Server / Data Center 8.14+                   | `name`                    |

`needsUser()` y `usesAccountId()` son las dos preguntas que el resto del código hace al modelo:
la primera decide si Ajustes pide usuario y si `JiraClient` puede llamar sin él; la segunda, si el
asignado viaja como `accountId` o como `name` al crear el issue y al leer los asignables del proyecto.
Los ajustes anteriores se migran al cargar: había correo → `CloudToken`, no había → `ServerToken`.

Jira Server rechaza el login con las cabeceras de Seraph (`X-Seraph-LoginReason`,
`X-Authentication-Denied-Reason`), que `HttpTrackerClient::Response` expone y `JiraClient::errorFor()`
traduce a un mensaje accionable: contraseña incorrecta, PAT en una versión que no los admite o el
**bloqueo por CAPTCHA** que Jira aplica tras varios intentos fallidos y que sólo se levanta entrando
por el navegador.

### Gestión de pruebas (Zephyr)

`ITestManagement` (core) es una interfaz aparte de `IIssueTracker`: aquélla crea defectos, ésta
publica el resultado de un ciclo. `ZephyrClient` (infrastructure/testmgmt/) la implementa sobre la
misma instancia y las mismas credenciales de Jira — comparte `HttpClient` y `jiraAuthorization()`
con los clientes de gestores, pero no hereda su interfaz.

Zephyr sirve su API por dos rutas según la versión del plugin, y el cliente prueba las dos en orden:

| Ruta | Cuándo |
|------|--------|
| `/rest/zapi/latest` | ZAPI pública: add-on aparte hasta Zephyr 5.6, incluida de fábrica desde entonces |
| `/rest/zephyr/latest` | La que publica el propio plugin y usa su interfaz web; lo único disponible por debajo de la 5.6 |

Un 404 significa «esa ruta no está aquí» y se pasa a la siguiente; cualquier otro fallo (401, red)
se informa tal cual en vez de disimularlo probando la otra.

`TestPublishService` (application) traduce un `PlanReport` a un `PublishRequest`: sólo las filas
ejecutadas, cada una con su `runId` y el Test que ya tenga (`RunRecord::testKey`), el caso tal y como
está escrito (título, precondiciones y pasos) y las evidencias de esa ejecución que sigan en disco con
el paso al que se asignaron.
El cliente encadena entonces, por cada caso:

| Paso | Petición |
|------|----------|
| Resolver los ids | `GET /rest/api/2/project/{clave}` (Zephyr trabaja con ids numéricos, no con claves): el del proyecto, el de la versión y el del tipo de incidencia de los Tests; y `GET /rest/api/2/issue/{testKey}?fields=id` para la ejecución que ya tiene Test (republicación) |
| Crear el ciclo | `POST {api}/cycle` con `projectId`, `versionId` y las fechas en el formato de Zephyr (`12/May/26`) |
| Crear el Test que falta | `POST /rest/api/2/issue` (tipo Test, título y precondiciones del caso) y un `POST {api}/teststep/{issueId}` por paso |
| Añadir el caso | `POST {api}/execution` → la respuesta viene indexada por el id de la ejecución creada |
| Veredicto del caso | `PUT {api}/execution/{id}/execute` con 1 PASS · 2 FAIL · 4 BLOCKED |
| Veredicto por paso | `GET {api}/stepResult?executionId=` y `PUT {api}/stepResult/{id}` (N/A queda sin ejecutar, -1) |
| Evidencias | `POST {api}/attachment?entityId=&entityType=` — `TESTSTEPRESULT` las de un paso, `EXECUTION` las demás |

**Cada ejecución, su Test.** Cada informe de plan es único, y un caso de QAflow se ejecuta en muchos
ciclos: si todos compartieran un Test, publicar el último ciclo cambiaría lo que enlazan los informes
anteriores. Por eso el Test de Zephyr es de la ejecución, no del caso: `RunRecord::testKey` (en
`history.json`) guarda el que se creó para ella al publicar su informe, y el caso no tiene ninguno.
`TestPublishService::requestFor()` manda cada ejecución con su `runId` y con el Test que ya tenga; a
la que no tiene, `ZephyrClient::createTestForCase()` se lo estrena a partir del caso —título,
precondiciones y pasos, etiquetado `qaflow` y con el id del caso, y con el nombre del ciclo en la
descripción para distinguirlo de los Tests del mismo caso en otros ciclos— y la clave vuelve en
`PublishResult::createdTests`. Con ella, `TestPublishService` llama a
`RunHistoryStore::assignTestKeys()`, que la guarda en la ejecución aunque el ciclo haya fallado a
medias: el Test ya existe en Jira y el reintento debe reutilizarlo, no duplicarlo. Republicar un
informe viaja, por tanto, con sus Tests; publicar otro ciclo del mismo caso estrena otros. El
`PlanReport` toma `row.testKey` de la ejecución (la historia de Jira sí sale del caso de hoy).

El tipo de incidencia con el que se crean sale de los ajustes y por defecto es `Test`, el que instala
Zephyr; en un Jira traducido se llama de otra manera, y si el proyecto no lo tiene se dice con su
nombre —también al probar la conexión— en vez de fallar issue a issue sin explicar por qué.

**Lo que se publica son los resultados, y el ciclo de plan recuerda dónde quedaron.** Al publicar con
éxito, `TestPublishService` llama a `RunHistoryStore::markPublished()` y el `PlanRun` guarda
`zephyrCycleId` y `publishedAt` (en `history.json`). Con eso, el informe del plan dice «Publicado en
Zephyr el … · ciclo N», un enlace al ciclo en Jira y dos acciones: **actualizar** ese ciclo o publicar
otro. El enlace lo construye `TrackerSettings::zephyrCycleUrl()` (`TestPublishService::cycleUrl()` le
pasa el nombre con el que se publicó, plan y fecha): Zephyr Server no expone una página con id
estable para un ciclo, así que apunta a la búsqueda de ejecuciones, `/secure/enav/#?query=` con la
ZQL `project = "CLAVE" AND cycleName = "nombre"`, que lista exactamente las de ese ciclo. Actualizar
(`TestPublishService::update()`) manda la misma petición con `PublishRequest::cycleId`: el cliente
comprueba que el ciclo sigue existiendo (`GET {api}/cycle/{id}`), busca para cada Test la ejecución
que ya tiene en él (`GET {api}/execution?issueId=`) y la reutiliza —o la crea, si ese caso se quedó
fuera la vez anterior—, vuelve a fijar veredictos y pasos, y antes de subir las evidencias descarta
las que ya están en su destino (`GET {api}/attachment/attachmentsByEntity`, por nombre de fichero).
Publicar otra vez sin actualizar avisa de que Zephyr creará un ciclo nuevo. El diálogo previo, la
llamada y el resumen del resultado están en `ZephyrPublishFlow` (presentation/widgets), que comparten
`HistoryView` y `PlanView`: la pantalla de planes enseña en cada ciclo publicado sus ejecuciones con
el Test de cada una y el botón de actualizar, y en los no publicados el de publicar. El enlace queda así en los dos sentidos: cada ejecución de caso apunta a
su Test, y cada ejecución de plan apunta al ciclo donde se publicaron sus resultados.

**Y el historial lo enseña.** `PlanReportRow` lleva el `jiraKey` y el `testKey` del caso —los de hoy,
que salen del catálogo por `PlanReport::CaseLookup`, no una foto del día de la ejecución—, así que
cada caso del informe muestra sus dos enlaces como chips que abren el issue en el navegador
(`HistoryView::issueLinks()` → `openJiraRequested`), y el detalle de una ejecución añade el ciclo de
Zephyr del plan al que pertenece. El Markdown exportado lleva lo mismo: el ciclo en la cabecera y
«**Historia:** … · **Test:** …» bajo cada caso, para que el informe pegado en un ticket diga con qué
está enlazado sin abrir QAflow.

El fallo de un caso no aborta el ciclo: se anota en `PublishResult::skipped` con su motivo y se sigue
con el siguiente, que es lo que interesa cuando se publican decenas. Sí abortan los fallos previos
(proyecto, versión inexistente o creación del ciclo), porque sin ellos no hay dónde publicar. Ahí
entra también lo que se queda fuera sin ser un error de red: un Test que Jira rechaza, un paso del
Test que no entra, una evidencia que ya no está en disco y los veredictos que se quedan sin sitio
cuando el Test tiene menos pasos que el caso — el Test enlazado y su caso se editan por separado y se
desincronizan. El informe los enseña al terminar con su motivo, uno por línea, porque un contador de
«3 sin publicar» no dice qué hay que arreglar.

La ruta detectada se recuerda por instancia (`m_apiFor`), y una detección que no encuentra nada la
olvida entera: dejar puesta la última que se probó hacía que la siguiente publicación contra una
instancia que sí tenía Zephyr saliera por la ruta equivocada. Que el plugin no esté no se marca como
reintentable — no lo arregla insistir —, y un 401 o una caída de red sí.

**Libro de bugs.** `BugReportService::submit()` crea el issue y guarda un `IssueLink` (clave,
url, título, caso, gestor, fecha) en `BugStore`; el editor de casos y la pantalla de bugs lo
muestran con su último estado. «Actualizar estados» recorre los issues del gestor actual con
`fetchStatus()` y marca los resueltos.

**Cola offline.** Si `createIssue()` falla de forma reintentable, el bug entra en
`BugStore::pending()` con su error. «Reintentar envío» (o el arranque de la app con el gestor
conectado) vuelve a enviarlos en orden: un rechazo del contenido deja el bug en la cola con el
error y sigue con el siguiente; un fallo de red detiene la ronda.

**Metadatos.** `loadMetadata()` cachea por gestor+proyecto y se invalida al cambiar los ajustes.
Los combos del formulario son editables: funcionan sin cargar nada.

**Asignados.** El campo «Asignado a» no se conforma con los primeros del proyecto:
`BugReportService::searchAssignees()` pregunta al gestor según se escribe si éste sabe buscar
(`IIssueTracker::canSearchAssignees()`, cierto sólo en Jira) y, si no, filtra en local los asignables
que trajo `fetchMetadata()`. `JiraClient` traduce la búsqueda a
`GET /rest/api/2/user/assignable/search`, con `username` en Server y `query` en Cloud, descarta las
cuentas desactivadas y devuelve el id que espera cada uno (`name` o `accountId`). La vista espera
300 ms entre pulsaciones, descarta las respuestas que llegan tarde y conserva lo escrito; si la
búsqueda falla, se queda con la lista anterior y enseña el error junto a los campos del gestor.

**Secretos.** `SettingsStore` guarda el secreto (API token, contraseña o PAT) en `ISecretStore` bajo `tracker/<gestor>/token`,
uno por gestor, y nunca lo pasa al repositorio de ajustes. `makeSecretStore()` elige el llavero
disponible comprobándolo con una escritura de prueba (`secret-tool` en Linux, `security` en
macOS, DPAPI en Windows) y, si no hay ninguno, cae a QSettings en claro; Ajustes muestra cuál se
usa. Un token que quedara en claro de una versión anterior se migra al llavero en la primera carga.

## Issues

Un **issue** organiza el trabajo de QA de un requerimiento: se importa de la bandeja de GESREQ (o se crea a
mano) y reúne sus casos, sus planes y los resultados de sus pruebas. Es la primera pantalla del rail
(Ctrl+6, que conserva los atajos anteriores).

`Issue` (core) separa lo que se escribe en QAflow de lo que se trae del sistema:

| Parte | Qué lleva | Quién la cambia |
|-------|-----------|-----------------|
| Del issue | id local (`IS-0001`, independiente de Jira), título, notas, prioridad, estado de QA, `caseIds`, `planIds` | QAflow |
| `RequirementLink` | conexión, la fila de la bandeja (`ExternalRequirement`), la ficha si se consultó, fechas de importación y lectura, `missing` y `changes` | cada consulta a GESREQ |
| Representación en Jira | `jiraKey`, `jiraUrl` (el filtro «Jira» ya los usa; la publicación llega después) | la publicación |

El **estado de QA** (Pendiente, En preparación, En pruebas, Finalizado) es de QAflow y no se deduce del
estado de GESREQ, del de Jira ni del resultado de las pruebas. La prioridad y el título salen del
requerimiento al importarlo y a partir de ahí son de QAflow. Un caso puede validar varios issues: la
asociación vive en el issue, así que `cases.json` y `plans.json` no cambian y los casos y planes sin
issue siguen como estaban; los ids que ya no existen se enseñan como tales, sin borrarlos.

`IssueStore` (application) persiste en `issues.json` (`JsonIssueRepository`, un fichero por proyecto) en
cada cambio. Un fichero que existe pero no se puede leer —dañado o de otra versión— deja el store en solo
lectura (`isReadOnly()`, `loadFailed`): nada se escribe encima, y cambiar de proyecto no queda bloqueado por
ello.

**Importación sin duplicados.** Un requerimiento se identifica por su número dentro de la conexión (la
dirección de GESREQ, sin distinguir la barra final ni mayúsculas); el número GREQ es único en el sistema,
así que un cambio de sistema en GESREQ aparece como un cambio del issue en vez de como un issue nuevo.
«Consultar GESREQ» (`IssuesView::consultRequirements`) lee la bandeja entera y:

1. `markInboxRead()` marca como ausentes los issues importados de esa conexión que ya no están en ella (el
   control de calidad terminó o se reasignó) y como presentes los que sí; nunca borra nada.
2. `previewImport()` clasifica los requerimientos del sistema vinculado al proyecto en nuevos, con cambios
   (y cuáles) o sin cambios; los de otros sistemas se enseñan aparte, para empezar sus pruebas donde toque.
3. `RequirementImportDialog` los enseña marcados (nuevos y con cambios) y `importRequirements()` crea los
   nuevos y, en los ya importados, reemplaza sólo lo extraído. Lo que cambió (`diffRequirement`: estado,
   descripción, prioridad, sistema, fechas de asignación, solicitante…) se acumula en `changes` con
   `mergeChanges` —de cada campo, el valor revisado por última vez y el último leído; si vuelve a como
   estaba, desaparece— hasta «Marcar como revisado». El rail cuenta los issues con cambios sin revisar.

Sin sistema vinculado al proyecto, la consulta no se lanza y abre los ajustes. La ficha del requerimiento
se lee bajo demanda («Cargar ficha», `RequirementSourceService::fetchDetail`) y se guarda en el issue con su
fecha; «Abrir en GESREQ» abre la ficha en el navegador, donde hace falta haber entrado.

**Pantalla.** `IssuesView` sigue el esquema de Casos: lista filtrable (texto sobre `Issue::searchText()`,
estado, prioridad, publicación en Jira) y el issue a la derecha, con su requerimiento (cambios, ausencia,
datos y ficha), notas de QA (se guardan 600 ms después de dejar de escribir, al cambiar de issue o al salir
de la pantalla), casos (crear uno con el título del issue y abrirlo, o vincular con `ChoiceDialog`),
planes (crear uno con los casos del issue, que queda activo y se abre, o vincular) y resultados:
`IssueStore::runsOf()` junta las ejecuciones de sus casos, la más reciente primero, con el plan y el ciclo
al que pertenecen.

**Iniciar pruebas (entre proyectos).** La bandeja de GESREQ es del usuario, no del proyecto: el diálogo de
importación enseña también los requerimientos de los demás sistemas, cada uno con el proyecto que los
trabaja (`ProjectStore::projectForRequirementSystem`). «Iniciar pruebas» resuelve ese proyecto y:

| Situación | Qué pasa |
|-----------|----------|
| Es el proyecto activo | `IssueStore::openForRequirement()` abre su issue aquí mismo: lo crea la primera vez y luego reutiliza el que hay, con sus casos, planes y lo escrito en QAflow |
| Es otro proyecto | La vista sólo lo pide (`IssuesView::startTestingRequested` → `MainWindow`); la raíz de composición guarda el actual, activa el destino y allí abre el issue (`MainWindow::startTesting`) |
| Ningún proyecto tiene ese sistema vinculado | `ProjectSetupDialog` pregunta en cuál se prueban: uno que ya existe (se le vincula el sistema) o uno nuevo, con el sistema ya escrito y su código Jira opcional; hecho eso se sigue por una de las dos filas anteriores |
| Ejecución o captura en curso | `ProjectSession::canLeave()` no deja salir y dice qué hay que terminar; si el guardado falla, el cambio se cancela y no se inicia nada |

Ninguna vista cambia de proyecto por su cuenta, y consultar la bandeja o previsualizar la importación
tampoco: sólo «Iniciar pruebas» lo pide. `canLeave()` es la misma regla que usa el selector de proyectos de
la barra, así que empezar unas pruebas y cambiar de proyecto a mano se comportan igual.

**Alta de proyecto (`ProjectSetupDialog`).** El mismo diálogo sirve para «Nuevo proyecto…» de la barra y para
la bandeja de GESREQ, y en los dos casos los dos códigos del proyecto son opcionales: el de Jira y el sistema
de GESREQ, ambos con «Buscar…» (`ChoiceDialog`) sobre lo que hay en cada sistema. Desde la bandeja llega
además el sistema ya escrito y se puede elegir un proyecto que ya existe en vez de crear otro —lo habitual
cuando está creado pero nadie le vinculó el sistema—; el aviso de debajo dice con qué proyecto choca lo
escrito, o qué sistema deja de trabajar el elegido, y nada se guarda a medias: si el sistema es de otro
proyecto el diálogo no se cierra. El sistema se guarda en el catálogo al aceptar; el código Jira no, porque
vive en los ajustes de cada proyecto y sólo los tiene abiertos su sesión: el diálogo lo devuelve y la ventana
lo pide con `projectJiraKeyRequested`, que la raíz de composición aplica sobre la sesión de ese proyecto
antes de activarlo.

**Publicación en el gestor.** `IssuePublishService` (application) crea la representación del issue en Jira,
o enlaza una que ya existe, y guarda en `Issue::publication` las tres identidades juntas: el requerimiento
de GESREQ, el issue de QAflow y el issue del gestor (con su instancia, proyecto, tipo, estado y fechas).

| Acción | Qué hace |
|--------|----------|
| Publicar | `draftFor()` arma título y descripción (lo importado de GESREQ, las notas de QA y de qué issue salió) y el diálogo los enseña para corregirlos antes de enviar; se crea con las etiquetas `qaflow`, el id del issue y `GREQ-<número>` |
| Vincular | `fetchIssue()` comprueba que la clave existe y la guarda como `linked`: lo escribió otra persona, así que QAflow no ofrece sobrescribirlo |
| Actualizar | `needsUpdate()` compara lo de ahora con `publishedTitle`/`publishedDescription` (lo último que salió de QAflow) y avisa; sólo esta acción reescribe el título y la descripción en el gestor, diciendo antes que lo editado allí se pierde |
| Estado | `refreshStatus()` guarda el estado del gestor, que se enseña aparte del estado de QA |

Nada se publica ni se sobrescribe solo. Si un envío se corta sin respuesta, el issue queda marcado como
**sin confirmar** (`publication.uncertain`): puede haberse creado igualmente, así que la pantalla dice cómo
buscarlo por su etiqueta y publicar otra vez pide confirmación expresa. Un rechazo del contenido (un tipo de
incidencia que no existe, por ejemplo) no deja esa duda y no marca nada.

## Requerimientos externos (GESREQ)

QAflow importará el trabajo de QA desde GESREQ, el sistema de gestión de requerimientos: cada
requerimiento importado será un issue de QAflow con sus casos y planes. De ese flujo existe por ahora el
conector, que sólo lee.

**Ajustes.** La conexión (URL, usuario y contraseña) es del usuario y común a todos los proyectos, como la
del gestor: `SettingsStore::requirementSource()` guarda URL, usuario y si la última prueba entró en el grupo
`gesreq` de QSettings, y la contraseña en `ISecretStore` bajo `gesreq/password` (la que quedara en claro se
migra al cargar). main.cpp crea un único `GesreqClient` que comparten todas las `ProjectSession`, así la
sesión de GESREQ no se repite al cambiar de proyecto. El **sistema de GESREQ** que se trabaja en cada
proyecto (`SUMA TRANSITO`) no es un ajuste general sino un dato del proyecto: vive en el catálogo
(`Project::requirementSystem`, en `projects.json`) y se edita en «Configuración del proyecto», junto al
código Jira. `ProjectStore::setRequirementSystem()` rechaza con `failed` el sistema que ya es de otro
proyecto —sin distinguir mayúsculas ni espacios repetidos—, porque al iniciar las pruebas de un
requerimiento tiene que haber un único proyecto al que ir; `projectForRequirementSystem()` es esa búsqueda.
`RequirementSourceService` (application) prueba la conexión con los ajustes guardados, deja `connected` y,
si entra, lee la bandeja para ofrecer sus sistemas: Ajustes los autocompleta en el campo del proyecto y
avisa, mientras se escribe, del proyecto con el que choca lo escrito (y con un toast si aun así se confirma,
porque Intro cierra el diálogo).

Los dos códigos de «Configuración del proyecto» se eligen además de lo que hay en cada sistema con
«Buscar…», que abre `ChoiceDialog` (presentation/widgets): consulta la lista al abrir (los proyectos de Jira
o el catálogo de sistemas de GESREQ), deja reintentar si falla, filtra en local por código y nombre palabra a
palabra y sin tildes, y se maneja con flechas e Intro sin salir de la búsqueda; la respuesta que llega con la
ventana ya cerrada, o después de reintentar, se descarta. En el catálogo de GESREQ van primero los sistemas
con requerimientos en la bandeja, y se señala el que ya está vinculado a otro proyecto. Escribir el código a
mano sigue valiendo.

`IRequirementSource` (core) tiene tres operaciones asíncronas: probar la conexión, leer la bandeja de
control de calidad del usuario (`fetchInbox`) y leer la ficha de un requerimiento (`fetchDetail`).
`GesreqClient` (infrastructure/requirements/) la implementa sobre `HttpClient` y deja todo lo que depende
del marcado en `GesreqParser` (`gesreq::`), funciones puras de HTML a modelos: un cambio en las páginas
del sistema se corrige ahí y en sus fixtures, sin tocar la sesión ni lo que se haga con los requerimientos.

GESREQ es una aplicación Struts/JSP con las páginas generadas en el servidor, así que basta HTTP con la
cookie de sesión; no hace falta un navegador. `HttpClient` aporta para ello `postForm()` (el formulario
codificado entero: con `QUrlQuery` una contraseña con `+` llegaría con un espacio), `pageRequest()` y
`clearCookies()`.

| Paso | Petición | Qué se lee |
|------|----------|------------|
| Entrada | `GET /greq/` | abre la sesión del servidor (`JSESSIONID`) |
| Login | `POST login.do` con `usuario` y `clave` → 302 a `dashboard.do` | con `logout.do`, dentro, y el nombre del usuario en el menú; con el formulario `AuthForm`, rechazado |
| Bandeja | `GET calidadreg.do` («Registro Control Calidad») | `table#main-table`, con las columnas buscadas por su cabecera (sin tildes ni mayúsculas) |
| Ficha | `GET publico.do?id=N&bandera=1` | pares `th`/`td` de la tabla general, bloques `h5.titulo`, adjuntos `docDownload.do`; se descartan las `div.modalWindow` con el historial de cada control |
| Catálogo de sistemas | `GET poai.do` («Seguimiento Requerimiento») y, si no trae el desplegable, `GET registroadicional.do` | `select[name=sistema]`: el valor es el mismo código que usa la bandeja y el texto, «CÓDIGO - NOMBRE» |

El cliente nunca pide `calidadregGestionRequerimiento.do` («Registrar»), que cambia el estado del requerimiento.

`ExternalRequirement` es una fila de la bandeja: el número GREQ es el identificador estable, `systemCode`
(lo que va antes del primer guion de «Sistema») es el proyecto externo que se vinculará a un proyecto de
QAflow, y `states` es una lista porque un requerimiento está a la vez en «CONTROL DE CALIDAD OBSERVADO» y
«CONTROL FUNCIONAL». `RequirementDetail` lleva los datos generales con campo propio y todos en `fields`,
las secciones, los adjuntos y el alcance en texto plano con sus párrafos y viñetas.

**Una página que no se entiende nunca es una bandeja vacía.** GESREQ no usa códigos HTTP para la sesión:
todo llega con 200, y el cliente lo distingue por el contenido.

| Respuesta | Qué es | Qué hace el cliente |
|-----------|--------|---------------------|
| El formulario `AuthForm` en lugar de la página | sesión caducada | inicia otra y repite una vez; si la recién iniciada tampoco vale, `Credentials` («no conserva la sesión») |
| La cáscara de la ficha sin número, con un script a `error2.jsp` | ficha pedida sin sesión | lo mismo; con la sesión recién iniciada, `NotFound` |
| La ficha con su número y sin ninguna tabla | con sesión, el requerimiento no existe | `NotFound`, sin repetir |
| Sin la tabla, sin una columna imprescindible (Requerimiento, Sistema, Descripción Corta, Estado), una fila sin número o una ficha sin «Estado» | la página cambió | `PageChanged`, diciendo qué falta |

`RequirementSourceFailure` completa la lista con `Configuration` (faltan la dirección o las credenciales,
o la dirección responde 404) y `Network` (red o 5xx), la única que `retryable()` marca para reintentar. La
sesión es de una dirección y un usuario; las peticiones que llegan mientras se inicia esperan a ese mismo
login en vez de lanzar otro, que cambiaría la cookie a las demás.

`tests/fixtures/gesreq` guarda copias **anonimizadas** de las cinco páginas (bandeja, ficha, ficha vacía,
ficha inexistente y login), con el marcado real y datos inventados: el repositorio es público. Si GESREQ
cambia, se guarda la página nueva sin datos reales en su fixture y se ajusta el extractor.
`test_gesreq_client` incluye `readsTheInboxOfARealGesreq`, que sólo se ejecuta contra un GESREQ de verdad
con `QAFLOW_GESREQ_URL`, `QAFLOW_GESREQ_USER` y `QAFLOW_GESREQ_PASSWORD`.

## Tests

`tests/` construye **un ejecutable por clase bajo prueba**, enlazado con la biblioteca de su capa:

```
tests/
├── support/
│   ├── MemoryRepositories.h   repositorios, ajustes y llavero en memoria (con `failWrites` para simular fallos de disco)
│   ├── FakeIssueTracker.h     IIssueTracker con modos Succeed / RejectContent / NetworkDown
│   ├── FakeScreenRecorder.h   IScreenRecorder que no graba: start/stop simulados y fichero mínimo
│   ├── FakeHttpServer.h       servidor HTTP mínimo en localhost que guarda las peticiones y responde lo que se le diga
│   └── AppFixture.h           toda la capa de aplicación ya cargada con los datos de ejemplo
├── core/                      modelos y funciones puras
│   ├── test_test_case.cpp     LastRun, readyToBeMarkedListo, searchText, parseTags, enums
│   ├── test_test_run.cpp      veredicto, saltos N/A, cronómetros, formatDuration, enums
│   ├── test_bug_report.cpp    validación y descripciones Jira / Markdown / HTML
│   ├── test_settings.cpp      TrackerSettings (URLs de issue por gestor) y CaptureSettings
│   ├── test_plan_report.cpp   conteos por caso, pendientes, veredicto, Markdown
│   ├── test_case_filter.cpp   búsqueda libre y filtros por campo
│   └── test_case_formats.cpp  JSON, CSV y Markdown (ida y vuelta, errores)
└── application/               stores y controlador sobre repositorios en memoria
    ├── test_test_case_store.cpp    carga, alta, duplicar, fusión, pasos, borrar y deshacer
    ├── test_run_controller.cpp     flujo, correcciones, archivado, cola del plan, sesión
    ├── test_run_history_store.cpp  ids, informes, cierre de planes, persistencia
    ├── test_plan_store.cpp         colección, orden, ciclos, estimación
    ├── test_settings_store.cpp     token en el llavero, migración, un token por gestor
    ├── test_bug_store.cpp          issues por caso, estados, cola de pendientes
    ├── test_bug_report_service.cpp borrador, envío, cola offline, reintentos, estados, metadatos
    ├── test_requirement_source_service.cpp conexión con GESREQ: ajustes usados, `connected`, sistemas de la bandeja
    └── test_evidence_service.cpp   captura (formato, cuenta atrás y su cancelación), adjuntos, grabación,
                                    sustitución de la imagen anotada, portapapeles, borrado de ficheros liberados
├── infrastructure/            disco y red reales, en directorios temporales y localhost
│   ├── test_json_repositories.cpp   ida y vuelta de casos, planes (y migración de plan.json), historial, sesión, bugs;
│   │                                ficheros corruptos y directorio sin permisos
│   ├── test_settings_repository.cpp QSettingsRepository (grupo "tracker", migración del grupo "jira"), PlainSettingsSecretStore
│   ├── test_tracker_clients.cpp     JiraClient y GitHubClient contra FakeHttpServer: cabeceras, cuerpo, adjuntos multipart,
│   │                                4xx no reintentable, 5xx y conexión rechazada reintentables, estados, metadatos, TrackerRouter
│   ├── test_gesreq_parser.cpp       extractor de GESREQ sobre fixtures anonimizados: bandeja, ficha, ficha vacía e inexistente, login
│   ├── test_gesreq_client.cpp       sesión de GESREQ contra un servidor falso (login, caducidad, reintento único, errores);
│   │                                opcionalmente, contra uno real
│   └── test_gif_encoder.cpp         cuantización (exacta y median cut) y GIF animado leído de vuelta con el plugin de Qt
└── presentation/              ventana completa con plataforma offscreen
    ├── test_main_window.cpp   navegación, ventana de ajustes, atajos del menú, Ctrl+F y filtro, teclas de veredicto, captura, cuenta atrás,
    │                          grabación, adjuntar por arrastre, abrir el visor, deshacer, métricas y el aviso «Reintentar»
    ├── test_evidence_widgets.cpp renderAnnotations (formas, texto, difuminado), AnnotationEditor, ImageViewer, Thumbnail y ShotCard
    └── test_choice_dialog.cpp  selector con búsqueda: carga, filtro sin tildes, flechas, error y reintento, respuestas tardías
```

`core/test_metrics.cpp` cubre `metrics::` (por suite, ciclos, tendencia); `core/test_issue.cpp`, el modelo de issues;
`application/test_issue_store.cpp`, el store (asociaciones, importación, ausencias, resultados, datos ilegibles) e
`infrastructure/test_issue_repository.cpp`, `issues.json`;
`application/test_issue_publish_service.cpp`, la publicación en el gestor (borrador, creación, vinculación,
actualización pendiente y envíos sin confirmar).

Cada fichero es una clase QtTest con los slots agrupados por tema (`// ---- …`). Los tests se
registran como `<capa>/<nombre>` y llevan la capa como etiqueta:

```
cmake -S . -B build && cmake --build build
ctest --test-dir build                 # todo
ctest --test-dir build -L core         # sólo modelos
ctest --test-dir build -R run_controller --output-on-failure
```

## Herramienta de desarrollo: capturas de pantallas

```
QT_QPA_PLATFORM=offscreen QAFLOW_SNAPSHOT_DIR=/tmp/qaflow-shots ./build/qaflow
```

Renderiza cada pantalla (incluida una ejecución con un paso fallido, el bug prellenado, el
informe de un plan, el panel de métricas con dos ciclos y la ventana de ajustes) a PNG y cierra. Usa un directorio de
datos, unos ajustes y una carpeta de capturas aislados (`$QAFLOW_SNAPSHOT_DIR/data`, `/config` y
`/capturas`) para no tocar los reales; el caso que se ejecuta lleva dos evidencias de ejemplo
dibujadas al vuelo.
`QAFLOW_SNAPSHOT_LANG=es|en` y `QAFLOW_SNAPSHOT_THEME=dark|light` eligen idioma y tema; sin
`LANG` se usa el del sistema. Útil para revisar el diseño sin interacción; CI lo ejecuta como humo.

## Empaquetado

`packaging/CMakeLists.txt` define la instalación (`install(TARGETS)`, `.desktop`, icono y
metainfo en Linux) y CPack: `.deb` + `.tar.gz` en Linux, NSIS + `.zip` en Windows (con
`windeployqt` en la instalación) y `.dmg` en macOS (`macdeployqt`). `packaging/linux/build-appimage.sh`
instala en un AppDir y llama a linuxdeploy con su plugin de Qt. El workflow de GitHub Actions
(`.github/workflows/ci.yml`) compila y pasa los tests en los tres sistemas y sube los paquetes.
