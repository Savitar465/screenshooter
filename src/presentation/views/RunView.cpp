#include "RunView.h"

#include "application/EvidenceService.h"
#include "application/RunController.h"
#include "application/SettingsStore.h"
#include "application/TestCaseStore.h"
#include "core/models/RunHistory.h"   // formatDuration
#include "presentation/theme/Theme.h"
#include "presentation/widgets/EvidenceActions.h"
#include "presentation/widgets/EvidencePreview.h"
#include "presentation/widgets/ProgressCells.h"
#include "presentation/widgets/ShotCard.h"
#include "presentation/widgets/TextArea.h"
#include "presentation/widgets/Ui.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>

namespace qaflow {

namespace {

constexpr int kCasePanelWidth = 288;
constexpr int kFilmPanelWidth = 296;

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

/// Botón de veredicto: rótulo + tecla rápida dentro del botón.
/// El color se fija de forma explícita porque las etiquetas hijas no heredan el `color`
/// que el QSS aplica al botón (`color: inherit` no existe en las hojas de estilo de Qt).
QPushButton* verdictButton(const QString& text, const QString& key, const char* role, const QString& fg) {
    auto* b = ui::button(QString(), role);
    b->setMinimumWidth(72);
    auto* h = ui::hbox(b, 0, 6);
    h->setContentsMargins(12, 9, 12, 9);
    h->addStretch(1);
    auto* t = new QLabel(text);
    t->setStyleSheet(QStringLiteral("font-weight:800;font-size:13.5px;color:%1;background:transparent;").arg(fg));
    auto* k = new QLabel(key);
    k->setStyleSheet(QStringLiteral("font-weight:600;font-size:11.5px;color:%1;background:transparent;").arg(theme::tint(fg, 175)));
    h->addWidget(t);
    h->addWidget(k);
    h->addStretch(1);
    for (auto* c : b->findChildren<QWidget*>()) c->setAttribute(Qt::WA_TransparentForMouseEvents);
    return b;
}

/// Pastilla de estado de un paso: veredicto, «ACTIVO» o «PENDIENTE».
QLabel* statePill(const QString& text, const QString& color) {
    auto* l = ui::pill(text, theme::tint(color, 38), color);
    l->setStyleSheet(l->styleSheet() + QStringLiteral("font-size:10px;font-weight:800;border:1px solid %1;").arg(theme::tint(color, 110)));
    return l;
}

} // namespace

RunView::RunView(TestCaseStore& cases, RunController& run, SettingsStore& settings, EvidenceService& evidence, QWidget* parent)
    : QWidget(parent), m_cases(cases), m_run(run), m_settings(settings), m_evidence(evidence) {
    auto* root = ui::hbox(this, 0, 0);
    root->addWidget(buildCasePanel());
    root->addWidget(buildStepPanel(), 1);
    root->addWidget(buildFilmPanel());

    // Atajos de teclado (sólo mientras esta vista es visible y el foco no está en un campo de texto)
    for (auto [key, res] : {std::pair{Qt::Key_P, StepResult::Pass}, std::pair{Qt::Key_F, StepResult::Fail},
                            std::pair{Qt::Key_B, StepResult::Block}, std::pair{Qt::Key_S, StepResult::Skip}}) {
        auto* sc = new QShortcut(QKeySequence(key), this);
        sc->setContext(Qt::WindowShortcut);
        connect(sc, &QShortcut::activated, this, [this, res]() { if (isVisible() && m_run.isRunning()) m_run.mark(res); });
    }
    auto* backKey = new QShortcut(QKeySequence(Qt::Key_Backspace), this);
    backKey->setContext(Qt::WindowShortcut);
    connect(backKey, &QShortcut::activated, this, [this]() { if (isVisible() && !m_run.state().caseId.isEmpty()) m_run.back(); });

    connect(&m_run, &RunController::runChanged, this, &RunView::refresh);
    connect(&m_cases, &TestCaseStore::caseChanged, this, [this](const QString& id) { if (id == m_run.state().caseId) refresh(); });
    connect(&m_settings, &SettingsStore::captureChanged, this, &RunView::refresh);
    m_clock.setInterval(1000);
    connect(&m_clock, &QTimer::timeout, this, &RunView::tick);
    refresh();
}

// ---- Columna del caso ------------------------------------------------------------------------

QWidget* RunView::buildCasePanel() {
    m_casePanel = ui::card("list-pane");
    m_casePanel->setObjectName(QStringLiteral("casePanel"));
    m_casePanel->setFixedWidth(kCasePanelWidth);
    auto* v = ui::vbox(m_casePanel, 0, 12);
    v->setContentsMargins(16, 18, 16, 16);

    auto* stateRow = new QWidget;
    auto* sh = ui::hbox(stateRow, 0, 7);
    m_stateDot = ui::dot(theme::Green, 7);
    sh->addWidget(m_stateDot);
    m_stateText = ui::label(QString(), "eyebrow");
    sh->addWidget(m_stateText, 1);
    v->addWidget(stateRow);

    m_caseTitle = new QLabel;
    m_caseTitle->setWordWrap(true);
    m_caseTitle->setStyleSheet(QStringLiteral("font-size:15px;font-weight:800;"));
    v->addWidget(m_caseTitle);
    m_caseMeta = ui::label(QString(), "muted-sm");
    m_caseMeta->setWordWrap(true);
    v->addWidget(m_caseMeta);

    m_progress = new ProgressCells;
    v->addWidget(m_progress);
    m_caseStats = ui::label(QString(), "muted-sm");
    m_caseStats->setWordWrap(true);
    v->addWidget(m_caseStats);

    auto* separator = new QFrame;
    separator->setFixedHeight(1);
    separator->setStyleSheet(QStringLiteral("background:%1;").arg(theme::Border));
    v->addWidget(separator);

    QWidget* list;
    QVBoxLayout* listLayout;
    auto* sa = ui::scrollArea(&list, &listLayout);
    m_stepsLayout = listLayout;
    m_stepsLayout->setSpacing(6);
    m_stepsLayout->addStretch(1);
    v->addWidget(sa, 1);

    m_finish = ui::button(tr("Cerrar ejecución"), "outline");
    m_finish->setStyleSheet(QStringLiteral("padding:10px;"));
    connect(m_finish, &QPushButton::clicked, this, [this]() {
        // Cerrarla a medias descarta los pasos sin marcar: se avisa antes.
        if (m_run.isRunning() &&
            QMessageBox::question(this, tr("Cerrar la ejecución"),
                                  tr("Quedan pasos sin marcar. Si la cierras ahora no se archivará en el historial.")) != QMessageBox::Yes)
            return;
        emit finishRequested();
    });
    v->addWidget(m_finish);
    return m_casePanel;
}

// ---- Columna del paso ------------------------------------------------------------------------

QWidget* RunView::buildStepPanel() {
    auto* panel = new QWidget;
    auto* v = ui::vbox(panel, 0, 14);
    v->setContentsMargins(24, 20, 24, 20);

    // Cabecera: paso actual (o veredicto final) y las acciones de la derecha.
    m_header = new QWidget;
    auto* hh = ui::hbox(m_header, 0, 20);
    auto* titleBlock = new QWidget;
    auto* tv = ui::vbox(titleBlock, 0, 4);
    auto* counterRow = new QWidget;
    auto* crh = ui::hbox(counterRow, 0, 10);
    m_stepCounter = ui::label(QString(), "eyebrow");
    crh->addWidget(m_stepCounter);
    m_stepClock = ui::label(QString(), "mono-muted");
    m_stepClock->setToolTip(tr("Tiempo en este paso"));
    crh->addWidget(m_stepClock);
    m_back = ui::button(tr("← Paso anterior"), "ghost");
    m_back->setToolTip(tr("Deshace el último veredicto y vuelve a ese paso (Retroceso)"));
    m_back->setStyleSheet(QStringLiteral("padding:2px 8px;font-size:11px;border-radius:7px;"));
    connect(m_back, &QPushButton::clicked, this, [this]() { m_run.back(); });
    crh->addWidget(m_back);
    crh->addStretch(1);
    tv->addWidget(counterRow);
    m_action = new QLabel;
    m_action->setWordWrap(true);
    m_action->setStyleSheet(QStringLiteral("font-size:22px;font-weight:800;"));
    tv->addWidget(m_action);
    m_expected = new QLabel;
    m_expected->setWordWrap(true);
    m_expected->setTextFormat(Qt::RichText);
    m_expected->setStyleSheet(QStringLiteral("font-size:13.5px;color:%1;").arg(theme::TextSoft));
    tv->addWidget(m_expected);
    hh->addWidget(titleBlock, 1);

    auto* actions = new QWidget;
    auto* av = ui::vbox(actions, 0, 8);
    m_verdicts = new QWidget;
    auto* vh = ui::hbox(m_verdicts, 0, 8);
    const struct { QString text; QString key; const char* role; QString fg; StepResult result; } verdicts[] = {
        {tr("Pasa"), tr("P"), "success", theme::OnAccent, StepResult::Pass},
        {tr("Falla"), tr("F"), "danger", QStringLiteral("#ffffff"), StepResult::Fail},
        {tr("Bloq."), tr("B"), "verdict-block", theme::AmberSoft, StepResult::Block},
        {tr("N/A"), tr("S"), "verdict-skip", theme::TextSoft, StepResult::Skip}};
    for (const auto& v2 : verdicts) {
        auto* b = verdictButton(v2.text, v2.key, v2.role, v2.fg);
        if (v2.result == StepResult::Skip) b->setToolTip(tr("Saltar: el paso no cuenta para el veredicto"));
        connect(b, &QPushButton::clicked, this, [this, r = v2.result]() { m_run.mark(r); });
        vh->addWidget(b);
    }
    av->addWidget(m_verdicts, 0, Qt::AlignRight);

    // Mismo hueco cuando la ejecución ya ha terminado.
    m_doneActions = new QWidget;
    auto* dh = ui::hbox(m_doneActions, 0, 8);
    m_reportBug = ui::button(tr("Reportar bug"), "danger");
    connect(m_reportBug, &QPushButton::clicked, this, &RunView::reportBugRequested);
    dh->addWidget(m_reportBug);
    m_reopen = ui::button(tr("← Último paso"), "outline");
    m_reopen->setToolTip(tr("Reabre el último paso para cambiar su veredicto"));
    connect(m_reopen, &QPushButton::clicked, this, [this]() { m_run.back(); });
    dh->addWidget(m_reopen);
    auto* repeat = ui::button(tr("Repetir"), "outline");
    repeat->setToolTip(tr("Vuelve a ejecutar el caso desde el primer paso"));
    connect(repeat, &QPushButton::clicked, this, [this]() { m_run.restart(); });
    dh->addWidget(repeat);
    av->addWidget(m_doneActions, 0, Qt::AlignRight);

    m_capture = ui::button(QString(), "primary");
    auto* ch = ui::hbox(m_capture, 0, 8);
    ch->setContentsMargins(14, 9, 14, 9);
    auto* icon = new QFrame;
    icon->setFixedSize(13, 13);
    icon->setStyleSheet(QStringLiteral("border:2px solid %1;border-radius:3px;background:transparent;").arg(theme::OnAccent));
    ch->addWidget(icon);
    auto* capText = new QLabel(tr("Capturar pantalla"));
    capText->setStyleSheet(QStringLiteral("font-weight:700;background:transparent;color:%1;").arg(theme::OnAccent));
    ch->addWidget(capText);
    m_captureShortcut = new QLabel;
    m_captureShortcut->setStyleSheet(QStringLiteral("font-size:11px;font-weight:700;background:transparent;color:%1;").arg(theme::tint(theme::OnAccent, 180)));
    ch->addWidget(m_captureShortcut);
    for (auto* c : m_capture->findChildren<QWidget*>()) c->setAttribute(Qt::WA_TransparentForMouseEvents);
    connect(m_capture, &QPushButton::clicked, this, &RunView::captureRequested);
    av->addWidget(m_capture, 0, Qt::AlignRight);
    hh->addWidget(actions, 0, Qt::AlignTop);
    v->addWidget(m_header);

    // Visor de la evidencia elegida, con su barra flotante.
    m_stage = new QWidget;
    auto* sg = new QGridLayout(m_stage);
    sg->setContentsMargins(0, 0, 0, 0);
    m_preview = new EvidencePreview;
    m_preview->setObjectName(QStringLiteral("evidencePreview"));
    connect(m_preview, &EvidencePreview::clicked, this, [this]() {
        evidence::openViewer(this, m_cases, m_evidence, m_run.state().caseId, m_selectedShot);
    });
    sg->addWidget(m_preview, 0, 0);

    m_shotBar = ui::card("card-flat");
    m_shotBar->setObjectName(QStringLiteral("shotBar"));
    m_shotBar->setStyleSheet(QStringLiteral("QFrame{background:%1;border:1px solid %2;border-radius:9px;}").arg(theme::Panel, theme::Border));
    auto* bh = ui::hbox(m_shotBar, 0, 6);
    bh->setContentsMargins(10, 6, 8, 6);
    bh->addWidget(ui::label(tr("ASIGNAR A"), "eyebrow"));
    m_assign = new QComboBox;
    m_assign->setObjectName(QStringLiteral("assignStep"));
    m_assign->setMinimumWidth(190);
    m_assign->setStyleSheet(QStringLiteral("QComboBox{background:%1;font-size:11.5px;}").arg(theme::Field));
    connect(m_assign, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_selfEdit || !m_selectedShot) return;
        m_cases.assignShotStep(m_run.state().caseId, m_selectedShot, m_assign->currentData().toInt());
    });
    bh->addWidget(m_assign);
    auto* prev = ui::button(QStringLiteral("◀"), "icon-move");
    prev->setObjectName(QStringLiteral("shotPrev"));
    prev->setToolTip(tr("Evidencia anterior"));
    connect(prev, &QPushButton::clicked, this, [this]() { selectRelativeShot(-1); });
    bh->addWidget(prev);
    auto* next = ui::button(QStringLiteral("▶"), "icon-move");
    next->setObjectName(QStringLiteral("shotNext"));
    next->setToolTip(tr("Evidencia siguiente"));
    connect(next, &QPushButton::clicked, this, [this]() { selectRelativeShot(+1); });
    bh->addWidget(next);
    auto* remove = ui::button(QStringLiteral("×"), "icon");
    remove->setToolTip(tr("Eliminar esta evidencia"));
    connect(remove, &QPushButton::clicked, this, [this]() {
        if (m_selectedShot) m_cases.removeShot(m_run.state().caseId, m_selectedShot);
    });
    bh->addWidget(remove);
    sg->addWidget(m_shotBar, 0, 0, Qt::AlignRight | Qt::AlignBottom);
    sg->setContentsMargins(0, 0, 14, 14);
    v->addWidget(m_stage, 1);

    m_note = new TextArea(2);
    m_note->setPlaceholderText(tr("Observaciones de este paso (opcional)…"));
    connect(m_note, &TextArea::edited, this, [this](const QString& t) { m_run.setNote(t); });
    v->addWidget(m_note);

    // Sin ejecución la columna se queda vacía: el mensaje ocupa su sitio.
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

// ---- Columna de capturas ---------------------------------------------------------------------

QWidget* RunView::buildFilmPanel() {
    m_filmPanel = ui::card("list-pane");
    m_filmPanel->setObjectName(QStringLiteral("filmPanel"));
    m_filmPanel->setFixedWidth(kFilmPanelWidth);
    auto* v = ui::vbox(m_filmPanel, 0, 10);
    v->setContentsMargins(16, 18, 16, 16);

    auto* head = new QWidget;
    auto* hh = ui::hbox(head, 0, 8);
    m_filmHeader = ui::label(tr("CAPTURAS"), "eyebrow");
    hh->addWidget(m_filmHeader, 1);
    m_sortShots = ui::button(tr("Ordenar por paso"), "outline");
    m_sortShots->setStyleSheet(QStringLiteral("padding:4px 8px;font-size:11px;border-radius:7px;"));
    connect(m_sortShots, &QPushButton::clicked, this, [this]() { m_cases.sortShotsByStep(m_run.state().caseId); });
    hh->addWidget(m_sortShots);
    v->addWidget(head);

    QWidget* list;
    QVBoxLayout* listLayout;
    m_filmScroll = ui::scrollArea(&list, &listLayout);
    m_filmScroll->setObjectName(QStringLiteral("filmScroll"));
    m_shotsLayout = listLayout;
    m_shotsLayout->setSpacing(10);
    m_shotsLayout->addStretch(1);
    v->addWidget(m_filmScroll, 1);

    auto* capture = ui::button(tr("+ Capturar"), "dashed");
    capture->setStyleSheet(QStringLiteral("padding:12px;font-size:12.5px;"));
    connect(capture, &QPushButton::clicked, this, &RunView::captureRequested);
    v->addWidget(capture);

    auto* more = new QWidget;
    auto* mh = ui::hbox(more, 0, 8);
    m_record = ui::button(tr("● GIF"), "dashed");
    m_record->setStyleSheet(QStringLiteral("padding:8px;font-size:12px;"));
    m_record->setToolTip(tr("Graba la pantalla o una región a GIF y la adjunta al caso"));
    m_record->setVisible(m_evidence.canRecord());
    connect(m_record, &QPushButton::clicked, this, [this]() { m_evidence.toggleRecording(); });
    connect(&m_evidence, &EvidenceService::recordingChanged, this, [this](bool on) { m_record->setText(on ? tr("■ Detener") : tr("● GIF")); });
    mh->addWidget(m_record, 1);
    auto* attach = ui::button(tr("+ Archivo"), "dashed");
    attach->setStyleSheet(QStringLiteral("padding:8px;font-size:12px;"));
    attach->setToolTip(tr("Adjunta logs, vídeos o imágenes existentes"));
    connect(attach, &QPushButton::clicked, this, [this]() { m_evidence.attachFiles(evidence::pickFiles(this)); });
    mh->addWidget(attach, 1);
    v->addWidget(more);
    return m_filmPanel;
}

// ---- Refresco --------------------------------------------------------------------------------

void RunView::tick() {
    const RunState& r = m_run.state();
    if (r.caseId.isEmpty()) { m_clock.stop(); return; }
    m_stepClock->setText(formatDuration(r.currentStepSecs()));
    const TestCase* c = m_cases.find(r.caseId);
    if (!c) return;
    m_caseStats->setText(tr("%1 de %2 pasos · %3 capturas · ⏱ %4")
                             .arg(r.results.size()).arg(c->steps.size()).arg(c->shots.size()).arg(formatDuration(r.elapsedSecs())));
}

void RunView::refresh() {
    const RunState& r = m_run.state();
    const TestCase* c = m_cases.find(r.caseId);
    m_captureShortcut->setText(m_settings.capture().shortcut);

    const bool hasRun = c != nullptr;
    const bool active = hasRun && !r.finished && !c->steps.isEmpty();
    m_casePanel->setVisible(hasRun);
    m_filmPanel->setVisible(hasRun);
    m_header->setVisible(hasRun);
    m_stage->setVisible(hasRun);   // el visor y su barra flotante sólo con ejecución
    m_note->setVisible(active);
    m_empty->setVisible(!hasRun);
    m_verdicts->setVisible(active);
    m_capture->setVisible(hasRun);
    m_doneActions->setVisible(hasRun && r.finished);
    if (!hasRun) {
        m_clock.stop();
        ui::clearLayout(m_stepsLayout);
        ui::clearLayout(m_shotsLayout);
        m_selectedShot = 0;
        m_maxShotId = 0;
        m_preview->setShot(Screenshot{});
        return;
    }

    if (!m_clock.isActive()) m_clock.start();
    m_caseTitle->setText(c->title);
    QStringList meta{c->id};
    if (!c->suite.isEmpty()) meta << c->suite;
    if (!c->component.isEmpty()) meta << c->component;
    m_caseMeta->setText(meta.join(QStringLiteral(" · ")));

    QStringList colors;
    for (int i = 0; i < c->steps.size(); ++i) {
        if (i < r.results.size()) colors << resultColor(r.results[i].result);
        else if (i == r.idx && !r.finished) colors << theme::Blue;
        else colors << theme::Border;
    }
    m_progress->setColors(colors);

    if (active) {
        m_stateDot->setStyleSheet(QStringLiteral("background:%1;border-radius:3px;").arg(theme::Green));
        m_stateDot->show();
        m_stateText->setText(tr("EJECUTANDO"));
        m_stateText->setStyleSheet(QStringLiteral("color:%1;").arg(theme::Green));
        m_stepCounter->setText(tr("PASO %1 DE %2").arg(r.idx + 1).arg(c->steps.size()));
        m_action->setText(c->steps[r.idx].action);
        m_expected->setText(tr("<b>Esperado:</b> %1").arg(c->steps[r.idx].expected.toHtmlEscaped()));
        m_note->setTextSilently(r.note);
        m_note->setPlaceholderText(tr("Observaciones del paso %1…").arg(r.idx + 1));
        m_back->setVisible(!r.results.isEmpty());
        m_finish->setText(tr("Cerrar ejecución"));
        m_action->setStyleSheet(QStringLiteral("font-size:22px;font-weight:800;color:%1;").arg(theme::Text));
    } else {
        const Verdict v = r.verdict();
        const QString color = v == Verdict::Bloqueado ? theme::Amber : v == Verdict::Fallido ? theme::Red : theme::Green;
        const QString text = v == Verdict::Bloqueado ? tr("Bloqueado") : v == Verdict::Fallido ? tr("Fallido") : tr("Superado");
        m_stateDot->setStyleSheet(QStringLiteral("background:%1;border-radius:3px;").arg(color));
        m_stateText->setText(tr("TERMINADA"));
        m_stateText->setStyleSheet(QStringLiteral("color:%1;").arg(color));
        m_stepCounter->setText(tr("EJECUCIÓN TERMINADA"));
        m_stepClock->clear();
        m_back->hide();
        m_action->setText(text);
        m_action->setStyleSheet(QStringLiteral("font-size:22px;font-weight:800;color:%1;").arg(color));
        QString summary = tr("%1 pasan · %2 fallan · %3 bloqueados")
                              .arg(r.count(StepResult::Pass)).arg(r.count(StepResult::Fail)).arg(r.count(StepResult::Block));
        if (r.count(StepResult::Skip) > 0) summary += tr(" · %1 N/A").arg(r.count(StepResult::Skip));
        summary += QStringLiteral(" · %1").arg(formatDuration(r.elapsedSecs()));
        m_expected->setText(summary);
        m_reportBug->setVisible(r.count(StepResult::Fail) > 0);
        m_reopen->setVisible(!r.results.isEmpty());
        m_finish->setText(m_run.queuedCount() > 0 ? tr("Siguiente caso · quedan %1").arg(m_run.queuedCount())
                          : !m_run.planRunId().isEmpty() ? tr("Terminar plan y ver informe")
                                                         : tr("Finalizar y volver"));
    }
    tick();
    refreshSteps();
    refreshShots();
}

void RunView::refreshSteps() {
    ui::clearLayout(m_stepsLayout);
    const RunState& r = m_run.state();
    const TestCase* c = m_cases.find(r.caseId);
    if (!c) return;
    for (int i = 0; i < c->steps.size(); ++i) m_stepsLayout->addWidget(stepCard(i, *c, r));
    m_stepsLayout->addStretch(1);
}

QWidget* RunView::stepCard(int index, const TestCase& c, const RunState& r) {
    const bool done = index < r.results.size();
    const bool current = !done && index == r.idx && !r.finished;
    const QString accent = done ? resultColor(r.results[index].result) : current ? theme::Blue : theme::Border;

    auto* card = ui::card("step-card");
    ui::setFlag(card, "active", current);
    auto* h = ui::hbox(card, 0, 0);
    auto* bar = ui::accentBar(accent);
    bar->setFixedWidth(3);
    h->addWidget(bar);
    auto* body = new QWidget;
    auto* v = ui::vbox(body, 0, 3);
    v->setContentsMargins(11, 9, 9, 9);

    auto* head = new QWidget;
    auto* hh = ui::hbox(head, 0, 8);
    hh->addWidget(ui::label(tr("PASO %1").arg(index + 1, 2, 10, QLatin1Char('0')), "eyebrow"), 1);
    if (done) {
        // El veredicto se puede corregir desde su propia pastilla.
        const StepResult res = r.results[index].result;
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

    int shots = 0;
    for (const auto& s : c.shots) if (s.step == index + 1) ++shots;
    auto* evidence = ui::label(shots == 0 ? tr("sin evidencia") : shots == 1 ? tr("1 captura") : tr("%1 capturas").arg(shots), "muted-sm");
    evidence->setStyleSheet(QStringLiteral("font-size:11px;"));
    v->addWidget(evidence);
    h->addWidget(body, 1);
    return card;
}

void RunView::refreshShots() {
    ui::clearLayout(m_shotsLayout);
    const TestCase* c = m_cases.find(m_run.state().caseId);
    if (!c) return;
    m_filmHeader->setText(tr("CAPTURAS · %1").arg(c->shots.size()));
    m_sortShots->setVisible(c->shots.size() > 1);

    // La captura recién hecha se abre sola en el visor; si la elegida ya no está, la última.
    int maxId = 0;
    bool selectionExists = false;
    for (const auto& s : c->shots) {
        maxId = std::max(maxId, s.id);
        if (s.id == m_selectedShot) selectionExists = true;
    }
    if (maxId > m_maxShotId) m_selectedShot = maxId;
    else if (!selectionExists) m_selectedShot = c->shots.isEmpty() ? 0 : c->shots.last().id;
    m_maxShotId = maxId;

    const QString id = c->id;
    QWidget* selectedCard = nullptr;
    for (const auto& s : c->shots) {
        auto* card = new ShotCard(s, c->steps, ShotCard::Layout::Film);
        card->setSelected(s.id == m_selectedShot);
        if (s.id == m_selectedShot) selectedCard = card;
        connect(card, &ShotCard::selectRequested, this, [this](int shotId) { selectShot(shotId); });
        connect(card, &ShotCard::moveRequested, this, [this, id](int shotId, int delta) { m_cases.moveShot(id, shotId, delta); });
        connect(card, &ShotCard::removeRequested, this, [this, id](int shotId) { m_cases.removeShot(id, shotId); });
        evidence::wireCard(card, this, m_cases, m_evidence, id);
        m_shotsLayout->addWidget(card);
    }
    m_shotsLayout->addStretch(1);
    // Una captura nueva se añade al final de la lista, fuera de la parte visible: hay que traerla a
    // la vista. En diferido y forzando la colocación, porque las tarjetas acaban de crearse: hasta
    // que el layout no se activa y el contenido no toma su tamaño, el área ni siquiera tiene rango.
    if (selectedCard) {
        QTimer::singleShot(0, this, [this, card = QPointer<QWidget>(selectedCard)]() {
            if (!card) return;
            m_filmScroll->widget()->layout()->activate();
            m_filmScroll->widget()->adjustSize();
            m_filmScroll->ensureWidgetVisible(card, 0, 12);
        });
    }

    // Visor y barra de la evidencia elegida
    const Screenshot* shot = selectedShot();
    m_preview->setShot(shot ? *shot : Screenshot{});
    m_preview->setPlaceholder(tr("Aún no hay evidencias de este caso.\nPulsa «Capturar pantalla» o arrastra un fichero a la ventana."));
    m_shotBar->setVisible(shot != nullptr);
    if (!shot) return;
    m_selfEdit = true;
    m_assign->clear();
    m_assign->addItem(tr("Sin asignar"), 0);
    for (int i = 0; i < c->steps.size(); ++i)
        m_assign->addItem(tr("Paso %1 · %2").arg(i + 1).arg(ui::elide(c->steps[i].action, 24)), i + 1);
    m_assign->setCurrentIndex(std::max(0, m_assign->findData(shot->step)));
    m_selfEdit = false;
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

void RunView::selectRelativeShot(int delta) {
    const TestCase* c = m_cases.find(m_run.state().caseId);
    if (!c || c->shots.isEmpty()) return;
    int index = 0;
    for (int i = 0; i < c->shots.size(); ++i) if (c->shots[i].id == m_selectedShot) index = i;
    selectShot(c->shots[std::clamp(index + delta, 0, static_cast<int>(c->shots.size()) - 1)].id);
}

} // namespace qaflow
