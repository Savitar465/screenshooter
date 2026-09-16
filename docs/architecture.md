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
│   │                Requirement (requerimientos de GESREQ: fila de la bandeja, ficha, registro del resultado y motivos de fallo),
│   │                Issue (issue de QA, lo importado del requerimiento, cambios entre lecturas, revisiones, filtro),
│   │                IssueProgress (cómo va el control de calidad y qué resultado se propone),
│   │                QualityRecord + QualityRecordDraft (el acta R-213 como datos y el borrador que se propone)
│   └── services/    ITestCaseRepository, IRunHistoryRepository, IRunSessionRepository, IBugRepository,
│                    ISettingsRepository, ISecretStore, IScreenCapture, IScreenRecorder, IGlobalHotkey, IIssueTracker,
│                    ITestManagement, IRequirementSource, IIssueRepository, IQualityRecordWriter
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
│   ├── IssueStore         issues de QA: los planes que prueban cada requerimiento (y los casos y ciclos que
│   │                      salen de ellos), importación de GESREQ sin duplicados
│   ├── IssuePublishService publicación del issue en el gestor: crear, vincular, actualizar, estado y
│   │                      el resultado de la revisión (comentario con el acta adjunta)
│   ├── QualityRecordService el acta de la revisión: la propone con el ciclo de plan que se elija, la escribe
│   │                      y la guarda en el issue
│   ├── RevisionPublishService publicar el resultado de una revisión: los ciclos en Zephyr, el resultado y el
│   │                      acta en el gestor y el registro en GESREQ
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
│   ├── report/      ZipWriter (ZIP mínimo), DocxWriter (OOXML) y QualityRecordDocx (la maqueta del R-213)
│   ├── requirements/ GesreqClient (sesión, lectura y registro del control en GESREQ) y GesreqParser (sus páginas HTML → modelos)
│   └── tracker/     HttpTrackerClient (base) → JiraClient, GitHubClient, GitLabClient, AzureDevOpsClient;
│                    TrackerRouter despacha por TrackerSettings::kind
└── presentation/    Widgets Qt. Depende de application; nunca de infrastructure.
    ├── theme/       Paletas oscura y clara (Theme.h); resources/styles/app.qss usa tokens (@bg, @tint(green,30))
    ├── widgets/     Piezas reutilizables: Ui (fábricas, icono), Icons (glifos del rail), LayoutButton, FlowLayout, Toast,
    │                FlashOverlay, ProgressCells, MetricBars (RateBar, TrendChart), Thumbnail, TextArea, ShotCard,
    │                EvidencePreview (visor de la ejecución), ImageViewer (visor a tamaño completo),
    │                AnnotationEditor (anotaciones), EvidenceActions (acciones compartidas)
    ├── views/       Una clase por pantalla: IssuesView (+ RequirementImportDialog, JiraPublishDialog,
    │                QualityRecordDialog, RevisionPublishDialog), CasesView, PlanView, RunView, HistoryView, BugView,
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

### Camino de vuelta

Sobre el rail hay una segunda navegación, la de profundidad, al estilo de las aplicaciones del móvil:
`MainWindow` distingue **ir a la raíz de una sección** de **entrar en algo desde donde se está**.

* `navigate(Screen)` es la raíz: lo que hacen el rail, el menú Ver y los atajos `Ctrl+1…6`. Vacía el
  camino de vuelta, así que el botón «atrás» desaparece y nunca promete una vuelta que ya no tiene
  sentido.
* `navigateInto(Screen)` entra en profundidad: lo que hacen las acciones de las tarjetas —crear un
  caso desde un plan, «Ir al plan» desde un issue, abrir el informe de un ciclo, reportar un bug
  desde la ejecución—. Apila de qué pantalla se vino **con el nombre que tenía en ese momento**
  (`screenLabel()`: el nombre del plan, el id del issue), no el que tenga al volver.
* `goBack()` deshace un paso. Lo disparan el botón `navbarBack` de la barra superior (que dice a
  dónde vuelve: «‹ Suite de regresión»), la acción `actBack` del menú Ver con `Alt+←` y el botón
  «atrás» del ratón.

Entrar en una pantalla por la que ya se pasó no alarga el camino: lo **deshace hasta ella**, para que
ir y venir entre un issue y su plan no deje una pila que cueste diez clics vaciar. `showScreen()` es
lo que realmente cambia la pila de widgets y refresca el botón; las tres funciones pasan por ella.

### Fin de un ciclo de plan

`MainWindow::finishRun()` decide a dónde lleva terminar el último caso de un ciclo. Si el plan prueba
un requerimiento (`issueOfPlanRun()` lo resuelve por `IssueStore::issuesForPlan()`, y entre varios
gana el issue seleccionado), el ciclo **termina en su issue**: es donde está el paso siguiente del
control de calidad —levantar el acta— y donde se ven los resultados que acaba de dar. El informe del
ciclo no se pierde: el aviso del resumen lleva un botón «Ver informe» que lo abre en el historial,
ya con vuelta al issue. Un ciclo que no prueba ningún requerimiento sigue terminando en su informe,
como antes.

## Menú, atajos y bandeja

`MainWindow::buildMenus()` crea el menú (Archivo, Editar, Ver, Ejecución, Ayuda) con `QAction`
y los atajos estándar (`QKeySequence::New`, `Find`, `Undo`, `Quit`, F5, Ctrl+1…5). Los atajos de
captura, de grabación y de la ejecución («Pasa y siguiente», «Falla y siguiente», «Paso anterior»,
«Paso siguiente»)
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
| Issues (sus planes, lo importado de GESREQ, sus cambios, la publicación en el gestor y las revisiones con su ciclo y su acta) | `issues.json` en el directorio de datos de cada proyecto |
| Bugs (el issue del gestor, su caso y el paso del que salió, su clasificación y su estado) | `bugs.json` en el directorio de datos de cada proyecto |
| Actas generadas (.docx)  | donde las guarde el usuario; el issue recuerda la ruta de cada revisión |
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

La pantalla del plan es donde se compone y donde se prueba: **«▶ Ejecutar plan»** arranca su ciclo
(`PlanView::runPlanRequested` → `MainWindow::startPlanRun`, que avisa si hay una ejecución o una captura
en curso y lleva a la pantalla de ejecución) y **«+ Nuevo caso»** crea un caso, lo añade al final del
plan y lo abre para escribir sus pasos. Son los dos pasos que la pantalla de issues manda hacer aquí,
así que están donde se llega desde ella.

Un **ciclo** es una ejecución del plan: `RunController::startSequence()` abre un `PlanRun` en el
historial con el `planId` del plan.

**Cada ciclo dice de qué ronda es y dónde se probó.** Un issue prueba varios planes y vuelve a
probarlos en cada revisión, así que el ciclo no se identifica por su plan y su fecha: el `PlanRun`
guarda además `issueId` y `revision` —el requerimiento cuyo control de calidad se está haciendo y la
ronda que estaba abierta al arrancar— y `environment`, el ambiente en el que se prueba. El ambiente se
pregunta al arrancar (`CycleStartDialog`, con el último usado en el proyecto ya propuesto y el issue y
la revisión a los que va a pertenecer a la vista); el issue y la revisión los pone `ProjectSession` al
recibir `RunController::planStarted`, con lo que devuelve `IssueStore::notePlanStarted()`. Los tres
viajan a Zephyr con el ciclo (ver «Publicar en Zephyr») y se ven en el historial y en la pantalla del
issue. Los ciclos anteriores a esto no los tienen: `IssueStore::cyclesOfRevision()` los reparte por la
ventana de fechas de cada ronda, como se hacía antes. `PlanStore::latestCycle(planId)` devuelve el `PlanReport` del
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

`RunState` guarda el caso, el índice del paso en pantalla, un `StepRecord` **por paso del caso**
(resultado, nota, segundos que estuvo en pantalla y si ya está `marked`) y el cronómetro del paso
actual. Resultados: `Pass`, `Fail`, `Block` y `Skip` (N/A: no cuenta para el veredicto). Ninguno
corta la ejecución: un bloqueo se queda en su paso y se sigue navegando por el resto.

* **Los pasos se recorren en cualquier orden.** `goTo(i)` pone en pantalla el paso `i` —marcado o
  no— y `back()`/`next()` se mueven de uno en uno; en la vista se llega por la lista de la columna
  izquierda (cada tarjeta es pinchable), por los botones «← Anterior / Siguiente →», con Retroceso
  y Alt+←/Alt+→ o con los atajos globales configurables. Navegar no toca ningún veredicto y reabre
  una ejecución ya terminada. Cada paso conserva su nota y su cronómetro entre visitas.
* `mark()` da veredicto al paso en pantalla (o lo cambia) y salta al siguiente pendiente, volviendo
  a los huecos de atrás cuando no queda nada por delante; `setResult(i, r)` corrige un veredicto
  desde la lista sin moverse de sitio. La ejecución pasa a "terminada" cuando **todos** los pasos
  están marcados. Nada se archiva hasta que se cierra, así que estas correcciones no dejan rastro
  en el historial.
* **Persistencia de la sesión.** Tras cada cambio (`changed()`) el controlador guarda un
  `RunSession` (estado, cola del plan, id del plan) mediante `IRunSessionRepository`. Al arrancar,
  `load()` la restaura si el caso sigue existiendo, ajusta la lista de resultados si el caso ganó o
  perdió pasos y
  reinicia el cronómetro del paso actual: el tiempo con la aplicación cerrada no cuenta, pero lo
  acumulado antes (`stepElapsedSecs`) sí. `MainWindow` abre directamente la pantalla de ejecución.
* **Duración real.** Cada `StepRecord` mide su tiempo; `RunRecord::durationSecs` es la suma. La
  vista muestra un reloj por paso y por caso (un `QTimer` de un segundo sólo actualiza etiquetas).
* **La pantalla, en tres columnas.** `RunView` se construye con una función por columna:
  `buildCasePanel()` (estado del caso, progreso, la lista de pasos con su veredicto —la pastilla de
  cada paso ya marcado abre el menú para corregirlo, la clave del bug que se reportó en él, y un clic
  en la tarjeta va a ese paso—, los
  botones de navegación y «Cerrar ejecución»), `buildStepPanel()` (el paso en pantalla, sus
  veredictos, «Reportar bug» —que sale en cualquier momento de la ejecución, no sólo al final—, el
  visor grande de la evidencia elegida con la barra «Asignar a» y las observaciones del paso) y `buildFilmPanel()` (la columna «Capturas», con todas las evidencias del caso).
  El visor es `EvidencePreview`, que dibuja la imagen ajustada al hueco con la etiqueta del paso y
  el nombre del fichero; las tarjetas de la columna de capturas son `ShotCard` con `Layout::Film`,
  que en vez de abrir el visor a tamaño completo emiten `selectRequested` para elegir qué se ve en
  grande. Una captura nueva se abre sola y la lista se desplaza hasta ella.
* **Estimación del plan.** `PlanStore::estimatedSecs()` usa la media real por paso de cada caso
  según su historial; para los casos sin historial, la media global; sin datos, 3 min por paso.
  `estimateBasis()` explica en la vista de qué datos sale.

## Historial de ejecuciones e informes de plan

Cada ejecución que se cierra se convierte en un `RunRecord`: instantánea del texto de los pasos,
resultado y nota de cada uno, veredicto, inicio y fin. `RunController::commitRun()` la archiva en
`RunHistoryStore` y actualiza la "última ejecución" del caso (`Passed`, `Failed` o `Blocked`).
«Cerrar ejecución» archiva también una ejecución a medias —lo normal cuando un paso bloquea y no
tiene sentido seguir—: se guardan los pasos hasta el último marcado y los huecos quedan como N/A,
con `plannedSteps` diciendo cuántos tenía el caso ("2 de 5"). Arrancar otro caso, repetir o
abandonar sólo archivan si la ejecución ya estaba terminada, para que nada se pierda sin que se
cuele media prueba en el historial.

Al iniciar un plan se abre un `PlanRun` (nombre, casos en orden, inicio) y todos los `RunRecord`
que genera llevan su `planRunId`. Cuando la cola se vacía (o se abandona) el plan se cierra y
`RunController` emite `planCompleted(id)`. El `PlanReport` no se persiste: `PlanReport::build()`
lo calcula a partir del `PlanRun` y sus registros (si un caso se repitió dentro del plan cuenta la
última ejecución; los casos que quedaron sin ejecutar aparecen como pendientes). `toMarkdown()`
produce el informe exportable; la vista sólo abre el diálogo de guardado o copia al portapapeles.

### Los bugs del ciclo

El informe trae también **los bugs que se reportaron mientras corría**. Un `IssueLink` no guarda de
qué ciclo salió —sólo su caso y el paso—, así que la pertenencia se deduce: `PlanReport::reportedDuring()`
acepta el bug si su fecha cae entre el arranque del ciclo y su cierre **más una hora**, porque el parte
se escribe justo después de ver el fallo, cuando la ejecución ya se ha archivado; un ciclo en curso
admite todo lo posterior a su arranque. `PlanReport::build()` recibe el libro de bugs entero y reparte
los que cuadran en `PlanReportRow::bugs`, caso por caso.

Quien pasa ese libro es `RunHistoryStore::setBugs()` (el `BugStore` se crea después que el historial,
así que se inyecta desde `ProjectSession`), de modo que **todos** los informes lo traen: la pantalla,
el Markdown, el acta y la publicación en Zephyr. Sin él —tests que no miran bugs— el informe sale
igual, sólo que sin ellos. `TestPublishService::defectsOf()` usa la misma `reportedDuring()`, así que
lo que el informe enseña es exactamente lo que se sube como defectos del ciclo.

`HistoryView` lista planes y ejecuciones sueltas (las de un plan se ven dentro de su informe, o con
el filtro «Casos»). Un informe se puede eliminar desde su cabecera: `RunHistoryStore::removePlanRun()`
quita el `PlanRun` y sus `RunRecord`, suelta las evidencias de esas ejecuciones
(`TestCaseStore::releaseShotsOfRuns()`, que emite `filesReleased` para que `EvidenceService` borre
los ficheros) y, a los casos cuya «última ejecución» era una de las borradas, les deja la más
reciente que quede. Es definitivo, sin deshacer, y la vista no lo ofrece para el ciclo en curso
(`RunController::planRunId()`), que sigue recibiendo ejecuciones. El informe enseña los bugs dos
veces: juntos en su propia tarjeta («Bugs encontrados · N», con cuántos siguen abiertos) y otra vez
dentro de la tarjeta de su caso, junto a los pasos donde se vieron. `MainWindow::finishRun()` es la
acción «Finalizar»: continúa con el siguiente caso del plan, lleva al issue que se está probando o
abre el informe cuando el ciclo no prueba ningún requerimiento (ver «Fin de un ciclo de plan»).

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

**Cada bug es de un paso.** `BugReport::linkedStep` (1..N; 0 = el caso entero) viaja hasta
`IssueLink::step` y de ahí a `bugs.json`. Con él, publicar la ejecución cuelga el defecto **del
resultado de ese paso** en Zephyr (`TestPublishService` → `PublishDefect`), el informe del ciclo y el
acta dicen de qué paso salió cada observación, y la lista de pasos de la ejecución enseña la clave del
bug en la tarjeta del suyo (`RunView` escucha `BugStore::bugsChanged`). El formulario lo trae puesto y
lo deja cambiar en el combo «Paso», junto a «Caso vinculado».

**El borrador.** `BugReportService::draftFromCurrentContext(stepIndex)` prellena el parte con el caso
seleccionado y un paso de la ejecución: el que pida quien abre el parte —«Reportar bug» de la pantalla
de ejecución manda el paso que se tiene delante— o, si no dice ninguno, el fallo o bloqueo más cercano
(`RunState::reportableStepIndex()`). Si el paso está **bloqueado**, el borrador sale con severidad
«Bloqueante» (y por tanto prioridad `Highest` en Jira) y el título habla de bloqueo en vez de falla.
Como un fallo o un bloqueo ya no cortan la ejecución, el parte se levanta en cuanto se ve el problema
y se sigue probando el resto del caso.

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

**Enlazar issues entre sí.** `canLinkIssues()` y `linkIssues()` son opcionales y sólo Jira los
implementa: `POST /rest/api/2/issueLink` con el tipo que ofrezca el servidor —cada instancia lo llama a
su manera («Relates», «Relacionada con»…), así que se leen los tipos (`GET /rest/api/2/issueLinkType`),
se elige el que relaciona y se recuerda para esa instancia—. Con eso, al publicar el resultado de una
revisión, del issue del requerimiento cuelgan sus bugs y los Tests de Zephyr de sus ejecuciones: desde el
issue se llega a todo lo que se probó. Un enlace que falle no tumba la publicación, se dice cuál.

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
| Crear el ciclo | `POST {api}/cycle` con `projectId`, `versionId`, el `environment` del ciclo y las fechas en el formato de Zephyr (`12/May/26`) |
| Crear el Test que falta | `POST /rest/api/2/issue` (tipo Test, título y precondiciones del caso) y un `POST {api}/teststep/{issueId}` por paso |
| Añadir el caso | `POST {api}/execution` → la respuesta viene indexada por el id de la ejecución creada |
| Veredicto del caso | `PUT {api}/execution/{id}/execute` con 1 PASS · 2 FAIL · 4 BLOCKED |
| Veredicto por paso | `GET {api}/stepResult?executionId=` y `PUT {api}/stepResult/{id}` (N/A queda sin ejecutar, -1) |
| Evidencias | `POST {api}/attachment?entityId=&entityType=` — `TESTSTEPRESULT` las de un paso, `EXECUTION` las demás |

**El nombre del ciclo dice de qué control de calidad es.** `TestPublishService::cycleName()` lo arma
con lo que el `PlanRun` sabe: el requerimiento (`GREQ 2026997`, resuelto por `issueId` contra el
`IssueStore` que le pasa `setIssues()`), la revisión (`Rev. 2`), el nombre del plan —lo que distingue
entre sí los ciclos de una misma ronda—, la fecha y el ambiente:
`GREQ 2026997 · Rev. 2 · Regresión · 12/05/2026 · QA`. Lo que el ciclo no diga no sale, así que una
ejecución suelta se queda con el plan y la fecha de siempre y los ciclos publicados antes de que esto
existiera conservan su nombre (y con él su enlace). Lo mismo va en la descripción del ciclo
(requerimiento, revisión y ambiente) y el ambiente, además, en el campo `environment` de Zephyr.

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
| Del issue | id local (`IS-0001`, independiente de Jira), título, notas, prioridad, estado de QA, `planIds` | QAflow |
| `RequirementLink` | conexión, la fila de la bandeja (`ExternalRequirement`), la ficha si se consultó, fechas de importación y lectura, `missing` y `changes` | cada consulta a GESREQ |
| Representación en Jira | `jiraKey`, `jiraUrl` (el filtro «Jira» ya los usa; la publicación llega después) | la publicación |

El **estado de QA** (Pendiente, En preparación, En pruebas, Finalizado) es de QAflow y no se deduce del
estado de GESREQ, del de Jira ni del resultado de las pruebas. Sí avanza solo con el trabajo, y nunca
hacia atrás: vincular el primer plan pasa un issue Pendiente a «En preparación», y arrancar un ciclo de
uno de sus planes lo pone «En pruebas» (`RunController::planStarted` → `IssueStore::notePlanStarted`,
conectados en `ProjectSession`). Cerrar la revisión lo deja Finalizado. La prioridad y el título salen del
requerimiento al importarlo y a partir de ahí son de QAflow.

**Lo que prueba un requerimiento son planes, no casos sueltos.** El issue se asocia sólo a `planIds`; sus
casos son los de esos planes (`IssueStore::caseIdsOf`, en el orden de los planes y sin repetir) y sus
resultados, los de los **ciclos** de esos planes (`cyclesOf`, `runsOf`). Así lo que la pantalla y el acta
cuentan es del requerimiento y sólo de él: la misma ejecución suelta de un caso, o un ciclo de otro plan que
reutiliza ese caso, no es un resultado suyo. Un plan puede agrupar las pruebas de varios issues
(`issuesForPlan`), la asociación vive en el issue —`cases.json` y `plans.json` no cambian— y los ids que ya
no existen se enseñan como tales, sin borrarlos.

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
estado, prioridad, publicación en Jira) y el issue a la derecha, en el orden en que se trabaja: el
requerimiento (cambios, ausencia, datos y ficha), la **revisión**, el **plan de pruebas** con sus casos
dentro, los **resultados** —los ciclos de sus planes, el más reciente primero, con su veredicto, sus
contadores, si está publicado en Zephyr y las ejecuciones de cada caso—, los **bugs reportados** en esas
ejecuciones (clasificación A–E, clave, de qué caso y paso salieron, estado y si son de la revisión en
curso) y, al final, la publicación en el gestor, que desde que el requerimiento se importa ya está hecha
y es contexto.

La **revisión es el corazón del issue**, así que se enseña como lo que es: una serie de pasos, cada uno
con lo que lleva hecho y su acción. El primero sin terminar es el que toca y se destaca; los hechos se
marcan con un visto y se apagan.

| Paso | Hecho cuando | Su acción |
|------|--------------|-----------|
| 1 · Preparar el plan de pruebas | el issue tiene un plan y el plan, casos | crear el plan o abrirlo |
| 2 · Ejecutar el plan | algún caso se ejecutó en esta revisión | ir al plan, que es donde se arrancan los ciclos |
| 3 · Generar el acta (R-213) | la revisión tiene su .docx | generar (o regenerar) el acta, y abrir la que hay |
| 4 · Cerrar la revisión | la revisión está cerrada con su resultado | cerrarla, eligiendo conforme u observado |
| 5 · Publicar el resultado | se publicó en el gestor o en GESREQ | «Publicar…», sólo con la revisión cerrada |
| 6 · Volver a probar | — | abrir la ronda siguiente (un requerimiento observado vuelve a pruebas) |

No hay tarjeta de casos ni notas de QA: los casos del issue son los del plan y se ven dentro de él, y lo
que hay que contar del control de calidad va en el acta y en el comentario del resultado.

**Iniciar pruebas (entre proyectos).** La bandeja de GESREQ es del usuario, no del proyecto: el diálogo de
importación enseña también los requerimientos de los demás sistemas, cada uno con el proyecto que los
trabaja (`ProjectStore::projectForRequirementSystem`). «Iniciar pruebas» resuelve ese proyecto y:

| Situación | Qué pasa |
|-----------|----------|
| Es el proyecto activo | `IssueStore::openForRequirement()` abre su issue aquí mismo: lo crea la primera vez —y con él, su issue en el gestor y su plan de pruebas (`IssuesView::ensurePlan`), listos para empezar— y luego reutiliza el que hay, con sus planes y lo escrito en QAflow |
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

**Revisiones: el control de calidad de punta a punta.** Un requerimiento se prueba en rondas: se prueba,
se cierra con un resultado y, si queda **observado**, vuelve a pruebas y se abre la ronda siguiente. Cada
ronda es un `IssueRevision` (número, cuándo empezó y cuándo se cerró, resultado, el acta con su fichero, y
qué se hizo con ella en el gestor y en GESREQ), y todas se guardan en el issue: el «Número de Revisión» del
acta es el de la ronda.

| Momento | Qué pasa |
|---------|----------|
| Arranca un ciclo del plan del issue | Se abre la revisión (la primera, o la siguiente si la anterior está cerrada) y el issue pasa a «En pruebas» |
| Durante la ronda | `issueProgress()` (core, función pura) cuenta la **última ejecución de cada caso dentro de la revisión** y los bugs del issue: de ahí salen los contadores y el resultado que se propone |
| Se genera el acta | `QualityRecordService` la arma con el **ciclo de plan** que se elija (`cyclesFor`), la escribe y la guarda en la revisión, con lo escrito en ella y con cuál fue ese ciclo (`IssueRevision::planRunId`) |
| Se cierra la revisión | Queda con su resultado (Conforme u Observado) y el issue, Finalizado. Volver a probar abre la siguiente |
| Se publica el resultado | Con la revisión cerrada aparece «Publicar…»: los ciclos a Zephyr, el resultado y el acta al gestor y el registro a GESREQ (`RevisionPublishService`) |

El **resultado** (`QaOutcome`: Pendiente, Conforme, Observado) es una propuesta hasta que alguien lo
confirma: se propone **Observado** si hay casos fallidos o bloqueados o bugs abiertos, **Conforme** si se
ejecutó todo y no queda ninguno, y Pendiente mientras falte ejecutar. `IssueProgress::blockers` dice por
qué («2 casos sin ejecutar», «1 bug abierto») en vez de dar sólo un veredicto, y la pantalla lo enseña en
la tarjeta «Revisión» junto al paso del flujo, los contadores, el acta y las revisiones ya cerradas.

**Publicación en el gestor.** `IssuePublishService` (application) crea la representación del issue en Jira,
o enlaza una que ya existe, y guarda en `Issue::publication` las tres identidades juntas: el requerimiento
de GESREQ, el issue de QAflow y el issue del gestor (con su instancia, proyecto, tipo, estado y fechas).

El issue importado **nace ya en el gestor y con su plan**: al traer el requerimiento de GESREQ («Iniciar
pruebas») se crea allí su issue con el borrador de siempre (`IssuesView::publishImported`) y se deja listo
el plan con el que se prueba (`ensurePlan`, que no crea otro si ya tiene uno), para que los casos, los
bugs y el resultado tengan dónde colgarse desde el principio. Si el gestor no está configurado, si el
envío falla o si quedó uno **sin confirmar**, no se insiste solo: el issue se queda sin publicar y se
publica a mano desde su tarjeta, que es donde además se puede vincular uno que ya existe.

| Acción | Qué hace |
|--------|----------|
| Publicar | `draftFor()` arma título y descripción (lo importado de GESREQ, las notas de QA y de qué issue salió) y el diálogo los enseña para corregirlos antes de enviar; se crea con las etiquetas `qaflow`, el id del issue y `GREQ-<número>` |
| Vincular | `fetchIssue()` comprueba que la clave existe y la guarda como `linked`: lo escribió otra persona, así que QAflow no ofrece sobrescribirlo |
| Actualizar | `needsUpdate()` compara lo de ahora con `publishedTitle`/`publishedDescription` (lo último que salió de QAflow) y avisa; sólo esta acción reescribe el título y la descripción en el gestor, diciendo antes que lo editado allí se pierde |
| Estado | `refreshStatus()` guarda el estado del gestor, que se enseña aparte del estado de QA |
| Resultado | `publishResult()` comenta en el issue cómo quedó la revisión (resumen de `quality::summaryOf`) y le adjunta el acta, con `IIssueTracker::commentIssue()` — opcional, sólo Jira (`POST /rest/api/2/issue/{clave}/comment` y los adjuntos del issue). Lo llama la publicación de la revisión, que enseña el texto antes de enviarlo |

Nada se publica ni se sobrescribe solo. Si un envío se corta sin respuesta, el issue queda marcado como
**sin confirmar** (`publication.uncertain`): puede haberse creado igualmente, así que la pantalla dice cómo
buscarlo por su etiqueta y publicar otra vez pide confirmación expresa. Un rechazo del contenido (un tipo de
incidencia que no existe, por ejemplo) no deja esa duda y no marca nada.

**Publicar el resultado de una revisión.** Cuando la revisión se cierra, la tarjeta «Revisión» ofrece
«Publicar…» (`RevisionPublishDialog` sobre `RevisionPublishService`): una sola pantalla con los tres
destinos, lo que iría a cada uno y lo que ya se hizo, para elegir y ver cómo termina cada paso.

| Paso | Qué manda | Cuándo se puede |
|------|-----------|-----------------|
| Zephyr | Los ciclos de los planes del issue, con sus casos, pasos, evidencias y **defectos** —cada bug va en la ejecución y en el resultado del paso del que salió (`IssueLink::step`, el paso del que se levantó el parte)— (`TestPublishService`); los ya publicados se actualizan en vez de duplicarse | Zephyr activado en Ajustes y algún caso ejecutado en esos ciclos |
| El gestor | Un comentario en el issue con el resumen de la revisión, los enlaces de los ciclos de Zephyr y el acta adjunta; y del issue se **cuelgan sus pruebas**: los bugs de la revisión y los Tests de Zephyr de sus ejecuciones, enlazados con `IIssueTracker::linkIssues()` (`RevisionPublishService::linkEvidence`) | El issue está en el gestor (lo está desde que se importó) y el gestor sabe comentar |
| GESREQ | El registro del control de calidad: resultado, comentario, las cinco cifras A–E del acta y el acta adjunta. Al guardar, el sistema dice con qué **estado** queda el requerimiento y el issue se actualiza con él (`IssueStore::noteRequirementState`), sin volver a leer la bandeja | El issue viene de GESREQ, el conector sabe registrar, **el sistema aceptaría el registro** y el control **no se registró ya** |

El envío se escribe **byte a byte como el de un navegador** (`HttpClient::formData`): delimitador sin
comillas, cada campo con sólo su `Content-Disposition` y el acta en el sitio que ocupa en el formulario
(`ControlForm::filePosition`). El multipart que compone `QHttpMultiPart` entrecomilla el delimitador y
etiqueta cada campo con un `Content-Type`, y el servidor de GESREQ (Struts sobre WebLogic) respondía a
eso con un 500 sin llegar a leer un solo campo.

Si el servidor falla al guardar (un HTTP 500 de la aplicación), lo que se enseña es lo que dice su
página —`gesreq::serverErrorReason` saca el mensaje, la excepción o la causa raíz del volcado de
Tomcat—, más el tamaño del acta si es grande: «se cortó sin respuesta» sería falso y no diría por dónde
mirar. El registro queda **sin confirmar** igualmente, porque el fallo pudo llegar después de guardar.
El comentario viaja recortado a lo que admite el campo del formulario (`kControlCommentMax`, los mismos
2000 caracteres que limita la propia página en su campo hermano).

Las reglas de GESREQ se preguntan **antes** de enviar nada (`IRequirementSource::registrationProblem`, que
`GesreqClient` responde con las mismas comprobaciones que hace al registrar): sin acta generada, con un
resultado que no es Conforme ni Observado, con un «OK» que lleva observaciones que no son recomendaciones o
con un «OBSERVADO» sin ninguna, el paso aparece bloqueado con el motivo en vez de fallar a mitad del
registro. Cambiar el resultado en el diálogo, o desmarcar el acta, vuelve a preguntarlo.

**Registrar es definitivo y se hace una sola vez por ronda.** Cambia el estado del requerimiento en
GESREQ y lo saca de la bandeja de control, así que el paso queda bloqueado en cuanto la revisión tiene su
registro, y un control cerrado como **Conforme** no se vuelve a registrar nunca: sólo un **Observado**
devuelve el requerimiento a pruebas y deja que la ronda siguiente registre el suyo
(`RevisionPublishService::alreadyRegistered`, que además protege al servicio de un segundo envío aunque
se lo pidan). Publicado el resultado en el gestor, el issue también se pone al día allí
(`refreshStatus`).

Se ejecutan en ese orden —Zephyr primero, para que sus enlaces viajen en el comentario y en el registro— y
un paso que falle no impide los demás: cada uno cuenta su resultado en el diálogo y lo que se cortó sin
respuesta queda **sin confirmar**, como en el resto de la aplicación. Lo que ya se hizo en esta revisión
viene desmarcado: repetirlo es una decisión, no un descuido. Nada se envía hasta pulsar «Publicar».

## Acta de control de calidad (R-213)

Cada revisión termina en el formulario **R-213, «REVISIÓN CONTROL DE CALIDAD DE SOFTWARE»**, que es lo que
la institución espera: un documento de Word con los datos del requerimiento, el resumen de observaciones
por tipo, dónde están los casos y los bugs, y las cuatro características que se revisan. QAflow lo genera.

`QualityRecord` (core) es el acta **como datos**, celda a celda: los generales (GREQ, sistema, módulo,
servidor, base de datos, descripción, quién lo desarrolló, el recurso de QA, el número de revisión y sus
fechas), las cinco filas del resumen (A Funcionamiento/Lógica, B Datos, C Estético/Forma,
D Recomendaciones, E Vulnerabilidades, cada una con sus observaciones y correcciones), los tres detalles
(casos, ejecución y bugs, con las capturas que se quieran pegar) y los resultados con sus observaciones
generales.

`quality::draftFor()` (core, pura) propone el acta con lo que ya hay: el requerimiento y **su ficha de
GESREQ** (de la que salen el alcance, quién lo pidió y con qué prioridad y estado, quién lo desarrolló, el
enlace del módulo —también el repositorio que aparezca en la descripción, sin la parte del merge request—,
el servidor, el esquema y las tablas y funciones afectadas), el **ciclo de plan** que se está documentando
(sus fechas, sus casos con el Test de Zephyr de cada uno, cómo terminó caso a caso y el enlace del ciclo
publicado) y los bugs de la revisión por su clasificación, más, como **correcciones**, las observaciones de
rondas anteriores que ya están cerradas. Lo que GESREQ no tiene (base de datos, usuarios, departamento,
membrete) se hereda del **acta anterior del proyecto**, así que sólo se escribe una vez; lo que nadie
rellena queda como en el formulario (`S/D`, `n/a`). `QualityRecordDialog` lo enseña todo, corregible, antes
de generar, y si la revisión tuvo **varias ejecuciones** deja elegir con cuál se levanta el acta (o con
todas), rehaciéndola al cambiar de una a otra.

| Quién | Qué hace |
|-------|----------|
| `QualityRecordService` (application) | arma el borrador (respetando lo ya escrito en la revisión), escribe el fichero por `IQualityRecordWriter`, lo guarda en la revisión (`documentPath`) y propone el nombre `ControlCalidad_<GREQ>_<marca de tiempo>.docx` |
| `QualityRecordDocx` (infrastructure/report) | la maqueta del R-213: cabecera con el membrete, y las secciones numeradas «1. Generales» (rejilla de diez columnas), «2. Resumen Observaciones» A–E con su total, «3. Detalles de la revisión», «4. Resultados» y «5. Observaciones Generales», con el formato del formulario: títulos en blanco sobre azul (`1F4E79`), etiquetas sobre azul claro (`DEEAF6`) y el cuerpo a 10 pt |
| `DocxWriter` | las piezas de OOXML: párrafos (con su color), tablas con `gridSpan`/`vMerge`, sombreados e imágenes (escaladas al ancho de su celda; una que no se pueda leer se omite en vez de romper el acta), en página carta con los márgenes del formulario |
| `ZipWriter` | el ZIP del .docx, con las entradas **sin comprimir** y su CRC-32: Word y LibreOffice lo leen igual y QAflow no necesita zlib ni API privada de Qt |

No hay plantilla que mantener: cambiar el formulario es cambiar esas tablas. El **membrete** no viaja en el
repositorio (que es público): se elige una vez en el diálogo, se guarda en el acta y se hereda de ahí en
adelante.

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

**Registrar el resultado.** Leer no es todo: al cerrar una revisión, QAflow puede registrar en GESREQ el
resultado del control de calidad con su acta. Es la **única escritura** del conector, así que va aparte en
la interfaz (`IRequirementSource::canRegisterResult()` / `registerResult()`, opcionales) y nunca se lanza
sola: sale del botón «Registrar en GESREQ…» de la pantalla de issues, que avisa de que **cambia el estado
del requerimiento**, enseña resultado, comentario y acta, y pide confirmación. Lo registrado queda en la
revisión (`RevisionRegistration`) y, con ella cerrada, el issue queda Finalizado; si el resultado fue
Observado, volver a probar abre la revisión siguiente. Un envío que se corta sin respuesta queda **sin
confirmar**, como en el gestor: puede haberse registrado igualmente, así que hay que mirarlo en GESREQ
antes de repetirlo. Mientras el conector no lo implemente, el botón lo dice en vez de fallar.

`GesreqClient` recorre para ello las mismas páginas que el usuario, pidiéndole al servidor cada enlace en
vez de componerlo:

| Paso | Petición | Por qué así |
|------|----------|-------------|
| Enlace del registro | `GET calidadreg.do` → el «Registrar» de esa fila | lleva el `estado` con el que el requerimiento figura en la bandeja; **sin ese `estado` la pantalla de gestión se abre sin el formulario** |
| Sistema a registrar | `GET calidadregGestionRequerimiento.do?id=…&accion=calidadreg&estado=…` | sólo muestra la gestión, no cambia nada; trae un control por sistema del requerimiento, y se elige el del `systemCode` del issue |
| Formulario | `GET calidadregFuncionalForm.do?gestion=…&idItem=…&corr=…&sis_cod=…` | la ventana con los campos y sus valores actuales |
| Envío | `POST calidadregGestionControlGuardar.do` (multipart) | **no es el `action` del formulario**: su propio script lo manda por AJAX ahí, y la respuesta es el estado de `Anb.form.ajax` (`OK`, `UPDATE`… = guardado) |

Del formulario viaja lo que puso el servidor (`gestion`, `corr`, `id`, `tip_control`, `cod_asignado`,
`fecha_ini`, las correcciones…) y QAflow sólo cambia lo suyo: `resultado_control` (`OK` / `OBSERVADO`), el
comentario y las cinco cifras del resumen de observaciones, que son las clasificaciones **A–E de los bugs**
(funcionamiento, datos, forma, recomendaciones, vulnerabilidades) tal y como las cuenta el acta, más el
acta misma en `arch_funcional`. Se reproduce también lo que el script de la ventana deshabilita antes de
enviar: los campos deshabilitados y la casilla visible de cada corrección, que si no viajaría dos veces.
Las reglas del sistema se comprueban antes de pedir nada (acta obligatoria y con extensión admitida, «OK»
sin observaciones que no sean recomendaciones y «OBSERVADO» con al menos una), para no dejar el resultado a
medias entre los dos sistemas; lo que aun así rechace GESREQ llega como `Rejected` con su motivo.

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
o la dirección responde 404), `Rejected` (GESREQ entendió el registro y no lo aceptó) y `Network` (red o
5xx), la única que `retryable()` marca para reintentar. La
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
│   ├── test_docx_writer.cpp         el ZIP y el .docx que QAflow escribe, y el acta R-213 con lo que dice el modelo
│   └── test_gif_encoder.cpp         cuantización (exacta y median cut) y GIF animado leído de vuelta con el plugin de Qt
└── presentation/              ventana completa con plataforma offscreen
    ├── test_main_window.cpp   navegación (rail, camino de vuelta y fin de ciclo), ventana de ajustes, atajos del menú, Ctrl+F y filtro,
    │                          teclas de veredicto, captura, cuenta atrás, grabación, adjuntar por arrastre, abrir el visor, deshacer,
    │                          métricas y el aviso «Reintentar»
    ├── test_evidence_widgets.cpp renderAnnotations (formas, texto, difuminado), AnnotationEditor, ImageViewer, Thumbnail y ShotCard
    └── test_choice_dialog.cpp  selector con búsqueda: carga, filtro sin tildes, flechas, error y reintento, respuestas tardías
```

`core/test_metrics.cpp` cubre `metrics::` (por suite, ciclos, tendencia); `core/test_issue.cpp`, el modelo de issues;
`application/test_issue_store.cpp`, el store (asociaciones, importación, ausencias, resultados, datos ilegibles) e
`infrastructure/test_issue_repository.cpp`, `issues.json`;
`application/test_issue_publish_service.cpp`, la publicación en el gestor (borrador, creación, vinculación,
actualización pendiente, el resultado de la revisión y envíos sin confirmar);
`core/test_quality_record.cpp`, el acta y su borrador, y `application/test_quality_record_service.cpp`, el
acta de la revisión en curso (lo que hereda, lo que respeta y el fichero generado).

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
