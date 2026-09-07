#include "RunView.h"

#include "application/RunController.h"
#include "application/SettingsStore.h"
#include "application/TestCaseStore.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/ProgressCells.h"
#include "presentation/widgets/ShotCard.h"
#include "presentation/widgets/TextArea.h"
#include "presentation/widgets/Ui.h"

#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>

namespace qaflow {

namespace {
QString resultColor(StepResult r) {
    switch (r) { case StepResult::Pass: return theme::Green; case StepResult::Fail: return theme::Red; case StepResult::Block: return theme::Amber; }
    return theme::Muted;
}
QString resultLabel(StepResult r) {
    switch (r) { case StepResult::Pass: return QStringLiteral("PASA"); case StepResult::Fail: return QStringLiteral("FALLA"); case StepResult::Block: return QStringLiteral("BLOQ."); }
    return {};
}
QPushButton* verdictButton(const QString& text, const QString& key, const char* role) {
    auto* b = ui::button(QString(), role);
    b->setMinimumWidth(140);
    b->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* h = ui::hbox(b, 0, 6);
    h->setContentsMargins(12, 12, 12, 12);
    h->addStretch(1);
    auto* t = new QLabel(text);
    t->setStyleSheet(QStringLiteral("font-weight:800;font-size:14px;color:inherit;background:transparent;"));
    auto* k = new QLabel(key);
    k->setStyleSheet(QStringLiteral("font-weight:600;font-size:12px;color:inherit;background:transparent;"));
    h->addWidget(t);
    h->addWidget(k);
    h->addStretch(1);
    for (auto* c : b->findChildren<QWidget*>()) c->setAttribute(Qt::WA_TransparentForMouseEvents);
    return b;
}
} // namespace

RunView::RunView(TestCaseStore& cases, RunController& run, SettingsStore& settings, QWidget* parent)
    : QWidget(parent), m_cases(cases), m_run(run), m_settings(settings) {
    auto* root = ui::hbox(this, 0, 0);
    QWidget* content;
    QVBoxLayout* outer;
    auto* sa = ui::scrollArea(&content, &outer);
    outer->setContentsMargins(32, 28, 32, 28);
    auto* page = new QWidget;
    page->setMaximumWidth(900);
    auto* v = ui::vbox(page, 0, 20);
    outer->addWidget(page, 0, Qt::AlignTop);
    root->addWidget(sa, 1);

    // Cabecera
    auto* head = new QWidget;
    auto* hh = ui::hbox(head, 0, 14);
    auto* titleBlock = new QWidget;
    auto* tv = ui::vbox(titleBlock, 0, 0);
    m_eyebrow = ui::label(QString(), "eyebrow");
    m_title = ui::label(QString(), "h1");
    m_title->setWordWrap(true);
    tv->addWidget(m_eyebrow);
    tv->addWidget(m_title);
    hh->addWidget(titleBlock, 1);
    auto* capture = ui::button(QString(), "outline");
    auto* ch = ui::hbox(capture, 0, 8);
    ch->setContentsMargins(14, 8, 14, 8);
    auto* icon = new QFrame;
    icon->setFixedSize(14, 14);
    icon->setStyleSheet(QStringLiteral("border:2px solid %1;border-radius:3px;background:transparent;").arg(theme::Text));
    ch->addWidget(icon);
    auto* capText = new QLabel(QStringLiteral("Capturar pantalla"));
    capText->setStyleSheet(QStringLiteral("font-weight:700;background:transparent;"));
    ch->addWidget(capText);
    m_shortcut = ui::label(QString(), "muted-sm");
    m_shortcut->setStyleSheet(QStringLiteral("font-size:11px;font-weight:600;color:%1;background:transparent;").arg(theme::Muted));
    ch->addWidget(m_shortcut);
    for (auto* c : capture->findChildren<QWidget*>()) c->setAttribute(Qt::WA_TransparentForMouseEvents);
    connect(capture, &QPushButton::clicked, this, &RunView::captureRequested);
    hh->addWidget(capture, 0, Qt::AlignTop);
    v->addWidget(head);

    m_progress = new ProgressCells;
    v->addWidget(m_progress);

    // Tarjeta del paso activo
    m_stepCard = ui::card("card-run");
    auto* scH = ui::hbox(m_stepCard, 0, 0);
    scH->addWidget(ui::accentBar(theme::Green));
    auto* scBody = new QWidget;
    auto* sc = ui::vbox(scBody, 0, 12);
    sc->setContentsMargins(22, 22, 24, 22);
    m_stepCounter = ui::label(QString(), "eyebrow");
    m_stepCounter->setStyleSheet(QStringLiteral("font-size:12px;"));
    sc->addWidget(m_stepCounter);
    auto* cols = new QWidget;
    auto* cg = new QGridLayout(cols);
    cg->setContentsMargins(0, 0, 0, 0);
    cg->setHorizontalSpacing(16);
    cg->addWidget(ui::label(QStringLiteral("ACCIÓN"), "eyebrow"), 0, 0);
    cg->addWidget(ui::label(QStringLiteral("RESULTADO ESPERADO"), "eyebrow"), 0, 1);
    m_action = new QLabel;
    m_action->setWordWrap(true);
    m_action->setStyleSheet(QStringLiteral("font-size:17px;font-weight:600;"));
    m_expected = new QLabel;
    m_expected->setWordWrap(true);
    m_expected->setStyleSheet(QStringLiteral("font-size:15px;color:#d0d8e0;"));
    cg->addWidget(m_action, 1, 0, Qt::AlignTop);
    cg->addWidget(m_expected, 1, 1, Qt::AlignTop);
    cg->setColumnStretch(0, 1);
    cg->setColumnStretch(1, 1);
    sc->addWidget(cols);
    m_note = new TextArea(2);
    m_note->setPlaceholderText(QStringLiteral("Observaciones de este paso (opcional)…"));
    connect(m_note, &TextArea::edited, this, [this](const QString& t) { m_run.setNote(t); });
    sc->addWidget(m_note);
    auto* verdicts = new QWidget;
    auto* vh = ui::hbox(verdicts, 0, 10);
    auto* pass = verdictButton(QStringLiteral("Pasa"), QStringLiteral("P"), "success");
    auto* fail = verdictButton(QStringLiteral("Falla"), QStringLiteral("F"), "danger");
    auto* block = verdictButton(QStringLiteral("Bloqueado"), QStringLiteral("B"), "warning-outline");
    connect(pass, &QPushButton::clicked, this, [this]() { m_run.mark(StepResult::Pass); });
    connect(fail, &QPushButton::clicked, this, [this]() { m_run.mark(StepResult::Fail); });
    connect(block, &QPushButton::clicked, this, [this]() { m_run.mark(StepResult::Block); });
    vh->addWidget(pass);
    vh->addWidget(fail);
    vh->addWidget(block);
    sc->addWidget(verdicts);
    scH->addWidget(scBody, 1);
    v->addWidget(m_stepCard);

    // Atajos de teclado (sólo mientras esta vista es visible y el foco no está en un campo de texto)
    for (auto [key, res] : {std::pair{Qt::Key_P, StepResult::Pass}, std::pair{Qt::Key_F, StepResult::Fail}, std::pair{Qt::Key_B, StepResult::Block}}) {
        auto* sc2 = new QShortcut(QKeySequence(key), this);
        sc2->setContext(Qt::WindowShortcut);
        connect(sc2, &QShortcut::activated, this, [this, res]() { if (isVisible() && m_run.isRunning()) m_run.mark(res); });
    }

    // Tarjeta de fin de ejecución
    m_doneCard = ui::card("card-lg");
    auto* dh = ui::hbox(m_doneCard, 22, 20);
    dh->setContentsMargins(24, 22, 24, 22);
    auto* dText = new QWidget;
    auto* dv = ui::vbox(dText, 0, 2);
    dv->addWidget(ui::label(QStringLiteral("EJECUCIÓN TERMINADA"), "eyebrow"));
    m_verdict = new QLabel;
    m_verdict->setStyleSheet(QStringLiteral("font-size:22px;font-weight:800;"));
    dv->addWidget(m_verdict);
    m_summary = ui::label(QString(), "muted");
    dv->addWidget(m_summary);
    dh->addWidget(dText, 1);
    m_reportBug = ui::button(QStringLiteral("Reportar bug"), "danger");
    connect(m_reportBug, &QPushButton::clicked, this, &RunView::reportBugRequested);
    auto* repeat = ui::button(QStringLiteral("Repetir"), "outline");
    connect(repeat, &QPushButton::clicked, this, [this]() { m_run.restart(); });
    m_finish = ui::button(QStringLiteral("Finalizar y volver"), "outline");
    connect(m_finish, &QPushButton::clicked, this, &RunView::finishRequested);
    dh->addWidget(m_reportBug);
    dh->addWidget(repeat);
    dh->addWidget(m_finish);
    v->addWidget(m_doneCard);

    m_empty = ui::label(QStringLiteral("No hay ninguna ejecución activa. Abre un caso y pulsa ▶ Ejecutar."), "muted");
    v->addWidget(m_empty);

    // Registro + Capturas
    auto* bottom = new QWidget;
    auto* bg = new QGridLayout(bottom);
    bg->setContentsMargins(0, 0, 0, 0);
    bg->setHorizontalSpacing(16);
    auto* log = ui::card("card");
    auto* lv = ui::vbox(log, 16, 10);
    lv->addWidget(ui::label(QStringLiteral("REGISTRO"), "eyebrow"));
    auto* logList = new QWidget;
    m_logLayout = ui::vbox(logList, 0, 6);
    lv->addWidget(logList);
    lv->addStretch(1);
    bg->addWidget(log, 0, 0);

    auto* shots = ui::card("card");
    auto* shv = ui::vbox(shots, 16, 10);
    auto* shHead = new QWidget;
    auto* shh = ui::hbox(shHead, 0, 8);
    m_shotsHeader = ui::label(QString(), "eyebrow");
    shh->addWidget(m_shotsHeader, 1);
    m_shotFolder = ui::label(QString(), "muted-sm");
    m_shotFolder->setStyleSheet(QStringLiteral("font-size:11px;"));
    shh->addWidget(m_shotFolder);
    shv->addWidget(shHead);
    auto* shotList = new QWidget;
    m_shotsLayout = ui::vbox(shotList, 0, 8);
    shv->addWidget(shotList);
    auto* shActions = new QWidget;
    auto* sah = ui::hbox(shActions, 0, 8);
    auto* capture2 = ui::button(QStringLiteral("+ Capturar"), "dashed");
    capture2->setStyleSheet(QStringLiteral("padding:10px;font-size:12.5px;"));
    connect(capture2, &QPushButton::clicked, this, &RunView::captureRequested);
    sah->addWidget(capture2, 1);
    m_sortShots = ui::button(QStringLiteral("Ordenar por paso"), "outline");
    m_sortShots->setStyleSheet(QStringLiteral("padding:10px 12px;font-size:12px;border-radius:8px;"));
    connect(m_sortShots, &QPushButton::clicked, this, [this]() { m_cases.sortShotsByStep(m_run.state().caseId); });
    sah->addWidget(m_sortShots);
    shv->addWidget(shActions);
    shv->addStretch(1);
    bg->addWidget(shots, 0, 1);
    bg->setColumnStretch(0, 14);
    bg->setColumnStretch(1, 10);
    v->addWidget(bottom);

    connect(&m_run, &RunController::runChanged, this, &RunView::refresh);
    connect(&m_cases, &TestCaseStore::caseChanged, this, [this](const QString& id) { if (id == m_run.state().caseId) refresh(); });
    connect(&m_settings, &SettingsStore::captureChanged, this, &RunView::refresh);
    refresh();
}

void RunView::refresh() {
    const RunState& r = m_run.state();
    const TestCase* c = m_cases.find(r.caseId);
    m_shortcut->setText(m_settings.capture().shortcut);
    m_shotFolder->setText(m_settings.capture().folder);

    const bool hasRun = c != nullptr;
    m_empty->setVisible(!hasRun);
    m_stepCard->setVisible(hasRun && !r.finished && !c->steps.isEmpty());
    m_doneCard->setVisible(hasRun && r.finished);
    if (!hasRun) {
        m_eyebrow->setText(QStringLiteral("EJECUCIÓN MANUAL"));
        m_title->setText(QStringLiteral("Sin ejecución"));
        m_progress->setColors({});
        ui::clearLayout(m_logLayout);
        ui::clearLayout(m_shotsLayout);
        m_shotsHeader->setText(QStringLiteral("CAPTURAS · 0"));
        m_sortShots->hide();
        return;
    }

    m_eyebrow->setText(QStringLiteral("%1 · %2 · EJECUCIÓN MANUAL").arg(c->id, c->suite.toUpper()));
    m_title->setText(c->title);

    QStringList colors;
    for (int i = 0; i < c->steps.size(); ++i) {
        if (i < r.results.size()) colors << resultColor(r.results[i].result);
        else if (i == r.idx && !r.finished) colors << theme::Blue;
        else colors << theme::Border;
    }
    m_progress->setColors(colors);

    if (!r.finished && r.idx < c->steps.size()) {
        m_stepCounter->setText(QStringLiteral("PASO %1 DE %2").arg(r.idx + 1).arg(c->steps.size()));
        m_action->setText(c->steps[r.idx].action);
        m_expected->setText(c->steps[r.idx].expected);
        m_note->setTextSilently(r.note);
    } else {
        const Verdict v = r.verdict();
        const QString color = v == Verdict::Bloqueado ? theme::Amber : v == Verdict::Fallido ? theme::Red : theme::Green;
        const QString text = v == Verdict::Bloqueado ? QStringLiteral("Bloqueado") : v == Verdict::Fallido ? QStringLiteral("Fallido") : QStringLiteral("Superado");
        m_verdict->setText(text);
        m_verdict->setStyleSheet(QStringLiteral("font-size:22px;font-weight:800;color:%1;").arg(color));
        m_summary->setText(QStringLiteral("%1 pasan · %2 fallan · %3 bloqueados")
                               .arg(r.count(StepResult::Pass)).arg(r.count(StepResult::Fail)).arg(r.count(StepResult::Block)));
        m_reportBug->setVisible(r.count(StepResult::Fail) > 0);
        m_finish->setText(m_run.queuedCount() > 0 ? QStringLiteral("Siguiente caso · quedan %1").arg(m_run.queuedCount())
                          : !m_run.planRunId().isEmpty() ? QStringLiteral("Terminar plan y ver informe")
                                                          : QStringLiteral("Finalizar y volver"));
    }
    refreshLog();
    refreshShots();
}

void RunView::refreshLog() {
    ui::clearLayout(m_logLayout);
    const RunState& r = m_run.state();
    const TestCase* c = m_cases.find(r.caseId);
    if (!c) return;
    if (r.results.isEmpty()) {
        auto* e = ui::label(QStringLiteral("Aún no hay pasos registrados."), "muted");
        e->setContentsMargins(10, 8, 10, 8);
        m_logLayout->addWidget(e);
        return;
    }
    for (int i = 0; i < r.results.size() && i < c->steps.size(); ++i) {
        auto* row = ui::card("card-flat");
        auto* h = ui::hbox(row, 0, 10);
        h->setContentsMargins(10, 8, 10, 8);
        auto* n = ui::label(QString::number(i + 1), "mono-muted");
        n->setFixedWidth(22);
        h->addWidget(n, 0, Qt::AlignTop);
        auto* a = new QLabel(c->steps[i].action);
        a->setWordWrap(true);
        a->setStyleSheet(QStringLiteral("color:#d0d8e0;"));
        h->addWidget(a, 1);
        const StepResult res = r.results[i].result;
        h->addWidget(ui::pill(resultLabel(res), resultColor(res), res == StepResult::Fail ? QStringLiteral("#ffffff") : theme::Bg), 0, Qt::AlignTop);
        m_logLayout->addWidget(row);
    }
}

void RunView::refreshShots() {
    ui::clearLayout(m_shotsLayout);
    const TestCase* c = m_cases.find(m_run.state().caseId);
    if (!c) return;
    m_shotsHeader->setText(QStringLiteral("CAPTURAS · %1").arg(c->shots.size()));
    m_sortShots->setVisible(!c->shots.isEmpty());
    const QString id = c->id;
    for (const auto& s : c->shots) {
        auto* card = new ShotCard(s, c->steps, ShotCard::Layout::Row);
        connect(card, &ShotCard::stepChanged, this, [this, id](int shotId, int step) { m_cases.assignShotStep(id, shotId, step); });
        connect(card, &ShotCard::moveRequested, this, [this, id](int shotId, int delta) { m_cases.moveShot(id, shotId, delta); });
        connect(card, &ShotCard::removeRequested, this, [this, id](int shotId) { m_cases.removeShot(id, shotId); });
        m_shotsLayout->addWidget(card);
    }
}

} // namespace qaflow
