#include "BugDialog.h"

#include "application/BugReportService.h"
#include "application/EvidenceService.h"
#include "application/SettingsStore.h"
#include "application/TestCaseStore.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/AnnotationEditor.h"
#include "presentation/widgets/EvidenceActions.h"
#include "presentation/widgets/FlowLayout.h"
#include "presentation/widgets/ImageViewer.h"
#include "presentation/widgets/ShotCard.h"
#include "presentation/widgets/TextArea.h"
#include "presentation/widgets/Ui.h"

#include <QComboBox>
#include <QCompleter>
#include <QGridLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QTimer>

#include <algorithm>

namespace qaflow {

namespace {
QWidget* field(const QString& title, QWidget* w, const QString& titleColor = QString()) {
    auto* box = new QWidget;
    auto* v = ui::vbox(box, 0, 6);
    auto* l = ui::label(title.toUpper(), "eyebrow");
    if (!titleColor.isEmpty()) l->setStyleSheet(QStringLiteral("color:%1;").arg(titleColor));
    v->addWidget(l);
    v->addWidget(w);
    return box;
}
QComboBox* editableCombo(const QString& placeholder) {
    auto* c = new QComboBox;
    c->setEditable(true);
    c->setInsertPolicy(QComboBox::NoInsert);
    c->lineEdit()->setPlaceholderText(placeholder);
    return c;
}
/// Rellena un combo conservando el texto actual.
void fill(QComboBox* c, const QStringList& items) {
    const QString cur = c->currentText();
    c->blockSignals(true);
    c->clear();
    c->addItems(items);
    c->setCurrentText(cur);
    c->blockSignals(false);
}
QPushButton* smallButton(const QString& text, const char* role) {
    auto* b = ui::button(text, role);
    b->setStyleSheet(QStringLiteral("padding:5px 10px;font-size:12px;border-radius:8px;"));
    return b;
}
} // namespace

BugDialog::BugDialog(TestCaseStore& cases, SettingsStore& settings, BugReportService& bugs, EvidenceService& evidence, QWidget* parent)
    : QDialog(parent), m_cases(cases), m_settings(settings), m_bugs(bugs), m_evidence(evidence) {
    setObjectName(QStringLiteral("bugDialog"));
    setWindowTitle(tr("Reportar bug"));
    setWindowIcon(ui::appIcon());
    // El formulario es ancho (cinco columnas de metadatos y tres de campos del gestor): se abre con
    // sitio para todo, sin pasarse de la pantalla.
    if (const QScreen* screen = QGuiApplication::primaryScreen())
        resize(std::min(1060, screen->availableGeometry().width() - 80), std::min(820, screen->availableGeometry().height() - 80));
    else
        resize(1060, 820);

    auto* root = ui::vbox(this, 0, 0);
    QWidget* content;
    QVBoxLayout* outer;
    auto* sa = ui::scrollArea(&content, &outer);
    // En una pantalla estrecha el formulario no se recorta: se desplaza.
    sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    outer->setContentsMargins(24, 22, 24, 22);
    auto* page = new QWidget;
    auto* v = ui::vbox(page, 0, 16);
    outer->addWidget(page, 0, Qt::AlignTop);
    root->addWidget(sa, 1);

    auto* head = new QWidget;
    auto* hv = ui::vbox(head, 0, 0);
    m_eyebrow = ui::label(QString(), "eyebrow");
    m_eyebrow->setTextFormat(Qt::RichText);
    hv->addWidget(m_eyebrow);
    hv->addWidget(ui::label(tr("Reportar bug"), "h1-sm"));
    v->addWidget(head);

    buildForm(v);

    // El paso elegido se describe con los pasos del caso: si el caso cambia, se repinta.
    connect(&m_cases, &TestCaseStore::caseChanged, this, [this](const QString& id) { if (id == m_cases.selectedId()) refreshShots(); });
    connect(&m_settings, &SettingsStore::trackerChanged, this, [this]() { refreshHeader(); refreshTrackerFields(); });
    connect(&m_bugs, &BugReportService::metadataChanged, this, &BugDialog::refreshTrackerFields);
    // La captura se hace con el diálogo escondido: vuelve en cuanto la imagen llega (o falla). Es
    // del parte, no de la ejecución: no pasa por el caso.
    connect(&m_evidence, &EvidenceService::bugShotCaptured, this, [this](const Screenshot& shot) {
        if (!m_capturing) return;
        m_capturing = false;
        show();
        raise();
        activateWindow();
        addShots({shot});
        if (m_settings.capture().openEditor && shot.isImage() && !shot.isAnimation()) {
            const int id = m_shots.last().id;
            QTimer::singleShot(0, this, [this, id]() { annotateShot(id); });
        }
    });
    connect(&m_evidence, &EvidenceService::failed, this, [this](const QString&) {
        if (!m_capturing) return;
        m_capturing = false;
        show();
    });
    refreshHeader();
    refreshTrackerFields();
}

void BugDialog::buildForm(QVBoxLayout* v) {
    auto* card = ui::card("card-lg");
    auto* ch = ui::hbox(card, 0, 0);
    ch->addWidget(ui::accentBar(theme::Red));
    auto* body = new QWidget;
    auto* bv = ui::vbox(body, 0, 16);
    bv->setContentsMargins(22, 22, 24, 22);

    m_title = new QLineEdit;
    m_title->setObjectName(QStringLiteral("bugTitle"));
    m_title->setPlaceholderText(tr("Resumen corto: qué falla y dónde"));
    m_title->setStyleSheet(QStringLiteral("font-size:14px;padding:9px 12px;"));
    connect(m_title, &QLineEdit::textChanged, this, [this]() { if (m_touched) ui::setFlag(m_title, "invalid", m_title->text().trimmed().isEmpty()); });
    bv->addWidget(field(tr("Título"), m_title));

    auto* meta = new QWidget;
    auto* mg = new QGridLayout(meta);
    mg->setContentsMargins(0, 0, 0, 0);
    mg->setHorizontalSpacing(12);
    m_severity = new QComboBox;
    m_severity->setObjectName(QStringLiteral("bugSeverity"));
    for (const auto& s : BugReport::severities()) m_severity->addItem(BugReport::severityLabel(s), s);
    connect(m_severity, &QComboBox::currentIndexChanged, this, [this](int) {
        // La severidad sugiere la prioridad de Jira mientras el usuario no haya elegido otra.
        const QString sev = m_severity->currentData().toString();
        if (m_settings.tracker().kind == TrackerKind::Jira && m_priority->currentText().isEmpty()) m_priority->setCurrentText(BugReport::jiraPriorityFor(sev));
    });
    m_classification = new QComboBox;
    for (const auto& c : BugReport::classifications()) m_classification->addItem(BugReport::classificationLabel(c), c);
    m_classification->setToolTip(tr("Tipo de observación del acta de control de calidad (R-213)"));
    m_env = new QComboBox;
    for (const auto& e : BugReport::environments()) m_env->addItem(BugReport::environmentLabel(e), e);
    m_linkedCase = new QLabel;
    m_linkedCase->setStyleSheet(QStringLiteral("background:%1;border:1px solid %2;border-radius:9px;padding:8px 10px;font-family:'Consolas','DejaVu Sans Mono',monospace;color:%3;").arg(theme::Elevated, theme::Border, theme::Muted));
    // Un bug es de un paso concreto: es lo que hace que al publicar la ejecución el defecto cuelgue
    // del resultado de ese paso y no del caso entero.
    m_linkedStep = new QComboBox;
    m_linkedStep->setObjectName(QStringLiteral("bugStep"));
    m_linkedStep->setToolTip(tr("Paso en el que se vio el fallo; el defecto se cuelga de él al publicar la ejecución"));
    mg->addWidget(field(tr("Severidad"), m_severity), 0, 0);
    mg->addWidget(field(tr("Clasificación"), m_classification), 0, 1);
    mg->addWidget(field(tr("Entorno"), m_env), 0, 2);
    mg->addWidget(field(tr("Caso vinculado"), m_linkedCase), 0, 3);
    mg->addWidget(field(tr("Paso"), m_linkedStep), 0, 4);
    for (int i = 0; i < 4; ++i) mg->setColumnStretch(i, 1);
    mg->setColumnStretch(4, 2);   // el paso lleva la acción: necesita más sitio que el resto
    bv->addWidget(meta);

    // Campos del gestor
    auto* trackerHead = new QWidget;
    auto* th = ui::hbox(trackerHead, 0, 8);
    th->addWidget(ui::label(tr("CAMPOS DEL GESTOR"), "eyebrow"));
    m_metaNote = ui::label(QString(), "muted-sm");
    m_metaNote->setStyleSheet(QStringLiteral("font-size:11px;"));
    th->addWidget(m_metaNote, 1);
    m_loadMeta = smallButton(tr("Cargar valores del proyecto"), "outline");
    connect(m_loadMeta, &QPushButton::clicked, this, [this]() { loadMetadata(true); });
    th->addWidget(m_loadMeta);
    bv->addWidget(trackerHead);
    auto* tf = new QWidget;
    auto* tg = new QGridLayout(tf);
    tg->setContentsMargins(0, 0, 0, 0);
    tg->setHorizontalSpacing(12);
    tg->setVerticalSpacing(12);
    m_issueType = editableCombo(BugReport::jiraIssueTypes().value(0));
    m_priority = editableCombo(tr("Por defecto"));
    m_assignee = editableCombo(tr("Sin asignar"));
    m_assignee->setObjectName(QStringLiteral("bugAssignee"));
    // Las opciones ya vienen filtradas por el gestor, así que el completador las muestra todas:
    // buscar "aperez" puede devolver a "Ana Pérez", cuyo nombre no contiene lo escrito.
    if (QCompleter* completer = m_assignee->completer()) {
        completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
    }
    m_assigneeSearch = new QTimer(this);
    m_assigneeSearch->setSingleShot(true);
    m_assigneeSearch->setInterval(300);
    connect(m_assigneeSearch, &QTimer::timeout, this, &BugDialog::searchAssignees);
    // `textEdited` (y no `editTextChanged`) para no buscar cuando el formulario se rellena solo.
    connect(m_assignee->lineEdit(), &QLineEdit::textEdited, this, [this]() { m_assigneeSearch->start(); });
    m_components = new QLineEdit;
    m_components->setPlaceholderText(tr("Separados por comas"));
    m_versions = new QLineEdit;
    m_versions->setPlaceholderText(tr("Separadas por comas"));
    m_labels = new QLineEdit;
    m_labels->setPlaceholderText(tr("Separadas por comas · siempre: qaflow, <caso>"));
    tg->addWidget(field(tr("Tipo"), m_issueType), 0, 0);
    tg->addWidget(field(tr("Prioridad"), m_priority), 0, 1);
    tg->addWidget(field(tr("Asignado a"), m_assignee), 0, 2);
    tg->addWidget(field(tr("Componentes"), m_components), 1, 0);
    tg->addWidget(field(tr("Versión afectada"), m_versions), 1, 1);
    tg->addWidget(field(tr("Etiquetas"), m_labels), 1, 2);
    for (int i = 0; i < 3; ++i) tg->setColumnStretch(i, 1);
    bv->addWidget(tf);

    m_steps = new TextArea(5);
    m_steps->setProperty("role", QStringLiteral("mono"));
    bv->addWidget(field(tr("Pasos para reproducir"), m_steps));

    auto* results = new QWidget;
    auto* rg = new QGridLayout(results);
    rg->setContentsMargins(0, 0, 0, 0);
    rg->setHorizontalSpacing(12);
    m_expected = new TextArea(3);
    m_actual = new TextArea(3);
    m_actual->setProperty("role", QStringLiteral("danger-soft"));
    m_actual->setPlaceholderText(tr("Qué ocurrió realmente"));
    connect(m_actual, &TextArea::edited, this, [this]() { if (m_touched) ui::setFlag(m_actual, "invalid", m_actual->toPlainText().trimmed().isEmpty()); });
    rg->addWidget(field(tr("Resultado esperado"), m_expected), 0, 0);
    rg->addWidget(field(tr("Resultado actual"), m_actual, theme::RedSoft), 0, 1);
    rg->setColumnStretch(0, 1);
    rg->setColumnStretch(1, 1);
    bv->addWidget(results);

    auto* shotsBlock = new QWidget;
    auto* sv = ui::vbox(shotsBlock, 0, 8);
    auto* shHead = new QWidget;
    auto* shh = ui::hbox(shHead, 0, 8);
    m_shotsHeader = ui::label(QString(), "eyebrow");
    shh->addWidget(m_shotsHeader, 1);
    auto* capture = ui::button(tr("+ Capturar pantalla"), "dashed");
    capture->setObjectName(QStringLiteral("bugCapture"));
    capture->setToolTip(tr("La ventana se esconde mientras se captura y vuelve con la imagen adjunta"));
    connect(capture, &QPushButton::clicked, this, &BugDialog::captureScreen);
    shh->addWidget(capture);
    auto* attach = ui::button(tr("+ Adjuntar archivo…"), "dashed");
    attach->setToolTip(tr("Adjunta logs, vídeos o imágenes existentes; se suben al gestor con el bug"));
    connect(attach, &QPushButton::clicked, this, [this]() { addShots(m_evidence.copyForBug(evidence::pickFiles(this))); });
    shh->addWidget(attach);
    sv->addWidget(shHead);
    auto* shots = new QWidget;
    m_shotsRow = new FlowLayout(shots, 0, 8, 8);
    sv->addWidget(shots);
    bv->addWidget(shotsBlock);

    ch->addWidget(body, 1);
    v->addWidget(card);

    auto* actions = new QWidget;
    auto* ah = ui::hbox(actions, 0, 10);
    ah->addStretch(1);
    auto* cancel = ui::button(tr("Cancelar"), "ghost");
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    m_submit = ui::button(QString(), "primary");
    m_submit->setObjectName(QStringLiteral("bugSubmit"));
    connect(m_submit, &QPushButton::clicked, this, &BugDialog::submit);
    ah->addWidget(cancel);
    ah->addWidget(m_submit);
    v->addWidget(actions);
}

// ---- Refrescos -----------------------------------------------------------------------------

void BugDialog::refreshHeader() {
    const TrackerSettings& t = m_settings.tracker();
    m_eyebrow->setText(tr("NUEVO DEFECTO · DESTINO %1 <span style=\"color:%2;font-family:monospace\">%3</span>")
                           .arg(toString(t.kind).toUpper(), theme::Blue, t.project.isEmpty() ? tr("(sin proyecto)") : t.project));
    m_submit->setText(tr("Crear en %1").arg(toString(t.kind)));
}

void BugDialog::refreshTrackerFields() {
    const TrackerSettings& t = m_settings.tracker();
    const ProjectMetadata& m = m_bugs.metadata();
    fill(m_issueType, m.issueTypes);
    fill(m_priority, m.priorities);
    setAssigneeOptions(m.assignees);
    const bool searches = m_bugs.searchesAssigneesOnServer();
    m_assignee->lineEdit()->setPlaceholderText(searches ? tr("Escribe para buscar en %1").arg(toString(t.kind)) : tr("Sin asignar"));
    m_assignee->setToolTip(searches ? tr("Las personas se buscan en %1 según escribes; no hace falta cargarlas antes").arg(toString(t.kind))
                                    : tr("Personas del proyecto cargadas con «Cargar valores del proyecto»"));
    // Con Jira se propone el primero de los tipos que QAflow trabaja («Error»): es lo que luego
    // encuentra «Traer de Jira», que busca justo esos tipos.
    if (m_issueType->currentText().isEmpty())
        m_issueType->setCurrentText(t.kind == TrackerKind::GitLab   ? QStringLiteral("issue")
                                    : t.kind == TrackerKind::GitHub ? QStringLiteral("Issue")
                                                                    : BugReport::jiraIssueTypes().value(0));
    m_issueType->setEnabled(t.kind != TrackerKind::GitHub);
    m_metaNote->setText(m_bugs.hasMetadata()
                            ? tr("%1 tipos · %2 prioridades · %3 componentes · %4 versiones · %5 asignables")
                                  .arg(m.issueTypes.size()).arg(m.priorities.size()).arg(m.components.size()).arg(m.versions.size()).arg(m.assignees.size())
                            : tr("Escribe los valores o cárgalos del proyecto"));
    switch (t.kind) {
        case TrackerKind::Jira: m_components->setToolTip(tr("Componentes del proyecto Jira")); m_versions->setToolTip(tr("Versiones afectadas")); break;
        case TrackerKind::GitHub: case TrackerKind::GitLab: m_components->setToolTip(tr("Se envían como etiquetas")); m_versions->setToolTip(tr("Milestone (sólo informativo)")); break;
        case TrackerKind::AzureDevOps: m_components->setToolTip(tr("Se envían como tags")); m_versions->setToolTip(tr("Campo Found In")); break;
    }
}

void BugDialog::loadDraft(int stepIndex) {
    const BugReport d = m_bugs.draftFromCurrentContext(stepIndex);
    m_touched = false;
    ui::setFlag(m_title, "invalid", false);
    ui::setFlag(m_actual, "invalid", false);
    m_title->setText(d.title);
    m_severity->setCurrentIndex(std::max(0, m_severity->findData(d.severity)));
    m_classification->setCurrentIndex(std::max(0, m_classification->findData(d.classification)));
    m_env->setCurrentIndex(std::max(0, m_env->findData(d.environment)));
    m_linkedCase->setText(d.linkedCaseId.isEmpty() ? QStringLiteral("—") : d.linkedCaseId);
    refreshStepOptions(d.linkedStep);
    m_priority->setCurrentText(m_settings.tracker().kind == TrackerKind::Jira ? d.priority : QString());
    m_assignee->setCurrentText(QString());
    m_components->setText(d.components.join(QStringLiteral(", ")));
    m_versions->clear();
    m_labels->clear();
    m_steps->setTextSilently(d.stepsToReproduce);
    m_expected->setTextSilently(d.expected);
    m_actual->setTextSilently(d.actual);
    // El parte arranca con una copia de la última captura de la ejecución; el resto se añade a mano.
    clearShots(true);
    if (const TestCase* c = m_cases.selected()) {
        const QList<Screenshot> run = c->latestEvidence();
        const auto last = std::max_element(run.cbegin(), run.cend(), [](const Screenshot& a, const Screenshot& b) { return a.id < b.id; });
        if (last != run.cend()) addShots(m_evidence.copyForBug({last->path}));
    }
    refreshShots();
    refreshTrackerFields();
    if (m_settings.tracker().connected && !m_bugs.hasMetadata()) loadMetadata(false);
}

void BugDialog::refreshStepOptions(int step) {
    const TestCase* c = m_cases.selected();
    const QSignalBlocker block(m_linkedStep);
    m_linkedStep->clear();
    m_linkedStep->addItem(tr("Todo el caso"), 0);
    if (c)
        for (int i = 0; i < c->steps.size(); ++i)
            m_linkedStep->addItem(tr("Paso %1 · %2").arg(i + 1).arg(ui::elide(c->steps[i].action, 34)), i + 1);
    m_linkedStep->setCurrentIndex(std::max(0, m_linkedStep->findData(step)));
    m_linkedStep->setEnabled(c && !c->steps.isEmpty());
}

void BugDialog::refreshShots() {
    ui::clearLayout(m_shotsRow);
    m_shotsHeader->setText(tr("ADJUNTOS · %1").arg(m_shots.size()));
    const TestCase* c = m_cases.selected();
    const QList<TestStep> steps = c ? c->steps : QList<TestStep>{};
    for (const auto& s : m_shots) {
        auto* card = new ShotCard(s, steps, ShotCard::Layout::Compact);
        connect(card, &ShotCard::removeRequested, this, &BugDialog::removeShot);
        connect(card, &ShotCard::openRequested, this, &BugDialog::openShot);
        connect(card, &ShotCard::annotateRequested, this, [this, card](int id) { if (annotateShot(id)) card->reloadThumbnail(); });
        connect(card, &ShotCard::copyRequested, this, [this](int id) { if (const Screenshot* s = findShot(id)) m_evidence.copyToClipboard(s->path); });
        connect(card, &ShotCard::openFolderRequested, this, &BugDialog::showShotInFolder);
        m_shotsRow->addWidget(card);
    }
}

// ---- Adjuntos del parte --------------------------------------------------------------------
// Son copias del parte: quitarlos o anotarlos no toca la evidencia de la ejecución.

void BugDialog::addShots(const QList<Screenshot>& shots) {
    if (shots.isEmpty()) return;
    for (Screenshot s : shots) {
        s.id = m_nextShotId++;
        m_shots << s;
    }
    refreshShots();
}

void BugDialog::removeShot(int shotId) {
    for (int i = 0; i < m_shots.size(); ++i) {
        if (m_shots[i].id != shotId) continue;
        m_evidence.discardBugFiles({m_shots.takeAt(i).path});
        refreshShots();
        return;
    }
}

void BugDialog::clearShots(bool deleteFiles) {
    if (deleteFiles) {
        QStringList paths;
        for (const auto& s : m_shots) paths << s.path;
        m_evidence.discardBugFiles(paths);
    }
    m_shots.clear();
}

const Screenshot* BugDialog::findShot(int shotId) const {
    for (const auto& s : m_shots) if (s.id == shotId) return &s;
    return nullptr;
}

void BugDialog::openShot(int shotId) {
    if (m_shots.isEmpty()) return;
    int index = 0;
    for (int i = 0; i < m_shots.size(); ++i) if (m_shots[i].id == shotId) index = i;
    auto* viewer = new ImageViewer(m_shots, index, this);
    QPointer<ImageViewer> guard(viewer);
    connect(viewer, &ImageViewer::copyRequested, viewer, [this](int id) { if (const Screenshot* s = findShot(id)) m_evidence.copyToClipboard(s->path); });
    connect(viewer, &ImageViewer::openFolderRequested, viewer, [this](int id) { showShotInFolder(id); });
    connect(viewer, &ImageViewer::annotateRequested, viewer, [this, guard](int id) {
        if (!annotateShot(id)) return;
        if (guard) guard->reload();
        refreshShots();
    });
    viewer->show();
    viewer->raise();
    viewer->activateWindow();
}

bool BugDialog::annotateShot(int shotId) {
    const Screenshot* s = findShot(shotId);
    if (!s || !s->isImage() || s->isAnimation()) return false;
    const QImage base(s->path);
    if (base.isNull()) return false;
    const QString path = s->path;
    const QImage edited = AnnotationEditor::edit(base, this);
    return !edited.isNull() && m_evidence.replaceBugImage(path, edited);
}

void BugDialog::showShotInFolder(int shotId) const {
    if (const Screenshot* s = findShot(shotId)) evidence::showInFolder(s->path);
}

void BugDialog::done(int result) {
    // Un parte cancelado se lleva sus adjuntos; uno creado (o encolado) los necesita: el gestor
    // los sube, o los subirá al reintentar.
    if (result == QDialog::Rejected) clearShots(true);
    QDialog::done(result);
}

// ---- Acciones ------------------------------------------------------------------------------

void BugDialog::captureScreen() {
    // La captura es del parte: no hace falta ejecución en curso ni se añade a la de la ejecución.
    if (m_capturing && m_evidence.isCountingDown()) { m_evidence.cancelCountdown(); return; }
    if (m_evidence.isBusy() || m_evidence.isCountingDown() || m_evidence.isRecording()) {
        emit toast(tr("Ya hay una captura o una grabación en curso"), theme::Amber);
        return;
    }
    m_capturing = true;
    hide();
    m_evidence.captureForBug();
}

void BugDialog::loadMetadata(bool force) {
    if (m_busy) return;
    m_busy = true;
    m_loadMeta->setEnabled(false);
    m_metaNote->setText(tr("Cargando…"));
    m_bugs.loadMetadata(force, [this](const MetadataResult& r) {
        m_busy = false;
        m_loadMeta->setEnabled(true);
        refreshTrackerFields();
        if (!r.ok) emit toast(tr("No se pudieron cargar los valores del proyecto · %1").arg(r.error), theme::Amber);
    });
}

void BugDialog::setAssigneeOptions(const QList<Assignee>& people) {
    // El combo guarda el id en itemData y muestra el nombre; se conserva lo escrito y el cursor.
    QLineEdit* edit = m_assignee->lineEdit();
    const QString typed = edit->text();
    const int cursor = edit->cursorPosition();
    m_assignee->blockSignals(true);
    m_assignee->clear();
    for (const auto& a : people) m_assignee->addItem(a.name, a.id);
    edit->setText(typed);
    edit->setCursorPosition(cursor);
    m_assignee->blockSignals(false);
}

void BugDialog::searchAssignees() {
    const int seq = ++m_assigneeSeq;
    m_bugs.searchAssignees(m_assignee->lineEdit()->text(), [this, seq](const AssigneeSearch& r) {
        if (seq != m_assigneeSeq) return;   // ya se ha escrito otra cosa: esta respuesta no vale
        setAssigneeOptions(r.assignees);
        if (!r.ok) { m_metaNote->setText(tr("No se pudieron buscar personas · %1").arg(r.error)); return; }
        if (m_assignee->lineEdit()->hasFocus() && m_assignee->completer()) m_assignee->completer()->complete();
    });
}

BugReport BugDialog::collect() const {
    BugReport b;
    b.title = m_title->text();
    b.severity = m_severity->currentData().toString();
    b.classification = m_classification->currentData().toString();
    b.environment = m_env->currentData().toString();
    b.linkedCaseId = m_linkedCase->text() == QStringLiteral("—") ? QString() : m_linkedCase->text();
    b.linkedStep = m_linkedStep->currentData().toInt();
    if (const TestCase* c = m_cases.selected(); c && c->id == b.linkedCaseId) b.linkedStoryKey = c->jiraKey;
    b.stepsToReproduce = m_steps->toPlainText();
    b.expected = m_expected->toPlainText();
    b.actual = m_actual->toPlainText();
    b.issueType = m_issueType->currentText().trimmed();
    b.priority = m_priority->currentText().trimmed();
    b.assigneeName = m_assignee->currentText().trimmed();
    // El id (usuario en Jira Server, accountId en Cloud) sale de la persona elegida; si el texto no
    // corresponde a ninguna, se envía tal cual y que lo valide el gestor.
    const int ai = m_assignee->findText(b.assigneeName, Qt::MatchFixedString);
    b.assigneeId = ai >= 0 ? m_assignee->itemData(ai).toString() : b.assigneeName;
    b.components = parseTags(m_components->text());
    b.affectsVersions = parseTags(m_versions->text());
    b.labels = parseTags(m_labels->text());
    for (const auto& s : m_shots) b.attachmentPaths << s.path;
    return b;
}

void BugDialog::submit() {
    if (m_sending) return;
    const BugReport b = collect();
    if (!b.isValid()) {
        m_touched = true;
        ui::setFlag(m_title, "invalid", b.title.trimmed().isEmpty());
        ui::setFlag(m_actual, "invalid", b.actual.trimmed().isEmpty());
        emit toast(tr("Completa título y resultado actual"), theme::Red);
        return;
    }
    const QString tracker = toString(m_settings.tracker().kind);
    if (m_settings.tracker().token.trimmed().isEmpty()) {
        emit toast(tr("%1 no está configurado · revisa Ajustes").arg(tracker), theme::Amber);
        return;
    }
    m_sending = true;
    m_submit->setEnabled(false);
    m_submit->setText(tr("Creando…"));
    m_bugs.submit(b, [this, tracker](const BugReportService::SubmitResult& r) {
        m_sending = false;
        m_submit->setEnabled(true);
        refreshHeader();
        if (r.ok) {
            emit toast(tr("%1 creado en %2 con %3 adjuntos").arg(r.key, tracker).arg(r.attachmentsUploaded), theme::Blue);
            emit submitted(r.key);
            accept();
        } else if (r.queued) {
            emit toast(tr("Sin conexión con %1 · el bug queda en la cola y se reintentará").arg(tracker), theme::Amber);
            emit submitted(QString());
            accept();
        } else {
            emit toast(tr("%1 rechazó el bug · %2").arg(tracker, r.error), theme::Red);
        }
    });
}

} // namespace qaflow
