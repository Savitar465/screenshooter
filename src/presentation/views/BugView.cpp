#include "BugView.h"

#include "application/BugReportService.h"
#include "application/BugStore.h"
#include "application/EvidenceService.h"
#include "application/SettingsStore.h"
#include "application/TestCaseStore.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/EvidenceActions.h"
#include "presentation/widgets/FlowLayout.h"
#include "presentation/widgets/ShotCard.h"
#include "presentation/widgets/TextArea.h"
#include "presentation/widgets/Ui.h"

#include <QComboBox>
#include <QCompleter>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>

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
QString when(const QDateTime& dt) { return dt.isValid() ? dt.toString(QStringLiteral("dd/MM/yyyy HH:mm")) : QStringLiteral("—"); }
QPushButton* smallButton(const QString& text, const char* role) {
    auto* b = ui::button(text, role);
    b->setStyleSheet(QStringLiteral("padding:5px 10px;font-size:12px;border-radius:8px;"));
    return b;
}
} // namespace

BugView::BugView(TestCaseStore& cases, SettingsStore& settings, BugReportService& bugs, BugStore& ledger, EvidenceService& evidence, QWidget* parent)
    : QWidget(parent), m_cases(cases), m_settings(settings), m_bugs(bugs), m_ledger(ledger), m_evidence(evidence) {
    auto* root = ui::hbox(this, 0, 0);
    QWidget* content;
    QVBoxLayout* outer;
    auto* sa = ui::scrollArea(&content, &outer);
    outer->setContentsMargins(32, 28, 32, 28);
    auto* page = new QWidget;
    page->setMaximumWidth(860);
    auto* v = ui::vbox(page, 0, 18);
    outer->addWidget(page, 0, Qt::AlignTop);
    root->addWidget(sa, 1);

    auto* head = new QWidget;
    auto* hv = ui::vbox(head, 0, 0);
    m_eyebrow = ui::label(QString(), "eyebrow");
    m_eyebrow->setTextFormat(Qt::RichText);
    hv->addWidget(m_eyebrow);
    hv->addWidget(ui::label(tr("Reportar bug"), "h1"));
    v->addWidget(head);

    buildForm(v);
    buildLists(v);

    connect(&m_cases, &TestCaseStore::caseChanged, this, [this](const QString& id) { if (id == m_cases.selectedId()) refreshShots(); });
    connect(&m_settings, &SettingsStore::trackerChanged, this, [this]() { refreshHeader(); refreshTrackerFields(); });
    connect(&m_bugs, &BugReportService::metadataChanged, this, &BugView::refreshTrackerFields);
    connect(&m_ledger, &BugStore::bugsChanged, this, [this]() { refreshIssues(); refreshPending(); });
    refreshHeader();
    refreshTrackerFields();
    refreshIssues();
    refreshPending();
}

void BugView::buildForm(QVBoxLayout* v) {
    auto* card = ui::card("card-lg");
    auto* ch = ui::hbox(card, 0, 0);
    ch->addWidget(ui::accentBar(theme::Red));
    auto* body = new QWidget;
    auto* bv = ui::vbox(body, 0, 16);
    bv->setContentsMargins(22, 22, 24, 22);

    m_title = new QLineEdit;
    m_title->setPlaceholderText(tr("Resumen corto: qué falla y dónde"));
    m_title->setStyleSheet(QStringLiteral("font-size:14px;padding:9px 12px;"));
    connect(m_title, &QLineEdit::textChanged, this, [this]() { if (m_touched) ui::setFlag(m_title, "invalid", m_title->text().trimmed().isEmpty()); });
    bv->addWidget(field(tr("Título"), m_title));

    auto* meta = new QWidget;
    auto* mg = new QGridLayout(meta);
    mg->setContentsMargins(0, 0, 0, 0);
    mg->setHorizontalSpacing(12);
    m_severity = new QComboBox;
    for (const auto& s : BugReport::severities()) m_severity->addItem(BugReport::severityLabel(s), s);
    connect(m_severity, &QComboBox::currentIndexChanged, this, [this](int) {
        // La severidad sugiere la prioridad de Jira mientras el usuario no haya elegido otra.
        const QString sev = m_severity->currentData().toString();
        if (m_settings.tracker().kind == TrackerKind::Jira && m_priority->currentText().isEmpty()) m_priority->setCurrentText(BugReport::jiraPriorityFor(sev));
    });
    m_env = new QComboBox;
    for (const auto& e : BugReport::environments()) m_env->addItem(BugReport::environmentLabel(e), e);
    m_linkedCase = new QLabel;
    m_linkedCase->setStyleSheet(QStringLiteral("background:%1;border:1px solid %2;border-radius:9px;padding:8px 10px;font-family:'Consolas','DejaVu Sans Mono',monospace;color:%3;").arg(theme::Elevated, theme::Border, theme::Muted));
    mg->addWidget(field(tr("Severidad"), m_severity), 0, 0);
    mg->addWidget(field(tr("Entorno"), m_env), 0, 1);
    mg->addWidget(field(tr("Caso vinculado"), m_linkedCase), 0, 2);
    for (int i = 0; i < 3; ++i) mg->setColumnStretch(i, 1);
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
    m_issueType = editableCombo(QStringLiteral("Bug"));
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
    connect(m_assigneeSearch, &QTimer::timeout, this, &BugView::searchAssignees);
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
    connect(capture, &QPushButton::clicked, this, &BugView::captureRequested);
    shh->addWidget(capture);
    auto* attach = ui::button(tr("+ Adjuntar archivo…"), "dashed");
    attach->setToolTip(tr("Adjunta logs, vídeos o imágenes existentes; se suben al gestor con el bug"));
    connect(attach, &QPushButton::clicked, this, [this]() { m_evidence.attachFiles(evidence::pickFiles(this)); });
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
    connect(cancel, &QPushButton::clicked, this, &BugView::cancelled);
    m_submit = ui::button(QString(), "primary");
    connect(m_submit, &QPushButton::clicked, this, &BugView::submit);
    ah->addWidget(cancel);
    ah->addWidget(m_submit);
    v->addWidget(actions);
}

void BugView::buildLists(QVBoxLayout* v) {
    // Pendientes de envío (sólo visible si hay)
    m_pendingBlock = ui::card("card");
    auto* ph = ui::hbox(m_pendingBlock, 0, 0);
    ph->addWidget(ui::accentBar(theme::Amber));
    auto* pbody = new QWidget;
    auto* pv = ui::vbox(pbody, 0, 8);
    pv->setContentsMargins(18, 14, 18, 14);
    auto* phead = new QWidget;
    auto* phh = ui::hbox(phead, 0, 8);
    m_pendingHeader = ui::label(QString(), "eyebrow");
    m_pendingHeader->setStyleSheet(QStringLiteral("color:%1;").arg(theme::AmberSoft));
    phh->addWidget(m_pendingHeader, 1);
    m_retry = smallButton(tr("Reintentar envío"), "outline");
    connect(m_retry, &QPushButton::clicked, this, &BugView::retryPending);
    phh->addWidget(m_retry);
    pv->addWidget(phead);
    auto* plist = new QWidget;
    m_pendingList = ui::vbox(plist, 0, 6);
    pv->addWidget(plist);
    ph->addWidget(pbody, 1);
    v->addWidget(m_pendingBlock);

    // Reportados
    auto* ihead = new QWidget;
    auto* ih = ui::hbox(ihead, 0, 8);
    m_issuesHeader = ui::label(QString(), "eyebrow");
    ih->addWidget(m_issuesHeader, 1);
    m_refreshStatuses = smallButton(tr("Actualizar estados"), "outline");
    connect(m_refreshStatuses, &QPushButton::clicked, this, &BugView::refreshStatuses);
    ih->addWidget(m_refreshStatuses);
    v->addWidget(ihead);
    auto* ilist = new QWidget;
    m_issuesList = ui::vbox(ilist, 0, 6);
    v->addWidget(ilist);
}

// ---- Refrescos -----------------------------------------------------------------------------

void BugView::refreshHeader() {
    const TrackerSettings& t = m_settings.tracker();
    m_eyebrow->setText(tr("NUEVO DEFECTO · DESTINO %1 <span style=\"color:%2;font-family:monospace\">%3</span>")
                           .arg(toString(t.kind).toUpper(), theme::Blue, t.project.isEmpty() ? tr("(sin proyecto)") : t.project));
    m_submit->setText(tr("Crear en %1").arg(toString(t.kind)));
}

void BugView::refreshTrackerFields() {
    const TrackerSettings& t = m_settings.tracker();
    const ProjectMetadata& m = m_bugs.metadata();
    fill(m_issueType, m.issueTypes);
    fill(m_priority, m.priorities);
    setAssigneeOptions(m.assignees);
    const bool searches = m_bugs.searchesAssigneesOnServer();
    m_assignee->lineEdit()->setPlaceholderText(searches ? tr("Escribe para buscar en %1").arg(toString(t.kind)) : tr("Sin asignar"));
    m_assignee->setToolTip(searches ? tr("Las personas se buscan en %1 según escribes; no hace falta cargarlas antes").arg(toString(t.kind))
                                    : tr("Personas del proyecto cargadas con «Cargar valores del proyecto»"));
    if (m_issueType->currentText().isEmpty()) m_issueType->setCurrentText(t.kind == TrackerKind::GitLab ? QStringLiteral("issue") : t.kind == TrackerKind::GitHub ? QStringLiteral("Issue") : QStringLiteral("Bug"));
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

void BugView::loadDraft() {
    const BugReport d = m_bugs.draftFromCurrentContext();
    m_touched = false;
    ui::setFlag(m_title, "invalid", false);
    ui::setFlag(m_actual, "invalid", false);
    m_title->setText(d.title);
    m_severity->setCurrentIndex(std::max(0, m_severity->findData(d.severity)));
    m_env->setCurrentIndex(std::max(0, m_env->findData(d.environment)));
    m_linkedCase->setText(d.linkedCaseId.isEmpty() ? QStringLiteral("—") : d.linkedCaseId);
    m_priority->setCurrentText(m_settings.tracker().kind == TrackerKind::Jira ? d.priority : QString());
    m_assignee->setCurrentText(QString());
    m_components->setText(d.components.join(QStringLiteral(", ")));
    m_versions->clear();
    m_labels->clear();
    m_steps->setTextSilently(d.stepsToReproduce);
    m_expected->setTextSilently(d.expected);
    m_actual->setTextSilently(d.actual);
    refreshShots();
    refreshTrackerFields();
    if (m_settings.tracker().connected && !m_bugs.hasMetadata()) loadMetadata(false);
}

void BugView::refreshShots() {
    ui::clearLayout(m_shotsRow);
    const TestCase* c = m_cases.selected();
    const int n = c ? c->shots.size() : 0;
    m_shotsHeader->setText(tr("ADJUNTOS · %1").arg(n));
    if (!c) return;
    const QString id = c->id;
    for (const auto& s : c->shots) {
        auto* card = new ShotCard(s, c->steps, ShotCard::Layout::Compact);
        connect(card, &ShotCard::removeRequested, this, [this, id](int shotId) { m_cases.removeShot(id, shotId); });
        evidence::wireCard(card, this, m_cases, m_evidence, id);
        m_shotsRow->addWidget(card);
    }
}

void BugView::refreshIssues() {
    ui::clearLayout(m_issuesList);
    const auto& issues = m_ledger.issues();
    m_issuesHeader->setText(tr("BUGS REPORTADOS · %1 · %2 ABIERTOS").arg(issues.size()).arg(m_ledger.openIssueCount()));
    m_refreshStatuses->setVisible(!issues.isEmpty());
    if (issues.isEmpty()) {
        m_issuesList->addWidget(ui::label(tr("Todavía no se ha reportado ningún bug desde QAflow."), "muted-sm"));
        return;
    }
    for (int i = issues.size() - 1; i >= 0; --i) {
        const IssueLink& l = issues[i];
        auto* row = ui::card("card-flat");
        auto* h = ui::hbox(row, 0, 10);
        h->setContentsMargins(12, 8, 10, 8);
        auto* key = ui::button(l.key, "ghost");
        key->setToolTip(tr("Abrir en %1").arg(l.tracker));
        key->setStyleSheet(QStringLiteral("padding:2px 8px;font-size:12px;font-weight:700;font-family:'Consolas','DejaVu Sans Mono',monospace;color:%1;").arg(theme::Blue));
        connect(key, &QPushButton::clicked, this, [this, url = l.url]() { emit openIssueRequested(url); });
        h->addWidget(key);
        auto* title = new QLabel(l.title.isEmpty() ? tr("(sin título)") : l.title);
        title->setWordWrap(true);
        h->addWidget(title, 1);
        if (!l.caseId.isEmpty()) h->addWidget(ui::label(l.caseId, "mono-muted"));
        h->addWidget(ui::label(when(l.createdAt), "muted-sm"));
        const QString statusText = l.status.isEmpty() ? tr("SIN CONSULTAR") : l.status.toUpper();
        h->addWidget(ui::pill(statusText, l.status.isEmpty() ? theme::tint(theme::Muted, 38) : l.resolved ? theme::Green : theme::tint(theme::Blue, 38),
                              l.status.isEmpty() ? theme::Muted : l.resolved ? theme::Bg : theme::Blue));
        m_issuesList->addWidget(row);
    }
}

void BugView::refreshPending() {
    const auto& pending = m_ledger.pending();
    m_pendingBlock->setVisible(!pending.isEmpty());
    ui::clearLayout(m_pendingList);
    if (pending.isEmpty()) return;
    m_pendingHeader->setText(tr("PENDIENTES DE ENVÍO · %1 · SE REINTENTAN AL CONECTAR").arg(pending.size()));
    for (const auto& p : pending) {
        auto* row = ui::card("card-flat");
        auto* h = ui::hbox(row, 0, 10);
        h->setContentsMargins(12, 8, 10, 8);
        h->addWidget(ui::label(p.id, "mono-muted"));
        auto* title = new QLabel(p.report.title);
        title->setWordWrap(true);
        h->addWidget(title, 1);
        h->addWidget(ui::label(tr("%1 · %2 intentos").arg(when(p.createdAt)).arg(p.attempts), "muted-sm"));
        auto* err = ui::label(ui::elide(p.lastError, 40), "muted-sm");
        err->setToolTip(p.lastError);
        err->setStyleSheet(QStringLiteral("font-size:11px;color:%1;").arg(theme::AmberSoft));
        h->addWidget(err);
        auto* discard = ui::button(QStringLiteral("×"), "icon");
        discard->setToolTip(tr("Descartar este bug pendiente"));
        discard->setFixedSize(26, 22);
        connect(discard, &QPushButton::clicked, this, [this, id = p.id]() { m_bugs.discardPending(id); });
        h->addWidget(discard);
        m_pendingList->addWidget(row);
    }
}

// ---- Acciones ------------------------------------------------------------------------------

void BugView::loadMetadata(bool force) {
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

void BugView::setAssigneeOptions(const QList<Assignee>& people) {
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

void BugView::searchAssignees() {
    const int seq = ++m_assigneeSeq;
    m_bugs.searchAssignees(m_assignee->lineEdit()->text(), [this, seq](const AssigneeSearch& r) {
        if (seq != m_assigneeSeq) return;   // ya se ha escrito otra cosa: esta respuesta no vale
        setAssigneeOptions(r.assignees);
        if (!r.ok) { m_metaNote->setText(tr("No se pudieron buscar personas · %1").arg(r.error)); return; }
        if (m_assignee->lineEdit()->hasFocus() && m_assignee->completer()) m_assignee->completer()->complete();
    });
}

BugReport BugView::collect() const {
    BugReport b;
    b.title = m_title->text();
    b.severity = m_severity->currentData().toString();
    b.environment = m_env->currentData().toString();
    b.linkedCaseId = m_linkedCase->text() == QStringLiteral("—") ? QString() : m_linkedCase->text();
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
    if (const TestCase* c = m_cases.selected()) for (const auto& s : c->shots) b.attachmentPaths << s.path;
    return b;
}

void BugView::submit() {
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
        } else if (r.queued) {
            emit toast(tr("Sin conexión con %1 · el bug queda en la cola y se reintentará").arg(tracker), theme::Amber);
            emit submitted(QString());
        } else {
            emit toast(tr("%1 rechazó el bug · %2").arg(tracker, r.error), theme::Red);
        }
    });
}

void BugView::retryPending() {
    if (m_busy) return;
    m_busy = true;
    m_retry->setEnabled(false);
    m_retry->setText(tr("Enviando…"));
    m_bugs.retryPending([this](const BugReportService::RetryResult& r) {
        m_busy = false;
        m_retry->setEnabled(true);
        m_retry->setText(tr("Reintentar envío"));
        if (r.sent > 0 && r.failed == 0) emit toast(tr("Enviados %1 bugs pendientes: %2").arg(r.sent).arg(r.keys.join(QStringLiteral(", "))), theme::Green);
        else if (r.sent > 0) emit toast(tr("Enviados %1 · %2 siguen pendientes").arg(r.sent).arg(r.failed), theme::Amber);
        else emit toast(tr("No se pudo enviar ninguno · sigue sin conexión"), theme::Red);
    });
}

void BugView::refreshStatuses() {
    if (m_busy) return;
    m_busy = true;
    m_refreshStatuses->setEnabled(false);
    m_refreshStatuses->setText(tr("Consultando…"));
    m_bugs.refreshStatuses(false, [this](const BugReportService::RefreshResult& r) {
        m_busy = false;
        m_refreshStatuses->setEnabled(true);
        m_refreshStatuses->setText(tr("Actualizar estados"));
        if (r.failed == 0) emit toast(tr("Estados actualizados · %1 bugs").arg(r.updated), theme::Green);
        else emit toast(tr("%1 actualizados · %2 sin respuesta").arg(r.updated).arg(r.failed), theme::Amber);
    });
}

} // namespace qaflow
