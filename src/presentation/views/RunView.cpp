#include "RunView.h"

#include "application/BugStore.h"
#include "application/EvidenceService.h"
#include "application/RunController.h"
#include "application/RunHistoryStore.h"
#include "application/SettingsStore.h"
#include "application/TestCaseStore.h"
#include "core/models/RunHistory.h"   // formatDuration
#include "presentation/theme/Theme.h"
#include "presentation/views/BugDetailWindow.h"
#include "presentation/widgets/ElidedLabel.h"
#include "presentation/widgets/EvidenceActions.h"
#include "presentation/widgets/EvidencePreview.h"
#include "presentation/widgets/FlowLayout.h"
#include "presentation/widgets/Icons.h"
#include "presentation/widgets/ShotCard.h"
#include "presentation/widgets/TextArea.h"
#include "presentation/widgets/Ui.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QStackedWidget>

namespace qaflow {

namespace {

constexpr int kInspectorWidth = 380;
constexpr int kFilmCardWidth = 156;   // miniatura de la tira bajo el visor
constexpr int kActionHeight = 40;     // alto de los botones del pie: veredictos, capturar, bug, flechas

QString resultColor(StepResult r) {
    switch (r) {
        case StepResult::Pass: return theme::Green;
        case StepResult::Fail: return theme::Red;
        case StepResult::Block: return theme::Amber;
        case StepResult::Skip: return theme::Muted;
    }
    return theme::Muted;
}

QString resultLabel(StepResult r) {
    switch (r) {
        case StepResult::Pass: return QCoreApplication::translate("RunView", "PASA");
        case StepResult::Fail: return QCoreApplication::translate("RunView", "FALLA");
        case StepResult::Block: return QCoreApplication::translate("RunView", "BLOQ.");
        case StepResult::Skip: return QStringLiteral("N/A");
    }
    return {};
}

constexpr StepResult kAllResults[] = {StepResult::Pass, StepResult::Fail, StepResult::Block, StepResult::Skip};

/// Un veredicto del grupo: rótulo en el color del veredicto sobre su tinte, y la tecla al lado.
/// El color se fija en las etiquetas porque no heredan el `color` del botón (`color: inherit` no
/// existe en las hojas de estilo de Qt). `edge` redondea el lado de fuera del primero y del último.
QPushButton* verdictButton(const QString& text, const QString& key, const QString& color, const QString& fg,
                           const QString& edge) {
    auto* b = new QPushButton;
    b->setCursor(Qt::PointingHandCursor);
    b->setMinimumHeight(kActionHeight);
    b->setStyleSheet(QStringLiteral("QPushButton{background:%1;border:none;border-radius:0;padding:0;%3}"
                                    "QPushButton:hover{background:%2;}QPushButton:pressed{background:%4;}")
                         .arg(theme::tint(color, 34), theme::tint(color, 70), edge, theme::tint(color, 110)));
    auto* h = ui::hbox(b, 0, 6);
    h->setContentsMargins(10, 0, 10, 0);
    h->addStretch(1);
    auto* t = new QLabel(text);
    t->setStyleSheet(QStringLiteral("font-weight:800;font-size:13px;color:%1;background:transparent;").arg(fg));
    auto* k = new QLabel(key);
    k->setStyleSheet(QStringLiteral("font-weight:600;font-size:11px;color:%1;background:transparent;").arg(theme::Muted));
    h->addWidget(t);
    h->addWidget(k);
    h->addStretch(1);
    for (auto* c : b->findChildren<QWidget*>()) c->setAttribute(Qt::WA_TransparentForMouseEvents);
    return b;
}

/// Botón cuadrado con un icono de línea (bug, anotar, foco…).
QPushButton* iconButton(icons::Glyph glyph, const QString& color, const QString& tooltip, int size = 30) {
    auto* b = ui::button(QString(), "outline");
    b->setIcon(QIcon(icons::pixmap(glyph, color, 18)));
    b->setIconSize(QSize(16, 16));
    b->setFixedSize(size, size);
    b->setStyleSheet(QStringLiteral("padding:0;"));
    b->setToolTip(tooltip);
    b->setAccessibleName(tooltip);
    return b;
}

/// Pestaña del inspector: texto con subrayado, como las pestañas de un IDE.
QPushButton* inspectorTab(const QString& text, const char* name) {
    auto* b = new QPushButton(text);
    b->setObjectName(QString::fromLatin1(name));
    b->setCheckable(true);
    b->setCursor(Qt::PointingHandCursor);
    b->setStyleSheet(QStringLiteral("QPushButton{background:transparent;border:none;border-bottom:2px solid transparent;"
                                    "border-radius:0;padding:9px 12px;font-size:12.5px;font-weight:700;color:%1;}"
                                    "QPushButton:hover{color:%2;}QPushButton:checked{color:%2;border-bottom-color:%3;}")
                         .arg(theme::Muted, theme::Text, theme::Blue));
    return b;
}

/// Pastilla de estado de un paso: veredicto, «ACTIVO» o «PENDIENTE».
QLabel* statePill(const QString& text, const QString& color) {
    auto* l = ui::pill(text, theme::tint(color, 38), color);
    l->setStyleSheet(l->styleSheet() + QStringLiteral("font-size:10px;font-weight:800;border:1px solid %1;").arg(theme::tint(color, 110)));
    return l;
}

/// Bloque con título de la ficha del paso («DATOS», «RESULTADO ESPERADO»…): el texto entero, con salto
/// de línea y seleccionable para copiarlo.
QWidget* sheetBlock(const QString& title, QLabel** titleLabel, QLabel** text) {
    auto* w = new QWidget;
    auto* v = ui::vbox(w, 0, 4);
    auto* t = ui::label(title, "eyebrow");
    v->addWidget(t);
    auto* l = new QLabel;
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setStyleSheet(QStringLiteral("font-size:13px;color:%1;").arg(theme::TextSoft));
    v->addWidget(l);
    if (titleLabel) *titleLabel = t;
    *text = l;
    return w;
}

} // namespace

RunView::RunView(TestCaseStore& cases, RunController& run, RunHistoryStore& history, SettingsStore& settings,
                 EvidenceService& evidence, BugStore& bugs, QWidget* parent)
    : QWidget(parent), m_cases(cases), m_run(run), m_history(history), m_settings(settings), m_evidence(evidence), m_bugs(bugs) {
    auto* root = ui::hbox(this, 0, 0);
    root->addWidget(buildStage(), 1);
    root->addWidget(buildInspector());

    // Atajos de teclado (sólo mientras esta vista es visible y el foco no está en un campo de texto)
    for (auto [key, res] : {std::pair{Qt::Key_P, StepResult::Pass}, std::pair{Qt::Key_F, StepResult::Fail},
                            std::pair{Qt::Key_B, StepResult::Block}, std::pair{Qt::Key_S, StepResult::Skip}}) {
        auto* sc = new QShortcut(QKeySequence(key), this);
        sc->setContext(Qt::WindowShortcut);
        connect(sc, &QShortcut::activated, this, [this, res]() { if (isVisible() && m_run.isRunning() && !m_run.isPaused()) m_run.mark(res); });
    }
    // Ir y venir por los pasos sin tocar sus veredictos.
    const struct { QKeySequence key; int delta; } moves[] = {
        {QKeySequence(Qt::Key_Backspace), -1}, {QKeySequence(Qt::ALT | Qt::Key_Left), -1}, {QKeySequence(Qt::ALT | Qt::Key_Right), +1}};
    for (const auto& m : moves) {
        auto* sc = new QShortcut(m.key, this);
        sc->setContext(Qt::WindowShortcut);
        connect(sc, &QShortcut::activated, this, [this, delta = m.delta]() {
            if (!isVisible() || m_run.state().caseId.isEmpty()) return;
            if (delta < 0) m_run.back(); else m_run.next();
        });
    }
    // Modo foco: F11 entra y sale (la F ya es «falla»); Esc sólo sale, y sólo existe mientras dura.
    auto* focus = new QShortcut(QKeySequence(Qt::Key_F11), this);
    focus->setContext(Qt::WindowShortcut);
    connect(focus, &QShortcut::activated, this, [this]() { if (isVisible()) setFocusMode(!m_focusMode); });
    m_exitFocus = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    m_exitFocus->setContext(Qt::WindowShortcut);
    m_exitFocus->setEnabled(false);
    connect(m_exitFocus, &QShortcut::activated, this, [this]() { setFocusMode(false); });

    connect(&m_run, &RunController::runChanged, this, &RunView::refresh);
    connect(&m_cases, &TestCaseStore::caseChanged, this, [this](const QString& id) { if (id == m_run.state().caseId) refresh(); });
    connect(&m_settings, &SettingsStore::captureChanged, this, &RunView::refresh);
    // Reportar un bug de un paso se ve en su tarjeta al volver a la ejecución.
    connect(&m_bugs, &BugStore::bugsChanged, this, &RunView::refresh);
    m_clock.setInterval(1000);
    connect(&m_clock, &QTimer::timeout, this, &RunView::tick);
    refresh();
}

void RunView::setFocusMode(bool on) {
    if (on && !m_cases.find(m_run.state().caseId)) on = false;
    if (on == m_focusMode) return;
    m_focusMode = on;
    m_exitFocus->setEnabled(on);
    m_focusButton->setChecked(on);
    m_focusButton->setToolTip(on ? tr("Salir del modo foco (Esc o F11)") : tr("Modo foco: la evidencia a toda la ventana (F11)"));
    refresh();
    emit focusModeChanged(on);
}

void RunView::hideEvent(QHideEvent* e) {
    setFocusMode(false);
    QWidget::hideEvent(e);
}

// ---- Izquierda: el caso, el visor y la tira ---------------------------------------------------

QWidget* RunView::buildStage() {
    auto* panel = new QWidget;
    auto* v = ui::vbox(panel, 0, 10);
    v->setContentsMargins(16, 12, 14, 12);
    v->addWidget(buildCaseBar());

    // El visor con su barra encima: nada de lo que se hace con la evidencia tapa la imagen.
    m_stage = new QWidget;
    auto* sv = ui::vbox(m_stage, 0, 8);

    m_shotBar = new QFrame;
    m_shotBar->setObjectName(QStringLiteral("shotBar"));
    m_shotBar->setStyleSheet(QStringLiteral("QFrame#shotBar{background:%1;border:1px solid %2;border-radius:9px;}").arg(theme::Panel, theme::Border));
    auto* bh = ui::hbox(m_shotBar, 0, 8);
    bh->setContentsMargins(10, 6, 8, 6);
    // De qué paso es la evidencia abierta y cómo se llama: en la barra, no pintado sobre la imagen.
    m_shotStep = new QLabel;
    m_shotStep->setObjectName(QStringLiteral("shotStep"));
    m_shotStep->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    bh->addWidget(m_shotStep);
    m_shotName = new ElidedLabel;
    m_shotName->setObjectName(QStringLiteral("shotName"));
    m_shotName->setProperty("role", QStringLiteral("mono-muted"));
    m_shotName->setMaximumWidth(260);
    bh->addWidget(m_shotName, 4);
    // Primero crecen el fichero y el selector, hasta su tope; lo que sobra con la ventana ancha (modo
    // foco) va a este hueco, y los botones no se estiran.
    bh->addStretch(1);
    auto* assignLabel = ui::label(tr("ASIGNAR A"), "eyebrow");
    bh->addWidget(assignLabel);
    m_assign = new QComboBox;
    m_assign->setObjectName(QStringLiteral("assignStep"));
    m_assign->setMinimumWidth(150);
    m_assign->setMaximumWidth(280);
    m_assign->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_assign->setMinimumContentsLength(18);
    m_assign->setStyleSheet(QStringLiteral("QComboBox{background:%1;font-size:11.5px;}").arg(theme::Field));
    connect(m_assign, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_selfEdit || !m_selectedShot) return;
        m_cases.assignShotStep(m_run.state().caseId, m_selectedShot, m_assign->currentData().toInt());
    });
    bh->addWidget(m_assign, 4);
    auto* separator = new QFrame;
    separator->setFixedSize(1, 22);
    separator->setStyleSheet(QStringLiteral("background:%1;").arg(theme::Border));
    bh->addWidget(separator);
    auto* zoom = ui::button(tr("Ampliar"), "outline");
    zoom->setObjectName(QStringLiteral("shotZoom"));
    zoom->setFixedHeight(30);
    zoom->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    zoom->setStyleSheet(QStringLiteral("padding:0 10px;font-size:11.5px;"));
    zoom->setToolTip(tr("Abre la evidencia a tamaño completo, con zoom"));
    connect(zoom, &QPushButton::clicked, this, &RunView::openSelectedShot);
    bh->addWidget(zoom);
    auto* annotate = iconButton(icons::Glyph::Annotate, theme::TextSoft, tr("Anotar (flechas, rectángulos, texto, difuminado)"));
    annotate->setObjectName(QStringLiteral("shotAnnotate"));
    connect(annotate, &QPushButton::clicked, this, &RunView::annotateSelectedShot);
    bh->addWidget(annotate);
    m_focusButton = ui::button(tr("Foco"), "outline");
    m_focusButton->setObjectName(QStringLiteral("runFocus"));
    m_focusButton->setIcon(QIcon(icons::pixmap(icons::Glyph::Focus, theme::TextSoft, 18)));
    m_focusButton->setIconSize(QSize(14, 14));
    m_focusButton->setCheckable(true);
    m_focusButton->setFixedHeight(30);
    m_focusButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_focusButton->setStyleSheet(QStringLiteral("QPushButton{padding:0 10px;font-size:11.5px;}QPushButton:checked{border-color:%1;color:%1;}").arg(theme::Blue));
    m_focusButton->setToolTip(tr("Modo foco: la evidencia a toda la ventana (F11)"));
    connect(m_focusButton, &QPushButton::clicked, this, [this](bool on) { setFocusMode(on); });
    bh->addWidget(m_focusButton);
    auto* remove = ui::button(QStringLiteral("×"), "icon");
    remove->setToolTip(tr("Eliminar esta evidencia"));
    connect(remove, &QPushButton::clicked, this, [this]() {
        if (m_selectedShot) m_cases.removeShot(m_run.state().caseId, m_selectedShot);
    });
    bh->addWidget(remove);
    sv->addWidget(m_shotBar);

    m_preview = new EvidencePreview;
    m_preview->setObjectName(QStringLiteral("evidencePreview"));
    m_preview->setCaptionVisible(false);
    // Clic en la evidencia: una imagen fija se abre directamente para anotarla; lo demás, en el visor.
    connect(m_preview, &EvidencePreview::clicked, this, [this]() {
        const Screenshot* shot = selectedShot();
        if (shot && shot->isImage() && !shot->isAnimation()) annotateSelectedShot();
        else openSelectedShot();
    });
    // Recorrer las evidencias sin salir del visor: flechas a los lados y la posición abajo.
    const auto arrow = [this](const QString& text, const char* name, const QString& tip, int delta) {
        auto* b = new QPushButton(text, m_preview);
        b->setObjectName(QString::fromLatin1(name));
        b->setToolTip(tip);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(40, 40);
        b->setStyleSheet(QStringLiteral("QPushButton{background:%1;color:%2;border:1px solid %3;border-radius:20px;font-size:20px;padding:0 0 3px 0;}"
                                        "QPushButton:hover{background:%4;}QPushButton:disabled{color:%5;}")
                             .arg(theme::tint(theme::Panel, 225), theme::Text, theme::Border, theme::Elevated, theme::Disabled));
        connect(b, &QPushButton::clicked, this, [this, delta]() { selectRelativeShot(delta); });
        return b;
    };
    m_shotPrev = arrow(QStringLiteral("‹"), "shotPrev", tr("Evidencia anterior"), -1);
    m_shotNext = arrow(QStringLiteral("›"), "shotNext", tr("Evidencia siguiente"), +1);
    m_shotCounter = new QLabel(m_preview);
    m_shotCounter->setObjectName(QStringLiteral("shotCounter"));
    m_shotCounter->setStyleSheet(QStringLiteral("background:%1;color:%2;border:1px solid %3;border-radius:5px;padding:2px 8px;"
                                                "font-size:11px;font-weight:700;")
                                     .arg(theme::tint(theme::Panel, 225), theme::TextSoft, theme::Border));
    m_preview->installEventFilter(this);
    sv->addWidget(m_preview, 1);
    m_shotControls = {assignLabel, m_assign, zoom, annotate, remove, m_shotPrev, m_shotNext};
    v->addWidget(m_stage, 1);

    v->addWidget(buildFilmStrip());
    v->addWidget(buildFocusBar());

    // Sin ejecución la pantalla se queda vacía: el mensaje ocupa su sitio.
    m_empty = new QWidget;
    auto* ev = ui::vbox(m_empty, 0, 6);
    ev->addStretch(1);
    auto* emptyEyebrow = ui::label(tr("EJECUCIÓN MANUAL"), "eyebrow");
    emptyEyebrow->setAlignment(Qt::AlignCenter);
    ev->addWidget(emptyEyebrow);
    auto* emptyTitle = ui::label(tr("No hay ninguna ejecución activa"), "h2");
    emptyTitle->setAlignment(Qt::AlignCenter);
    ev->addWidget(emptyTitle);
    auto* emptyHint = ui::label(tr("Abre un caso en «Casos de prueba» y pulsa ▶ Ejecutar (F5) para empezar."), "muted");
    emptyHint->setAlignment(Qt::AlignCenter);
    emptyHint->setWordWrap(true);
    ev->addWidget(emptyHint);
    ev->addStretch(1);
    v->addWidget(m_empty, 1);
    return panel;
}

QWidget* RunView::buildCaseBar() {
    // El caso que se está probando, encima de la imagen: qué es, cómo va y cómo se cierra.
    m_caseBar = new QWidget;
    m_caseBar->setObjectName(QStringLiteral("runCaseBar"));
    auto* v = ui::vbox(m_caseBar, 0, 8);
    auto* row = new QWidget;
    auto* h = ui::hbox(row, 0, 10);
    m_stateDot = ui::dot(theme::Green, 8);
    h->addWidget(m_stateDot);
    m_stateText = ui::label(QString(), "eyebrow");
    h->addWidget(m_stateText);
    m_caseTitle = new ElidedLabel;
    m_caseTitle->setObjectName(QStringLiteral("runCaseTitle"));
    m_caseTitle->setStyleSheet(QStringLiteral("font-size:15px;font-weight:800;"));
    h->addWidget(m_caseTitle, 1);
    m_caseStats = ui::label(QString(), "mono-muted");
    h->addWidget(m_caseStats);
    m_pause = pauseButton("runPause");
    m_pause->setFixedHeight(32);
    h->addWidget(m_pause);
    m_finish = ui::button(tr("Cerrar ejecución"), "outline");
    m_finish->setObjectName(QStringLiteral("runFinish"));
    m_finish->setFixedHeight(32);
    m_finish->setStyleSheet(QStringLiteral("padding:0 12px;"));
    connect(m_finish, &QPushButton::clicked, this, [this]() {
        // Cerrarla a medias la archiva con lo que se llegó a marcar: se avisa antes.
        const RunState& r = m_run.state();
        const int pending = m_run.totalSteps() - r.markedCount();
        if (m_run.isRunning() && pending > 0 &&
            QMessageBox::question(this, tr("Cerrar la ejecución"),
                                  tr("Quedan %1 pasos sin marcar. Se archivará en el historial con los %2 que ya tienen veredicto; "
                                     "los demás quedan como N/A.").arg(pending).arg(r.markedCount())) != QMessageBox::Yes)
            return;
        emit finishRequested();
    });
    h->addWidget(m_finish);
    v->addWidget(row);

    // De qué va esta ejecución: la ronda del control de calidad y, sobre todo, si se está continuando
    // una anterior (entonces no se prueba el plan entero, sólo lo que se rompió).
    m_continuation = ui::card("card-flat");
    m_continuation->setObjectName(QStringLiteral("runContinuation"));
    auto* cv = ui::vbox(m_continuation, 0, 2);
    cv->setContentsMargins(10, 7, 10, 7);
    m_continuationText = ui::label(QString(), "muted-sm");
    m_continuationText->setWordWrap(true);
    cv->addWidget(m_continuationText);
    v->addWidget(m_continuation);
    return m_caseBar;
}

QWidget* RunView::buildFilmStrip() {
    // Tira de capturas: en horizontal bajo el visor, para que el visor se quede con todo el ancho.
    m_filmPanel = new QFrame;
    m_filmPanel->setObjectName(QStringLiteral("filmPanel"));
    m_filmPanel->setFixedHeight(146);
    auto* fh = ui::hbox(m_filmPanel, 0, 10);

    auto* side = new QWidget;
    side->setFixedWidth(128);
    auto* sdv = ui::vbox(side, 0, 6);
    m_shotsCount = ui::label(tr("CAPTURAS"), "eyebrow");
    m_shotsCount->setWordWrap(true);
    sdv->addWidget(m_shotsCount);
    m_sortShots = ui::button(tr("Ordenar por paso"), "outline");
    m_sortShots->setStyleSheet(QStringLiteral("padding:3px 6px;font-size:11px;border-radius:7px;"));
    m_sortShots->setToolTip(tr("Ordena las evidencias del caso por el paso al que están asignadas"));
    connect(m_sortShots, &QPushButton::clicked, this, [this]() { m_cases.sortShotsByStep(m_run.state().caseId); });
    sdv->addWidget(m_sortShots);
    sdv->addStretch(1);
    fh->addWidget(side);

    m_filmScroll = new QScrollArea;
    m_filmScroll->setObjectName(QStringLiteral("filmScroll"));
    m_filmScroll->setWidgetResizable(true);
    m_filmScroll->setFrameShape(QFrame::NoFrame);
    m_filmScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* strip = new QWidget;
    m_shotsLayout = ui::hbox(strip, 0, 10);
    m_shotsLayout->addStretch(1);
    m_filmScroll->setWidget(strip);
    fh->addWidget(m_filmScroll, 1);

    auto* actions = new QWidget;
    actions->setFixedWidth(128);
    auto* av = ui::vbox(actions, 0, 6);
    auto* capture = ui::button(tr("+ Capturar"), "dashed");
    capture->setStyleSheet(QStringLiteral("padding:10px;font-size:12.5px;"));
    connect(capture, &QPushButton::clicked, this, &RunView::captureRequested);
    av->addWidget(capture, 1);
    m_record = ui::button(tr("● GIF"), "dashed");
    m_record->setStyleSheet(QStringLiteral("padding:6px;font-size:12px;"));
    m_record->setToolTip(tr("Graba la pantalla o una región a GIF y la adjunta al caso"));
    m_record->setVisible(m_evidence.canRecord());
    connect(m_record, &QPushButton::clicked, this, [this]() { m_evidence.toggleRecording(); });
    connect(&m_evidence, &EvidenceService::recordingChanged, this, [this](bool on) { m_record->setText(on ? tr("■ Detener") : tr("● GIF")); });
    av->addWidget(m_record);
    auto* attach = ui::button(tr("+ Archivo"), "dashed");
    attach->setStyleSheet(QStringLiteral("padding:6px;font-size:12px;"));
    attach->setToolTip(tr("Adjunta logs, vídeos o imágenes existentes"));
    connect(attach, &QPushButton::clicked, this, [this]() { m_evidence.attachFiles(evidence::pickFiles(this)); });
    av->addWidget(attach);
    fh->addWidget(actions);
    return m_filmPanel;
}

QWidget* RunView::buildVerdicts() {
    auto* group = new QFrame;
    group->setObjectName(QStringLiteral("verdictGroup"));
    group->setStyleSheet(QStringLiteral("QFrame#verdictGroup{background:%1;border:1px solid %2;border-radius:9px;}").arg(theme::Panel, theme::Border));
    auto* h = ui::hbox(group, 0, 0);
    h->setContentsMargins(1, 1, 1, 1);
    const struct { QString text; QString key; QString color; QString fg; StepResult result; } verdicts[] = {
        {tr("Pasa"), tr("P"), theme::Green, theme::Green, StepResult::Pass},
        {tr("Falla"), tr("F"), theme::Red, theme::RedSoft, StepResult::Fail},
        {tr("Bloq."), tr("B"), theme::Amber, theme::AmberSoft, StepResult::Block},
        {tr("N/A"), tr("S"), theme::Muted, theme::TextSoft, StepResult::Skip}};
    const int last = int(std::size(verdicts)) - 1;
    for (int i = 0; i <= last; ++i) {
        const auto& v = verdicts[i];
        // Separador entre veredictos y el lado de fuera redondeado, para que el grupo se lea como uno.
        QString edge = i > 0 ? QStringLiteral("border-left:1px solid %1;").arg(theme::Border) : QString();
        if (i == 0) edge += QStringLiteral("border-top-left-radius:8px;border-bottom-left-radius:8px;");
        if (i == last) edge += QStringLiteral("border-top-right-radius:8px;border-bottom-right-radius:8px;");
        auto* b = verdictButton(v.text, v.key, v.color, v.fg, edge);
        b->setToolTip(v.result == StepResult::Skip ? tr("Saltar: el paso no cuenta para el veredicto (S)")
                                                   : tr("Marcar el paso: %1 (%2)").arg(v.text, v.key));
        connect(b, &QPushButton::clicked, this, [this, r = v.result]() { m_run.mark(r); });
        h->addWidget(b, 1);
    }
    return group;
}

QPushButton* RunView::pauseButton(const char* name) {
    auto* b = ui::button(tr("❚❚ Pausar"), "outline");
    b->setObjectName(QString::fromLatin1(name));
    b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(b, &QPushButton::clicked, this, [this]() { m_run.togglePause(); });
    return b;
}

QPushButton* RunView::captureButton(QLabel** shortcut) {
    auto* b = ui::button(QString(), "primary");
    b->setMinimumHeight(kActionHeight);
    auto* h = ui::hbox(b, 0, 8);
    h->setContentsMargins(12, 0, 12, 0);
    h->addStretch(1);
    auto* icon = new QLabel;
    icon->setPixmap(icons::pixmap(icons::Glyph::Capture, theme::OnAccent, 18));
    icon->setStyleSheet(QStringLiteral("background:transparent;"));
    h->addWidget(icon);
    auto* text = new QLabel(tr("Capturar"));
    text->setStyleSheet(QStringLiteral("font-weight:800;background:transparent;color:%1;").arg(theme::OnAccent));
    h->addWidget(text);
    if (shortcut) {
        *shortcut = new QLabel;
        (*shortcut)->setStyleSheet(QStringLiteral("font-size:11px;font-weight:700;background:transparent;color:%1;").arg(theme::tint(theme::OnAccent, 170)));
        h->addWidget(*shortcut);
    }
    h->addStretch(1);
    for (auto* c : b->findChildren<QWidget*>()) c->setAttribute(Qt::WA_TransparentForMouseEvents);
    connect(b, &QPushButton::clicked, this, &RunView::captureRequested);
    return b;
}

QWidget* RunView::buildFocusBar() {
    // El mando del modo foco: lo justo para seguir probando sin salir de la evidencia.
    m_focusBar = new QFrame;
    m_focusBar->setObjectName(QStringLiteral("focusBar"));
    m_focusBar->setStyleSheet(QStringLiteral("QFrame#focusBar{background:%1;border:1px solid %2;border-radius:12px;}").arg(theme::Panel, theme::Border));
    auto* h = ui::hbox(m_focusBar, 0, 12);
    h->setContentsMargins(16, 8, 10, 8);
    m_focusCounter = ui::label(QString(), "eyebrow");
    h->addWidget(m_focusCounter);
    m_focusAction = new ElidedLabel;
    m_focusAction->setStyleSheet(QStringLiteral("font-size:13.5px;font-weight:700;"));
    h->addWidget(m_focusAction, 1);
    m_focusVerdicts = buildVerdicts();
    m_focusVerdicts->setFixedWidth(360);
    h->addWidget(m_focusVerdicts);
    auto* capture = captureButton(nullptr);
    capture->setFixedWidth(128);
    h->addWidget(capture);
    m_focusReportBug = iconButton(icons::Glyph::Bug, theme::RedSoft, tr("Reportar bug"), kActionHeight);
    m_focusReportBug->setObjectName(QStringLiteral("focusReportBug"));
    connect(m_focusReportBug, &QPushButton::clicked, this, [this]() { emit reportBugRequested(bugStepIndex()); });
    h->addWidget(m_focusReportBug);
    m_focusPause = pauseButton("focusPause");
    m_focusPause->setFixedHeight(kActionHeight);
    h->addWidget(m_focusPause);
    auto* exit = ui::button(tr("Salir · Esc"), "outline");
    exit->setObjectName(QStringLiteral("focusExit"));
    exit->setFixedHeight(kActionHeight);
    exit->setStyleSheet(QStringLiteral("padding:0 12px;"));
    connect(exit, &QPushButton::clicked, this, [this]() { setFocusMode(false); });
    h->addWidget(exit);
    m_focusBar->hide();
    return m_focusBar;
}

// ---- Derecha: el inspector ------------------------------------------------------------------

QWidget* RunView::buildInspector() {
    m_casePanel = ui::card("list-pane");
    m_casePanel->setObjectName(QStringLiteral("casePanel"));
    m_casePanel->setFixedWidth(kInspectorWidth);
    // El panel va a la derecha: su borde, a la izquierda.
    m_casePanel->setStyleSheet(QStringLiteral("QFrame#casePanel{border-right:none;border-left:1px solid %1;}").arg(theme::Border));
    auto* v = ui::vbox(m_casePanel, 0, 0);

    // Las pestañas: el paso en pantalla, todos los pasos, lo que se ha reportado y, en un ciclo, sus casos.
    auto* tabs = new QFrame;
    tabs->setObjectName(QStringLiteral("inspectorTabs"));
    tabs->setStyleSheet(QStringLiteral("QFrame#inspectorTabs{border:none;border-bottom:1px solid %1;}").arg(theme::Border));
    auto* th = ui::hbox(tabs, 0, 2);
    th->setContentsMargins(10, 6, 10, 0);
    m_stepTab = inspectorTab(tr("Paso"), "runStepTab");
    m_stepsTab = inspectorTab(tr("Pasos"), "runStepsTab");
    m_bugsTab = inspectorTab(tr("Bugs"), "runBugsTab");
    m_casesTab = inspectorTab(tr("Casos"), "runCasesTab");
    m_casesTab->setToolTip(tr("Los casos del ciclo: ir a otro deja éste en pausa, tal como está"));
    for (auto* t : {m_stepTab, m_stepsTab, m_bugsTab, m_casesTab}) th->addWidget(t);
    th->addStretch(1);
    connect(m_stepTab, &QPushButton::clicked, this, [this]() { showTab(0); });
    connect(m_stepsTab, &QPushButton::clicked, this, [this]() { showTab(1); });
    connect(m_bugsTab, &QPushButton::clicked, this, [this]() { showTab(2); });
    connect(m_casesTab, &QPushButton::clicked, this, [this]() { showTab(3); });
    v->addWidget(tabs);

    m_inspectorStack = new QStackedWidget;
    v->addWidget(m_inspectorStack, 1);
    m_inspectorStack->addWidget(buildStepPage());

    // Pestaña «Pasos»: todos, con su veredicto; un clic lleva a ese paso.
    auto* stepsPage = new QWidget;
    auto* spv = ui::vbox(stepsPage, 0, 0);
    spv->setContentsMargins(10, 10, 10, 8);
    QWidget* list;
    QVBoxLayout* listLayout;
    m_stepsScroll = ui::scrollArea(&list, &listLayout);
    m_stepsScroll->setObjectName(QStringLiteral("stepsScroll"));
    m_stepsLayout = listLayout;
    m_stepsLayout->setSpacing(4);
    m_stepsLayout->addStretch(1);
    spv->addWidget(m_stepsScroll, 1);
    m_inspectorStack->addWidget(stepsPage);

    // Pestaña «Bugs»: los partes que han salido de este caso, por paso. La ficha de cada uno se abre
    // en su propia ventana, para mirarla sin perder la prueba de vista.
    auto* bugsPage = new QWidget;
    auto* bv = ui::vbox(bugsPage, 0, 10);
    bv->setContentsMargins(14, 12, 14, 10);
    QWidget* bugsList;
    QVBoxLayout* bugsLayout;
    auto* bugsScroll = ui::scrollArea(&bugsList, &bugsLayout);
    bugsScroll->setObjectName(QStringLiteral("bugsScroll"));
    m_bugsLayout = bugsLayout;
    m_bugsLayout->setSpacing(8);
    m_bugsEmpty = ui::label(tr("Todavía no se ha reportado ningún bug de este caso.\nAl reportar uno queda aquí, con el paso del que salió."), "muted-sm");
    m_bugsEmpty->setWordWrap(true);
    m_bugsLayout->addWidget(m_bugsEmpty);
    m_bugsLayout->addStretch(1);
    bv->addWidget(bugsScroll, 1);
    auto* report = ui::button(tr("+ Reportar bug"), "dashed");
    report->setObjectName(QStringLiteral("runTabReportBug"));
    report->setStyleSheet(QStringLiteral("padding:12px;font-size:12.5px;"));
    report->setToolTip(tr("Abre el parte con el paso del que salió el fallo ya puesto"));
    connect(report, &QPushButton::clicked, this, [this]() { emit reportBugRequested(bugStepIndex()); });
    bv->addWidget(report);
    m_inspectorStack->addWidget(bugsPage);

    // Pestaña «Casos»: los del ciclo, con cómo va cada uno. Un clic en uno pendiente lo pone en
    // pantalla y deja éste en pausa, sin archivarlo: se retoma donde se dejó.
    auto* casesPage = new QWidget;
    auto* cpv = ui::vbox(casesPage, 0, 0);
    cpv->setContentsMargins(10, 10, 10, 8);
    QWidget* casesList;
    QVBoxLayout* casesLayout;
    m_casesScroll = ui::scrollArea(&casesList, &casesLayout);
    m_casesScroll->setObjectName(QStringLiteral("casesScroll"));
    m_casesLayout = casesLayout;
    m_casesLayout->setSpacing(4);
    m_casesLayout->addStretch(1);
    cpv->addWidget(m_casesScroll, 1);
    m_inspectorStack->addWidget(casesPage);
    showTab(0);

    v->addWidget(buildFooter());
    return m_casePanel;
}

QWidget* RunView::buildStepPage() {
    // Pestaña «Paso»: los números de todos los pasos para saltar, y la ficha completa del activo. Los
    // textos pueden ser largos: se leen enteros, con scroll, y nada que se lee se recorta a una línea.
    auto* page = new QWidget;
    auto* v = ui::vbox(page, 0, 10);
    v->setContentsMargins(14, 12, 14, 0);
    auto* chips = new QWidget;
    chips->setObjectName(QStringLiteral("stepChips"));
    m_chipsLayout = new FlowLayout(chips, 0, 6, 6);
    v->addWidget(chips);

    QWidget* sheet;
    QVBoxLayout* dv;
    auto* sheetScroll = ui::scrollArea(&sheet, &dv);
    sheetScroll->setObjectName(QStringLiteral("stepSheet"));
    dv->setSpacing(14);
    dv->setContentsMargins(0, 4, 8, 12);
    auto* counterRow = new QWidget;
    auto* crh = ui::hbox(counterRow, 0, 8);
    m_stepCounter = ui::label(QString(), "eyebrow");
    crh->addWidget(m_stepCounter);
    crh->addStretch(1);
    m_stepClock = ui::label(QString(), "mono-muted");
    m_stepClock->setToolTip(tr("Tiempo en este paso"));
    crh->addWidget(m_stepClock);
    dv->addWidget(counterRow);

    m_action = new QLabel;
    m_action->setObjectName(QStringLiteral("stepAction"));
    m_action->setWordWrap(true);
    m_action->setTextInteractionFlags(Qt::TextSelectableByMouse);
    dv->addWidget(m_action);
    m_dataBlock = sheetBlock(tr("DATOS"), nullptr, &m_data);
    dv->addWidget(m_dataBlock);
    dv->addWidget(sheetBlock(tr("RESULTADO ESPERADO"), &m_expectedTitle, &m_expected));
    m_expected->setObjectName(QStringLiteral("stepExpected"));
    m_noteBlock = new QWidget;
    auto* nv = ui::vbox(m_noteBlock, 0, 4);
    nv->addWidget(ui::label(tr("OBSERVACIONES"), "eyebrow"));
    m_note = new TextArea(2);
    m_note->setPlaceholderText(tr("Observaciones de este paso (opcional)…"));
    connect(m_note, &TextArea::edited, this, [this](const QString& t) { m_run.setNote(t); });
    nv->addWidget(m_note);
    dv->addWidget(m_noteBlock);
    dv->addStretch(1);
    v->addWidget(sheetScroll, 1);
    return page;
}

QWidget* RunView::buildFooter() {
    // El pie: lo que se hace con el paso, siempre a la vista por largo que sea su texto.
    auto* footer = new QFrame;
    footer->setObjectName(QStringLiteral("inspectorFooter"));
    footer->setStyleSheet(QStringLiteral("QFrame#inspectorFooter{background:%1;border:none;border-top:1px solid %2;}").arg(theme::Panel, theme::Border));
    auto* v = ui::vbox(footer, 0, 8);
    v->setContentsMargins(14, 12, 14, 14);
    m_verdicts = buildVerdicts();
    v->addWidget(m_verdicts);

    // Mismo hueco cuando la ejecución ya ha terminado.
    m_doneActions = new QWidget;
    auto* dh = ui::hbox(m_doneActions, 0, 8);
    m_reopen = ui::button(tr("← Último paso"), "outline");
    m_reopen->setMinimumHeight(kActionHeight);
    m_reopen->setToolTip(tr("Reabre el último paso para cambiar su veredicto"));
    connect(m_reopen, &QPushButton::clicked, this, [this]() { m_run.back(); });
    dh->addWidget(m_reopen, 1);
    auto* repeat = ui::button(tr("Repetir"), "outline");
    repeat->setMinimumHeight(kActionHeight);
    repeat->setToolTip(tr("Vuelve a ejecutar el caso desde el primer paso"));
    connect(repeat, &QPushButton::clicked, this, [this]() { m_run.restart(); });
    dh->addWidget(repeat, 1);
    v->addWidget(m_doneActions);

    // Capturar, el bug (en cualquier momento de la ejecución, no sólo al terminarla: el paso que
    // falla o bloquea se reporta en cuanto se ve y se sigue probando) y el paso anterior o siguiente.
    auto* row = new QWidget;
    auto* h = ui::hbox(row, 0, 8);
    m_capture = captureButton(&m_captureShortcut);
    m_capture->setObjectName(QStringLiteral("runCapture"));
    h->addWidget(m_capture, 1);
    m_reportBug = iconButton(icons::Glyph::Bug, theme::RedSoft, tr("Reportar bug"), kActionHeight);
    m_reportBug->setObjectName(QStringLiteral("runReportBug"));
    connect(m_reportBug, &QPushButton::clicked, this, [this]() { emit reportBugRequested(bugStepIndex()); });
    h->addWidget(m_reportBug);
    const auto move = [this, h](const QString& text, const char* name, const QString& tip) {
        auto* b = ui::button(text, "outline");
        b->setObjectName(QString::fromLatin1(name));
        b->setFixedSize(kActionHeight, kActionHeight);
        b->setStyleSheet(QStringLiteral("padding:0;font-size:15px;"));
        b->setToolTip(tip);
        h->addWidget(b);
        return b;
    };
    // Los pasos se recorren en cualquier orden: por los números, la lista, estas flechas o el teclado.
    m_back = move(QStringLiteral("←"), "stepPrev", tr("Vuelve al paso anterior sin tocar su veredicto (Retroceso o Alt+←)"));
    connect(m_back, &QPushButton::clicked, this, [this]() { m_run.back(); });
    m_next = move(QStringLiteral("→"), "stepNext", tr("Pasa al siguiente sin darle veredicto a este (Alt+→)"));
    connect(m_next, &QPushButton::clicked, this, [this]() { m_run.next(); });
    v->addWidget(row);
    return footer;
}

void RunView::showTab(int index) {
    m_inspectorStack->setCurrentIndex(index);
    m_stepTab->setChecked(index == 0);
    m_stepsTab->setChecked(index == 1);
    m_bugsTab->setChecked(index == 2);
    m_casesTab->setChecked(index == 3);
}

QWidget* RunView::stepGroupHeader(int step, const TestCase& c) const {
    const QString text = step <= 0
                             ? tr("SIN PASO")
                             : (step <= c.steps.size()
                                    ? tr("PASO %1 · %2").arg(step, 2, 10, QLatin1Char('0')).arg(ui::elide(c.steps[step - 1].action, 26))
                                    : tr("PASO %1").arg(step, 2, 10, QLatin1Char('0')));
    auto* header = ui::label(text, "eyebrow");
    header->setWordWrap(true);
    return header;
}

// ---- Refresco --------------------------------------------------------------------------------

void RunView::tick() {
    const RunState& r = m_run.state();
    if (r.caseId.isEmpty()) { m_clock.stop(); return; }
    if (!r.finished) m_stepClock->setText((r.paused ? tr("❚❚ %1") : tr("⏱ %1")).arg(formatDuration(r.currentStepSecs())));
    const TestCase* c = m_cases.find(r.caseId);
    if (!c) return;
    m_caseStats->setText(tr("%1/%2 pasos · %3 capturas · ⏱ %4")
                             .arg(r.markedCount()).arg(c->steps.size()).arg(c->shotsOfRun(QString()).size()).arg(formatDuration(r.elapsedSecs())));
}

void RunView::refresh() {
    const RunState& r = m_run.state();
    const TestCase* c = m_cases.find(r.caseId);
    m_captureShortcut->setText(m_settings.capture().shortcut);

    const bool hasRun = c != nullptr;
    const bool active = hasRun && !r.finished && !c->steps.isEmpty();
    // Sin ejecución no hay nada que enfocar; con ella, el modo foco lo esconde todo menos el visor.
    if (!hasRun && m_focusMode) {
        m_focusMode = false;
        m_exitFocus->setEnabled(false);
        m_focusButton->setChecked(false);
        emit focusModeChanged(false);
    }
    m_caseBar->setVisible(hasRun && !m_focusMode);
    m_casePanel->setVisible(hasRun && !m_focusMode);
    m_filmPanel->setVisible(hasRun && !m_focusMode);
    m_focusBar->setVisible(hasRun && m_focusMode);
    m_stage->setVisible(hasRun);   // el visor y su barra sólo con ejecución
    m_shotBar->setVisible(hasRun);
    m_noteBlock->setVisible(active);
    m_empty->setVisible(!hasRun);
    m_verdicts->setVisible(active);
    m_focusVerdicts->setVisible(active);
    m_doneActions->setVisible(hasRun && r.finished);
    m_continuation->setVisible(false);
    // En pausa no se marca ni se cambia de paso: los veredictos siguen a la vista, apagados.
    const bool paused = active && m_run.isPaused();
    m_verdicts->setEnabled(!paused);
    m_focusVerdicts->setEnabled(!paused);
    for (auto* b : {m_pause, m_focusPause}) {
        b->setVisible(active);
        b->setText(paused ? tr("▶ Reanudar") : tr("❚❚ Pausar"));
        b->setToolTip(paused ? tr("Reanuda la ejecución: los cronómetros vuelven a correr (Ctrl+Alt+Espacio)")
                             : tr("Pausa la ejecución: el tiempo en pausa no cuenta (Ctrl+Alt+Espacio)"));
        b->setStyleSheet(paused ? QStringLiteral("QPushButton{padding:0 12px;border-color:%1;color:%1;}").arg(theme::Amber)
                                : QStringLiteral("QPushButton{padding:0 12px;}"));
    }
    if (!hasRun) {
        m_clock.stop();
        ui::clearLayout(m_stepsLayout);
        ui::clearLayout(m_chipsLayout);
        ui::clearLayout(m_shotsLayout);
        ui::clearLayout(m_bugsLayout);
        ui::clearLayout(m_casesLayout);
        m_casesTab->hide();
        if (m_inspectorStack->currentIndex() == 3) showTab(0);
        m_selectedShot = 0;
        m_maxShotId = 0;
        m_preview->setShot(Screenshot{});
        return;
    }

    // De qué ronda es esta ejecución y si continúa otra: lo primero que hay que saber al llegar aquí,
    // porque cambia lo que se espera de ella (no se prueba el plan entero, sino lo que se rompió).
    if (const PlanRun* cycle = m_run.planRunId().isEmpty() ? nullptr : m_history.findPlan(m_run.planRunId())) {
        QStringList parts;
        if (cycle->revision > 0) parts << tr("revisión %1").arg(cycle->revision);
        if (!cycle->environment.trimmed().isEmpty()) parts << cycle->environment.trimmed();
        QString text;
        if (cycle->isContinuation()) {
            text = tr("<b>CONTINUANDO LA %1</b> · se repiten sólo los casos que fallaron o quedaron bloqueados en el ciclo %2")
                       .arg(parts.isEmpty() ? tr("ronda de pruebas") : tr("REVISIÓN %1").arg(cycle->revision), cycle->continuesCycleId);
            if (!cycle->environment.trimmed().isEmpty()) text += tr(" · ambiente %1").arg(cycle->environment.trimmed());
            if (!m_run.continuesRunId().isEmpty())
                text += tr("<br>Este caso se retoma en el paso que se rompió; los anteriores vienen de la ejecución %1.")
                            .arg(m_run.continuesRunId());
        } else if (!parts.isEmpty()) {
            text = tr("Ciclo %1 · %2").arg(cycle->id, parts.join(QStringLiteral(" · ")));
        }
        m_continuationText->setText(text);
        m_continuationText->setStyleSheet(QStringLiteral("font-size:11.5px;color:%1;")
                                              .arg(cycle->isContinuation() ? theme::Amber : theme::Muted));
        m_continuation->setVisible(!text.isEmpty() && !m_focusMode);
    }

    if (!m_clock.isActive()) m_clock.start();
    // El título entero va en el tooltip, con la suite y el componente: en la barra cabe en una línea.
    m_caseTitle->setFullText(c->title);
    QStringList meta{c->id};
    if (!c->suite.isEmpty()) meta << c->suite;
    if (!c->component.isEmpty()) meta << c->component;
    m_caseTitle->setToolTip(QStringLiteral("%1\n%2").arg(meta.join(QStringLiteral(" · ")), c->title));

    const int total = c->steps.size();
    if (active) {
        const QString stateColor = paused ? theme::Amber : theme::Green;
        m_stateDot->setStyleSheet(QStringLiteral("background:%1;border-radius:4px;").arg(stateColor));
        m_stateText->setText(paused ? tr("%1 · EN PAUSA").arg(c->id) : tr("%1 · EJECUTANDO").arg(c->id));
        m_stateText->setStyleSheet(QStringLiteral("color:%1;").arg(stateColor));
        // Un paso ya marcado se puede volver a ver (y a marcar): el rótulo lo dice.
        m_stepCounter->setText(r.isMarked(r.idx)
                                   ? tr("PASO %1 DE %2 · %3").arg(r.idx + 1).arg(total).arg(resultLabel(r.results[r.idx].result))
                                   : tr("PASO %1 DE %2 · ACCIÓN").arg(r.idx + 1).arg(total));
        m_stepCounter->setStyleSheet(QStringLiteral("color:%1;").arg(r.isMarked(r.idx) ? resultColor(r.results[r.idx].result) : theme::Muted));
        m_action->setText(c->steps[r.idx].action);
        m_action->setStyleSheet(QStringLiteral("font-size:15.5px;font-weight:800;color:%1;").arg(theme::Text));
        const QString data = c->steps[r.idx].data.trimmed();
        m_data->setText(data);
        m_dataBlock->setVisible(!data.isEmpty());
        m_expectedTitle->setText(tr("RESULTADO ESPERADO"));
        const QString expected = c->steps[r.idx].expected.trimmed();
        m_expected->setText(expected.isEmpty() ? tr("(sin resultado esperado)") : expected);
        m_note->setTextSilently(r.note);
        m_note->setPlaceholderText(tr("Observaciones del paso %1…").arg(r.idx + 1));
        m_back->setEnabled(m_run.canGoBack());
        m_next->setEnabled(m_run.canGoNext());
        m_finish->setText(tr("Cerrar ejecución"));
        m_focusCounter->setText(paused ? tr("EN PAUSA · PASO %1 / %2").arg(r.idx + 1).arg(total)
                                       : tr("PASO %1 / %2").arg(r.idx + 1).arg(total));
        m_focusAction->setFullText(c->steps[r.idx].action);
    } else {
        const Verdict v = r.verdict();
        const QString color = v == Verdict::Bloqueado ? theme::Amber : v == Verdict::Fallido ? theme::Red : theme::Green;
        const QString text = v == Verdict::Bloqueado ? tr("Bloqueado") : v == Verdict::Fallido ? tr("Fallido") : tr("Superado");
        m_stateDot->setStyleSheet(QStringLiteral("background:%1;border-radius:4px;").arg(color));
        m_stateText->setText(tr("%1 · TERMINADA").arg(c->id));
        m_stateText->setStyleSheet(QStringLiteral("color:%1;").arg(color));
        m_stepCounter->setText(tr("EJECUCIÓN TERMINADA"));
        m_stepCounter->setStyleSheet(QStringLiteral("color:%1;").arg(theme::Muted));
        m_stepClock->clear();
        m_back->setEnabled(m_run.canGoBack());   // reabre el último paso
        m_next->setEnabled(false);
        m_action->setText(text);
        m_action->setStyleSheet(QStringLiteral("font-size:18px;font-weight:800;color:%1;").arg(color));
        QString summary = tr("%1 pasan · %2 fallan · %3 bloqueados")
                              .arg(r.count(StepResult::Pass)).arg(r.count(StepResult::Fail)).arg(r.count(StepResult::Block));
        if (r.count(StepResult::Skip) > 0) summary += tr(" · %1 N/A").arg(r.count(StepResult::Skip));
        summary += QStringLiteral(" · %1").arg(formatDuration(r.elapsedSecs()));
        m_dataBlock->hide();
        m_expectedTitle->setText(tr("RESUMEN"));
        m_expected->setText(summary);
        m_focusCounter->setText(tr("TERMINADA"));
        m_focusAction->setFullText(QStringLiteral("%1 · %2").arg(text, summary));
        m_reopen->setVisible(r.markedCount() > 0);
        m_finish->setText(m_run.queuedCount() > 0 ? tr("Siguiente caso · quedan %1").arg(m_run.queuedCount())
                          : !m_run.planRunId().isEmpty() ? tr("Terminar plan y ver informe")
                                                         : tr("Finalizar y volver"));
    }
    m_stepsTab->setText(tr("Pasos · %1").arg(total));

    // El bug se cuelga del paso con problema; un paso bloqueado pide un bug bloqueante, y el botón
    // pasa de tinte a rojo lleno para que se note.
    const int bugStep = bugStepIndex();
    const bool blocked = r.isMarked(bugStep) && r.results[bugStep].result == StepResult::Block;
    const QString bugName = blocked ? tr("Reportar bug bloqueante") : tr("Reportar bug");
    const QString bugTip = bugStep >= 0 && bugStep < total
                               ? tr("%1 · paso %2 · %3").arg(bugName).arg(bugStep + 1).arg(ui::elide(c->steps[bugStep].action, 40))
                               : tr("%1 de este caso").arg(bugName);
    for (auto* b : {m_reportBug, m_focusReportBug}) {
        b->setAccessibleName(bugName);
        b->setToolTip(bugTip);
        b->setIcon(QIcon(icons::pixmap(icons::Glyph::Bug, blocked ? QStringLiteral("#ffffff") : theme::RedSoft, 18)));
        b->setStyleSheet(QStringLiteral("QPushButton{padding:0;background:%1;border:1px solid %2;border-radius:8px;}"
                                        "QPushButton:hover{background:%3;}")
                             .arg(blocked ? theme::Red : theme::tint(theme::Red, 34), theme::tint(theme::Red, blocked ? 255 : 110),
                                  blocked ? theme::Red : theme::tint(theme::Red, 70)));
    }

    tick();
    refreshSteps();
    refreshShots();
    refreshBugs();
    refreshCases();
}

int RunView::bugStepIndex() const {
    const RunState& r = m_run.state();
    if (r.caseId.isEmpty()) return -1;
    const int broken = r.reportableStepIndex();
    return broken >= 0 ? broken : r.idx;
}

QString RunView::stepColor(int index, const RunState& r) const {
    if (r.isMarked(index)) return resultColor(r.results[index].result);
    return index == r.idx && !r.finished ? theme::Blue : theme::Border;
}

void RunView::refreshSteps() {
    ui::clearLayout(m_stepsLayout);
    ui::clearLayout(m_chipsLayout);
    const RunState& r = m_run.state();
    const TestCase* c = m_cases.find(r.caseId);
    if (!c) return;
    QWidget* currentCard = nullptr;
    for (int i = 0; i < c->steps.size(); ++i) {
        m_chipsLayout->addWidget(stepChip(i, *c, r));
        QWidget* card = stepCard(i, *c, r);
        if (i == r.idx) currentCard = card;
        m_stepsLayout->addWidget(card);
    }
    m_stepsLayout->addStretch(1);
    // En un caso largo el paso activo puede quedar fuera de la lista visible: se trae a la vista.
    if (currentCard) {
        QTimer::singleShot(0, this, [this, card = QPointer<QWidget>(currentCard)]() {
            if (!card) return;
            m_stepsScroll->widget()->layout()->activate();
            m_stepsScroll->ensureWidgetVisible(card, 0, 8);
        });
    }
}

QPushButton* RunView::stepChip(int index, const TestCase& c, const RunState& r) {
    const bool current = index == r.idx && !r.finished;
    auto* chip = new QPushButton(QStringLiteral("%1").arg(index + 1, 2, 10, QLatin1Char('0')));
    chip->setObjectName(QStringLiteral("stepChip%1").arg(index + 1));
    chip->setCursor(Qt::PointingHandCursor);
    chip->setFixedSize(42, 30);
    chip->setToolTip(tr("Paso %1 · %2").arg(index + 1).arg(c.steps[index].action));
    chip->setStyleSheet(QStringLiteral("QPushButton{background:%1;color:%2;border:1px solid %3;border-bottom:3px solid %4;border-radius:6px;"
                                       "font-family:'Consolas','DejaVu Sans Mono',monospace;font-size:11px;font-weight:800;padding:0;}"
                                       "QPushButton:hover{background:%5;}")
                            .arg(current ? theme::Elevated : QStringLiteral("transparent"), current ? theme::Text : theme::TextSoft,
                                 current ? theme::Blue : theme::Border, stepColor(index, r), theme::Elevated));
    connect(chip, &QPushButton::clicked, this, [this, index]() { m_run.goTo(index); });
    return chip;
}

QWidget* RunView::stepCard(int index, const TestCase& c, const RunState& r) {
    const bool done = r.isMarked(index);
    const bool current = index == r.idx && !r.finished;

    auto* card = ui::card("step-card");
    ui::setFlag(card, "active", current);
    // La lista es el mando de la ejecución: un clic pone ese paso en pantalla, esté marcado o no.
    card->setObjectName(QStringLiteral("stepCard%1").arg(index + 1));
    card->setProperty("stepIndex", index);
    card->setCursor(Qt::PointingHandCursor);
    card->setAttribute(Qt::WA_Hover);
    card->setToolTip(tr("Ir al paso %1").arg(index + 1));
    card->installEventFilter(this);
    auto* h = ui::hbox(card, 0, 0);
    auto* bar = ui::accentBar(stepColor(index, r));
    bar->setFixedWidth(3);
    h->addWidget(bar);
    auto* body = new QWidget;
    auto* v = ui::vbox(body, 0, 4);
    v->setContentsMargins(10, 8, 8, 8);

    // El número con lo que ya tiene (capturas, bugs, veredicto) y la acción entera debajo.
    auto* head = new QWidget;
    auto* hh = ui::hbox(head, 0, 6);
    hh->addWidget(ui::label(tr("PASO %1").arg(index + 1, 2, 10, QLatin1Char('0')), "eyebrow"));
    int shots = 0;
    for (const auto& s : c.shots) if (s.step == index + 1 && s.runId.isEmpty()) ++shots;
    if (shots > 0) {
        auto* evidence = ui::label(shots == 1 ? tr("· 1 captura") : tr("· %1 capturas").arg(shots), "muted-sm");
        evidence->setStyleSheet(QStringLiteral("font-size:11px;"));
        hh->addWidget(evidence);
    }
    // Cada bug pertenece a un paso de esta ejecución: el suyo lo enseña aquí, para no reportarlo dos veces.
    QStringList keys;
    for (const auto& b : bugsOfRun()) if (b.step == index + 1) keys << b.key;
    if (!keys.isEmpty()) {
        auto* bug = ui::pill(keys.join(QStringLiteral(" · ")), theme::tint(theme::Red, 38), theme::Red);
        bug->setStyleSheet(bug->styleSheet() + QStringLiteral("font-size:10px;font-weight:700;"));
        bug->setToolTip(keys.size() == 1 ? tr("Bug reportado en este paso") : tr("Bugs reportados en este paso"));
        hh->addWidget(bug);
    }
    hh->addStretch(1);
    if (done) {
        // El veredicto se puede corregir desde su propia pastilla.
        const StepResult res = r.results[index].result;
        if (current) hh->addWidget(statePill(tr("ACTIVO"), theme::Blue));
        // Lo que viene de la ejecución que se retoma no se ha vuelto a probar: conviene que se note.
        if (r.results[index].inherited) {
            auto* pill = statePill(tr("ANTERIOR"), theme::Muted);
            pill->setToolTip(tr("Viene de la ejecución que se está continuando: este paso no se ha repetido"));
            hh->addWidget(pill);
        }
        auto* badge = ui::button(resultLabel(res), "chip");
        badge->setStyleSheet(QStringLiteral("padding:1px 7px;font-size:10px;font-weight:800;background:%1;color:%2;border-color:%1;")
                                 .arg(resultColor(res), res == StepResult::Fail ? QStringLiteral("#ffffff") : theme::Bg));
        badge->setToolTip(tr("Cambiar el veredicto de este paso"));
        connect(badge, &QPushButton::clicked, this, [this, index, badge]() {
            QMenu menu(this);
            for (StepResult alt : kAllResults)
                menu.addAction(label(alt), this, [this, index, alt]() { m_run.setResult(index, alt); });
            menu.exec(badge->mapToGlobal(badge->rect().bottomLeft()));
        });
        hh->addWidget(badge);
    } else {
        hh->addWidget(statePill(current ? tr("ACTIVO") : tr("PENDIENTE"), current ? theme::Blue : theme::Muted));
    }
    v->addWidget(head);

    auto* action = new QLabel(c.steps[index].action);
    action->setWordWrap(true);
    action->setStyleSheet(QStringLiteral("font-size:12.5px;font-weight:%1;color:%2;")
                              .arg(current ? 700 : 400).arg(current ? theme::Text : theme::TextSoft));
    v->addWidget(action);
    h->addWidget(body, 1);
    return card;
}

void RunView::refreshShots() {
    ui::clearLayout(m_shotsLayout);
    const TestCase* c = m_cases.find(m_run.state().caseId);
    if (!c) return;
    // Sólo las de esta ejecución: las de las anteriores están en su ficha del historial.
    const QList<Screenshot> shots = c->shotsOfRun(QString());
    m_shotsCount->setText(shots.isEmpty() ? tr("CAPTURAS") : tr("CAPTURAS · %1").arg(shots.size()));

    // La captura recién hecha se abre sola en el visor; si la elegida ya no está, la última.
    int maxId = 0;
    bool selectionExists = false;
    for (const auto& s : shots) {
        maxId = std::max(maxId, s.id);
        if (s.id == m_selectedShot) selectionExists = true;
    }
    if (maxId > m_maxShotId) m_selectedShot = maxId;
    else if (!selectionExists) m_selectedShot = shots.isEmpty() ? 0 : shots.last().id;
    m_maxShotId = maxId;

    const QString id = c->id;
    QWidget* selectedCard = nullptr;
    // En la tira van en el orden del caso; cada miniatura lleva el paso al que pertenece, y
    // «Ordenar por paso» las agrupa cuando hay de más de uno.
    QList<int> groups;
    for (const auto& shot : shots) {
        const int step = shot.step > 0 ? shot.step : 0;
        if (!groups.contains(step)) groups << step;
    }
    if (shots.isEmpty()) {
        auto* hint = ui::label(tr("Aún no hay capturas en esta ejecución.\nPulsa «Capturar» o arrastra un fichero a la ventana."), "muted-sm");
        m_shotsLayout->addWidget(hint);
    }
    int position = 0;
    for (int i = 0; i < shots.size(); ++i) {
        const Screenshot& shot = shots[i];
        auto* card = new ShotCard(shot, c->steps, ShotCard::Layout::Film);
        card->setFixedWidth(kFilmCardWidth);
        card->setThumbWidthHint(kFilmCardWidth - 2);
        card->setSelected(shot.id == m_selectedShot);
        if (shot.id == m_selectedShot) {
            selectedCard = card;
            position = i + 1;
        }
        connect(card, &ShotCard::selectRequested, this, [this](int shotId) { selectShot(shotId); });
        connect(card, &ShotCard::moveRequested, this, [this, id](int shotId, int delta) { m_cases.moveShot(id, shotId, delta); });
        connect(card, &ShotCard::removeRequested, this, [this, id](int shotId) { m_cases.removeShot(id, shotId); });
        evidence::wireCard(card, this, m_cases, m_evidence, id);
        m_shotsLayout->addWidget(card, 0, Qt::AlignTop);
    }
    m_shotsLayout->addStretch(1);
    m_groupedShots = groups.size() > 1;
    m_sortShots->setVisible(m_groupedShots);
    // Una captura nueva se añade al final de la tira, fuera de la parte visible: hay que traerla a
    // la vista. En diferido y forzando la colocación, porque las tarjetas acaban de crearse: hasta
    // que el layout no se activa y el contenido no toma su tamaño, el área ni siquiera tiene rango.
    if (selectedCard) {
        QTimer::singleShot(0, this, [this, card = QPointer<QWidget>(selectedCard)]() {
            if (!card) return;
            m_filmScroll->widget()->layout()->activate();
            m_filmScroll->widget()->adjustSize();
            m_filmScroll->ensureWidgetVisible(card, 12, 0);
        });
    }

    // Visor y barra de la evidencia elegida
    const Screenshot* shot = selectedShot();
    m_preview->setShot(shot ? *shot : Screenshot{});
    m_preview->setPlaceholder(tr("Aún no hay evidencias de este caso.\nPulsa «Capturar» o arrastra un fichero a la ventana."));
    for (auto* w : m_shotControls) w->setEnabled(shot != nullptr);
    m_shotPrev->setVisible(shots.size() > 1);
    m_shotNext->setVisible(shots.size() > 1);
    m_shotCounter->setVisible(shot != nullptr);
    m_shotCounter->setText(QStringLiteral("%1 / %2").arg(position).arg(shots.size()));
    m_shotCounter->adjustSize();
    placeViewerOverlay();
    m_shotStep->setVisible(shot != nullptr);
    m_shotName->setFullText(shot ? shot->fileName : QString());
    m_selfEdit = true;
    m_assign->clear();
    if (!shot) {
        m_selfEdit = false;
        return;
    }
    // La etiqueta del paso lleva el color de su veredicto: de un vistazo, si es la prueba de un fallo.
    const RunState& r = m_run.state();
    const QString stepColorName = shot->step > 0 ? stepColor(shot->step - 1, r) : theme::Amber;
    m_shotStep->setText(shot->step > 0 ? tr("PASO %1").arg(shot->step) : tr("SIN PASO"));
    m_shotStep->setStyleSheet(QStringLiteral("background:%1;color:%2;border:1px solid %3;border-radius:4px;padding:2px 8px;"
                                             "font-size:11px;font-weight:800;")
                                  .arg(theme::tint(stepColorName, 40), stepColorName == theme::Border ? theme::TextSoft : stepColorName,
                                       theme::tint(stepColorName, 110)));
    m_assign->addItem(tr("Sin asignar"), 0);
    for (int i = 0; i < c->steps.size(); ++i)
        m_assign->addItem(tr("Paso %1 · %2").arg(i + 1).arg(ui::elide(c->steps[i].action, 48)), i + 1);
    m_assign->setCurrentIndex(std::max(0, m_assign->findData(shot->step)));
    m_selfEdit = false;
}

void RunView::placeViewerOverlay() {
    const int y = (m_preview->height() - m_shotPrev->height()) / 2;
    m_shotPrev->move(12, y);
    m_shotNext->move(m_preview->width() - m_shotNext->width() - 12, y);
    m_shotCounter->move(m_preview->width() - m_shotCounter->width() - 12, m_preview->height() - m_shotCounter->height() - 10);
}

QList<IssueLink> RunView::bugsOfRun() const {
    const RunState& r = m_run.state();
    if (r.caseId.isEmpty()) return {};
    QList<IssueLink> out;
    for (const auto& bug : m_bugs.issues()) {
        // Lo normal: el bug dice de qué ejecución salió. Una sesión guardada antes de que se anotara
        // no lo sabe, y entonces cuentan los de este caso reportados desde que arrancó.
        const bool mine = !bug.runId.trimmed().isEmpty()
                              ? bug.runId == r.runId
                              : bug.caseId == r.caseId && r.startedAt.isValid() && bug.createdAt.isValid()
                                    && bug.createdAt >= r.startedAt;
        if (mine) out.prepend(bug);   // el libro va del primero al último: aquí, el más reciente arriba
    }
    return out;
}

void RunView::refreshBugs() {
    ui::clearLayout(m_bugsLayout);
    const TestCase* c = m_cases.find(m_run.state().caseId);
    if (!c) {
        m_bugsTab->setText(tr("Bugs"));
        return;
    }
    // Los bugs de esta ejecución, del más reciente al primero, agrupados por su paso.
    const QList<IssueLink> bugs = bugsOfRun();
    const int open = std::count_if(bugs.cbegin(), bugs.cend(), [](const IssueLink& b) { return !b.resolved; });
    m_bugsTab->setText(bugs.isEmpty() ? tr("Bugs") : tr("Bugs · %1").arg(bugs.size()));
    m_bugsTab->setToolTip(bugs.isEmpty() ? tr("Los bugs reportados en esta ejecución, con el paso del que salieron")
                                         : tr("%1 bug(s) de esta ejecución · %2 sin cerrar").arg(bugs.size()).arg(open));
    if (bugs.isEmpty()) {
        m_bugsEmpty = ui::label(tr("Todavía no se ha reportado ningún bug en esta ejecución.\nAl reportar uno queda aquí, con el paso del que salió."), "muted-sm");
        m_bugsEmpty->setWordWrap(true);
        m_bugsLayout->addWidget(m_bugsEmpty);
        m_bugsLayout->addStretch(1);
        return;
    }

    QList<int> groups;
    for (const auto& bug : bugs) {
        const int step = bug.step > 0 ? bug.step : 0;
        if (!groups.contains(step)) groups << step;
    }
    std::sort(groups.begin(), groups.end(), [](int a, int b) { return a != 0 && (b == 0 || a < b); });
    for (int step : groups) {
        m_bugsLayout->addWidget(stepGroupHeader(step, *c));
        for (const auto& bug : bugs) {
            if ((bug.step > 0 ? bug.step : 0) != step) continue;
            auto* card = ui::button(QString(), "row");
            card->setObjectName(QStringLiteral("runBug-%1").arg(bug.key));
            card->setToolTip(tr("Abrir la ficha del bug en otra ventana"));
            auto* bvl = ui::vbox(card, 0, 4);
            bvl->setContentsMargins(10, 8, 10, 8);
            auto* top = new QWidget;
            auto* tph = ui::hbox(top, 0, 6);
            tph->addWidget(ui::label(bug.key, "mono-muted"));
            const QString color = bug.resolved ? theme::Green : theme::Amber;
            tph->addWidget(ui::pill(bug.resolved ? tr("CERRADO") : tr("ABIERTO"), theme::tint(color, 34), color));
            tph->addStretch(1);
            tph->addWidget(ui::label(BugReport::classificationLabel(bug.classification), "muted-sm"));
            bvl->addWidget(top);
            auto* title = new QLabel(bug.title.isEmpty() ? tr("(sin título)") : bug.title);
            title->setWordWrap(true);
            title->setStyleSheet(QStringLiteral("font-size:12.5px;color:%1;").arg(theme::Text));
            bvl->addWidget(title);
            for (auto* child : card->findChildren<QWidget*>()) child->setAttribute(Qt::WA_TransparentForMouseEvents);
            connect(card, &QPushButton::clicked, this, [this, key = bug.key]() { openBug(key); });
            m_bugsLayout->addWidget(card);
        }
    }
    m_bugsLayout->addStretch(1);
}

void RunView::openBug(const QString& key) {
    // Una ventana por bug: volver a pulsarlo trae la que ya está abierta en vez de apilar copias.
    if (auto* open = m_bugWindows.value(key).data()) {
        if (const IssueLink* bug = m_bugs.findIssue(key)) open->setBug(*bug);
        open->show();
        open->raise();
        open->activateWindow();
        return;
    }
    const IssueLink* bug = m_bugs.findIssue(key);
    if (!bug) return;
    const TestCase* c = m_cases.find(bug->caseId);
    const QString stepAction = c && bug->step > 0 && bug->step <= c->steps.size() ? c->steps[bug->step - 1].action : QString();
    auto* window = new BugDetailWindow(*bug, c ? c->title : QString(), stepAction, this);
    window->setAttribute(Qt::WA_DeleteOnClose);
    connect(window, &BugDetailWindow::openUrlRequested, this, &RunView::openUrlRequested);
    m_bugWindows.insert(key, window);
    window->show();
}

void RunView::refreshCases() {
    ui::clearLayout(m_casesLayout);
    const QStringList cases = m_run.planCases();
    m_casesTab->setVisible(!cases.isEmpty());
    if (cases.isEmpty()) {
        if (m_inspectorStack->currentIndex() == 3) showTab(0);
        return;
    }
    // Lo que ya se archivó en este ciclo, con su veredicto: la última ejecución de cada caso.
    QHash<QString, Verdict> archived;
    for (const auto& run : m_history.runsForPlan(m_run.planRunId())) archived.insert(run.caseId, run.verdict);
    int done = 0;
    for (const auto& id : cases) if (archived.contains(id) && id != m_run.state().caseId && !m_run.isQueued(id)) ++done;
    m_casesTab->setText(tr("Casos · %1/%2").arg(done).arg(cases.size()));

    QWidget* currentCard = nullptr;
    for (const auto& id : cases) {
        QWidget* card = caseCard(id, archived);
        if (id == m_run.state().caseId) currentCard = card;
        m_casesLayout->addWidget(card);
    }
    m_casesLayout->addStretch(1);
    if (currentCard) {
        QTimer::singleShot(0, this, [this, card = QPointer<QWidget>(currentCard)]() {
            if (!card) return;
            m_casesScroll->widget()->layout()->activate();
            m_casesScroll->ensureWidgetVisible(card, 0, 8);
        });
    }
}

QWidget* RunView::caseCard(const QString& caseId, const QHash<QString, Verdict>& archived) {
    const TestCase* c = m_cases.find(caseId);
    const bool current = caseId == m_run.state().caseId;
    const bool queued = m_run.isQueued(caseId);
    const RunState* parked = m_run.parkedRun(caseId);
    const bool isArchived = !current && !queued && archived.contains(caseId);
    // Lo que se enseña de cada uno: el activo, el que se dejó a medias, el ya archivado con su
    // veredicto o el que todavía no se ha empezado.
    QString state;
    QString color;
    if (current) {
        state = tr("ACTIVO");
        color = theme::Blue;
    } else if (parked) {
        state = tr("EN PAUSA · %1/%2").arg(parked->markedCount()).arg(parked->results.size());
        color = theme::TextSoft;
    } else if (isArchived) {
        const Verdict v = archived.value(caseId);
        state = v == Verdict::Bloqueado ? tr("BLOQUEADO") : v == Verdict::Fallido ? tr("FALLIDO") : tr("SUPERADO");
        color = v == Verdict::Bloqueado ? theme::Amber : v == Verdict::Fallido ? theme::Red : theme::Green;
    } else {
        state = queued ? tr("PENDIENTE") : tr("SIN EJECUTAR");
        color = theme::Muted;
    }
    const bool reachable = queued && c;

    auto* card = ui::card("step-card");
    ui::setFlag(card, "active", current);
    card->setObjectName(QStringLiteral("caseCard-%1").arg(caseId));
    if (reachable) {
        card->setProperty("caseId", caseId);
        card->setCursor(Qt::PointingHandCursor);
        card->setAttribute(Qt::WA_Hover);
        card->setToolTip(parked ? tr("Volver a este caso, donde se dejó") : tr("Ir a este caso; el actual queda en pausa"));
        card->installEventFilter(this);
    } else if (isArchived) {
        card->setToolTip(tr("Ya archivado en el historial de este ciclo"));
    }
    auto* h = ui::hbox(card, 0, 0);
    auto* bar = ui::accentBar(current ? theme::Blue : isArchived ? color : parked ? theme::TextSoft : theme::Border);
    bar->setFixedWidth(3);
    h->addWidget(bar);
    auto* body = new QWidget;
    auto* v = ui::vbox(body, 0, 4);
    v->setContentsMargins(10, 8, 8, 8);
    auto* head = new QWidget;
    auto* hh = ui::hbox(head, 0, 6);
    hh->addWidget(ui::label(caseId, "eyebrow"));
    hh->addStretch(1);
    hh->addWidget(statePill(state, color));
    v->addWidget(head);
    auto* title = new QLabel(c ? c->title : tr("(caso eliminado)"));
    title->setWordWrap(true);
    title->setStyleSheet(QStringLiteral("font-size:12.5px;font-weight:%1;color:%2;")
                             .arg(current ? 700 : 400).arg(current ? theme::Text : isArchived ? theme::Muted : theme::TextSoft));
    v->addWidget(title);
    h->addWidget(body, 1);
    return card;
}

const Screenshot* RunView::selectedShot() const {
    const TestCase* c = m_cases.find(m_run.state().caseId);
    if (!c) return nullptr;
    for (const auto& s : c->shots) if (s.id == m_selectedShot) return &s;
    return nullptr;
}

void RunView::selectShot(int shotId) {
    if (shotId == m_selectedShot) return;
    m_selectedShot = shotId;
    refreshShots();
}

void RunView::openSelectedShot() {
    if (m_selectedShot) evidence::openViewer(this, m_cases, m_evidence, m_run.state().caseId, m_selectedShot);
}

void RunView::annotateSelectedShot() {
    const Screenshot* shot = selectedShot();
    if (!shot || !shot->isImage() || shot->isAnimation()) {
        emit toast(tr("Sólo se pueden anotar imágenes"), theme::Amber);
        return;
    }
    if (evidence::annotate(this, m_cases, m_evidence, m_run.state().caseId, m_selectedShot)) m_preview->reload();
}

bool RunView::eventFilter(QObject* watched, QEvent* event) {
    if (watched == m_preview && event->type() == QEvent::Resize) placeViewerOverlay();
    if (event->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        const QVariant caseId = watched->property("caseId");
        if (caseId.isValid()) {
            // Ir a otro caso lleva a su ficha, como elegir un paso: es lo que se va a probar ahora.
            // En diferido: la tarjeta pulsada se destruye al refrescar la lista.
            QTimer::singleShot(0, this, [this, id = caseId.toString()]() {
                if (m_run.goToCase(id)) showTab(0);
            });
            return true;
        }
        const QVariant step = watched->property("stepIndex");
        if (step.isValid()) {
            // Elegir un paso de la lista lleva a su ficha: es lo que se quiere leer a continuación.
            m_run.goTo(step.toInt());
            showTab(0);
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void RunView::selectRelativeShot(int delta) {
    const TestCase* c = m_cases.find(m_run.state().caseId);
    if (!c || c->shots.isEmpty()) return;
    int index = 0;
    for (int i = 0; i < c->shots.size(); ++i) if (c->shots[i].id == m_selectedShot) index = i;
    selectShot(c->shots[std::clamp(index + delta, 0, static_cast<int>(c->shots.size()) - 1)].id);
}

} // namespace qaflow
