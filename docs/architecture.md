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
│   │                Metrics (tasa por suite, evolución entre ciclos), CaseFilter, CaseFormats (JSON / CSV / Markdown)
│   └── services/    ITestCaseRepository, IRunHistoryRepository, IRunSessionRepository, IBugRepository,
│                    ISettingsRepository, ISecretStore, IScreenCapture, IScreenRecorder, IGlobalHotkey, IIssueTracker
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
│   ├── SeedData           datos de ejemplo del primer arranque
│   └── AppContext         agrupa los servicios ya construidos para la presentación
├── infrastructure/  Implementaciones concretas de las interfaces de core.
│   ├── persistence/ JsonTestCaseRepository (cases.json, plans.json), JsonRunHistoryRepository
│   │                (history.json), JsonRunSessionRepository (session.json), JsonBugRepository (bugs.json),
│   │                QSettingsRepository (tracker, captura y app; sin token)
│   ├── capture/     ScreenCaptureService (QScreen::grabWindow o PortalScreenshot en Wayland), RegionSelector
│   │                (overlay), GifRecorder + GifEncoder (grabación a GIF), RecorderOverlay (control flotante)
│   ├── hotkey/      GlobalHotkey (RegisterHotKey / XGrabKey / Carbon), PortalShortcuts (portal de Wayland)
│   ├── secrets/     SecretStores: secret-tool (Linux), Keychain (macOS), DPAPI (Windows), fichero en claro
│   ├── http/        HttpClient: base REST (JSON, multipart, errores, reintentables) de tracker/ y testmgmt/
│   ├── testmgmt/    ZephyrClient: ciclos, ejecuciones y evidencias en Zephyr for Jira
│   └── tracker/     HttpTrackerClient (base) → JiraClient, GitHubClient, GitLabClient, AzureDevOpsClient;
│                    TrackerRouter despacha por TrackerSettings::kind
└── presentation/    Widgets Qt. Depende de application; nunca de infrastructure.
    ├── theme/       Paletas oscura y clara (Theme.h); resources/styles/app.qss usa tokens (@bg, @tint(green,30))
    ├── widgets/     Piezas reutilizables: Ui (fábricas, icono), Icons (glifos del rail), LayoutButton, FlowLayout, Toast,
    │                FlashOverlay, ProgressCells, MetricBars (RateBar, TrendChart), Thumbnail, TextArea, ShotCard,
    │                EvidencePreview (visor de la ejecución), ImageViewer (visor a tamaño completo),
    │                AnnotationEditor (anotaciones), EvidenceActions (acciones compartidas)
    ├── views/       Una clase por pantalla: CasesView, PlanView, RunView, HistoryView, BugView,
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
| Bugs y cola offline    | `$XDG_DATA_HOME/QAflow/QAflow/bugs.json`                    |
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
bloque «Plan» de la barra de estado como progreso. Archivar un plan sólo lo oculta y bloquea
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
el filtro «Casos»). `MainWindow::finishRun()` es la acción «Finalizar»: continúa con el siguiente
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
ejecutadas, la clave del Test desde `TestCase::testKey` y las evidencias del caso que sigan en disco
con el paso al que se asignaron. El cliente encadena entonces, por cada caso:

| Paso | Petición |
|------|----------|
| Resolver los ids | `GET /rest/api/2/project/{clave}` (Zephyr trabaja con ids numéricos, no con claves) y `GET /rest/api/2/issue/{testKey}?fields=id` |
| Crear el ciclo | `POST {api}/cycle` con `projectId`, `versionId` y las fechas en el formato de Zephyr (`12/May/26`) |
| Añadir el caso | `POST {api}/execution` → la respuesta viene indexada por el id de la ejecución creada |
| Veredicto del caso | `PUT {api}/execution/{id}/execute` con 1 PASS · 2 FAIL · 4 BLOCKED |
| Veredicto por paso | `GET {api}/stepResult?executionId=` y `PUT {api}/stepResult/{id}` (N/A queda sin ejecutar, -1) |
| Evidencias | `POST {api}/attachment?entityId=&entityType=` — `TESTSTEPRESULT` las de un paso, `EXECUTION` las demás |

El fallo de un caso no aborta el ciclo: se anota en `PublishResult::skipped` con su motivo y se sigue
con el siguiente, que es lo que interesa cuando se publican decenas. Sí abortan los fallos previos
(proyecto, versión inexistente o creación del ciclo), porque sin ellos no hay dónde publicar. Ahí
entra también lo que se queda fuera sin ser un error de red: un caso sin clave de Test, una evidencia
que ya no está en disco y los veredictos que sobran cuando el Test de Zephyr tiene menos pasos que el
caso de QAflow — los dos se editan por separado y se desincronizan. El informe los enseña al terminar
con su motivo, uno por línea, porque un contador de «3 sin publicar» no dice qué hay que arreglar.

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
    └── test_evidence_service.cpp   captura (formato, cuenta atrás y su cancelación), adjuntos, grabación,
                                    sustitución de la imagen anotada, portapapeles, borrado de ficheros liberados
├── infrastructure/            disco y red reales, en directorios temporales y localhost
│   ├── test_json_repositories.cpp   ida y vuelta de casos, planes (y migración de plan.json), historial, sesión, bugs;
│   │                                ficheros corruptos y directorio sin permisos
│   ├── test_settings_repository.cpp QSettingsRepository (grupo "tracker", migración del grupo "jira"), PlainSettingsSecretStore
│   ├── test_tracker_clients.cpp     JiraClient y GitHubClient contra FakeHttpServer: cabeceras, cuerpo, adjuntos multipart,
│   │                                4xx no reintentable, 5xx y conexión rechazada reintentables, estados, metadatos, TrackerRouter
│   └── test_gif_encoder.cpp         cuantización (exacta y median cut) y GIF animado leído de vuelta con el plugin de Qt
└── presentation/              ventana completa con plataforma offscreen
    ├── test_main_window.cpp   navegación, ventana de ajustes, atajos del menú, Ctrl+F y filtro, teclas de veredicto, captura, cuenta atrás,
    │                          grabación, adjuntar por arrastre, abrir el visor, deshacer, métricas y el aviso «Reintentar»
    └── test_evidence_widgets.cpp renderAnnotations (formas, texto, difuminado), AnnotationEditor, ImageViewer, Thumbnail y ShotCard
```

`core/test_metrics.cpp` cubre `metrics::` (por suite, ciclos, tendencia).

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
