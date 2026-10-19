#include "BugDialog.h"

#include "application/BugReportService.h"
#include "application/EvidenceService.h"
#include "application/SettingsStore.h"
#include "application/TestCaseStore.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/AnnotationEditor.h"
#include "presentation/widgets/EvidenceActions.h"
#include "presentation/widgets/FlowLayout.h"
#include "presentation/widgets/Responsive.h"
#include "presentation/widgets/ImageViewer.h"
#include "presentation/widgets/ShotCard.h"
#include "presentation/widgets/TextArea.h"
#include "presentation/widgets/Ui.h"

#include <QComboBox>
#include <QCompleter>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>

#include <algorithm>

namespace qaflow {

namespace {
QWidget* field(const QString& title, QWidget* w, const QString& titleColor = QString()) {
    auto* box = new QWidget;
    auto* v = ui::vbox(box, 0, 6);
    auto* l = ui::label(title.toUpper(), "eyebrow");
    l->setWordWrap(true);   // con letras anchas (Segoe UI) no puede imponer el ancho del diálogo
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
QPushButton* removeButton(const QString& accessibleName) {
    auto* b = ui::button(QStringLiteral("×"), "icon");
    b->setAccessibleName(accessibleName);
    b->setToolTip(accessibleName);
    b->setFixedSize(30, 30);
    return b;
}
/// Página del asistente: su título y, debajo, lo que se le añada; se desplaza si no cabe.
QScrollArea* pageArea(const QString& title, QVBoxLayout** body) {
    QWidget* content;
    QVBoxLayout* outer;
    auto* sa = ui::scrollArea(&content, &outer);
    outer->setContentsMargins(28, 24, 28, 24);
    outer->setSpacing(18);
    outer->addWidget(ui::label(title, "h2"));
    *body = outer;
    return sa;
}
/// Rótulo de sección que parte línea si no cabe, en vez de imponer su ancho al diálogo.
QLabel* eyebrow(const QString& text) {
    auto* l = ui::label(text, "eyebrow");
    l->setWordWrap(true);
    return l;
}
/// Los valores de `text` (separados por comas) que están en `known`, con la ortografía del gestor;
/// `unknown` recibe los que no. Sin lista conocida todo vale.
QStringList knownValues(const QString& text, const QStringList& known, QStringList* unknown = nullptr) {
    const QStringList values = parseTags(text);
    if (known.isEmpty()) return values;
    QStringList out;
    for (const auto& v : values) {
        const auto it = std::find_if(known.cbegin(), known.cend(), [&](const QString& k) { return k.compare(v, Qt::CaseInsensitive) == 0; });
        if (it != known.cend()) out << *it;
        else if (unknown) *unknown << v;
    }
    return out;
}
/// Campo de valores separados por comas con un botón que despliega los que tiene el proyecto, para
/// marcarlos en vez de escribirlos.
QWidget* withPicker(QLineEdit* edit, const QString& tooltip, std::function<QStringList()> options) {
    auto* box = new QWidget;
    auto* h = ui::hbox(box, 0, 6);
    h->addWidget(edit, 1);
    // Un botón del tema con menú (no un QToolButton, que el tema no pinta); sin la flecha de serie.
    auto* pick = smallButton(QStringLiteral("▾"), "ghost");
    pick->setStyleSheet(pick->styleSheet() + QStringLiteral("QPushButton::menu-indicator{image:none;width:0;}"));
    pick->setToolTip(tooltip);
    pick->setAccessibleName(tooltip);
    auto* menu = new QMenu(pick);
    pick->setMenu(menu);
    QObject::connect(menu, &QMenu::aboutToShow, menu, [menu, edit, options]() {
        menu->clear();
        const QStringList all = options();
        if (all.isEmpty()) { menu->addAction(BugDialog::tr("Carga los valores del proyecto"))->setEnabled(false); return; }
        const QStringList chosen = parseTags(edit->text());
        for (const auto& o : all) {
            QAction* a = menu->addAction(o);
            a->setCheckable(true);
            a->setChecked(chosen.contains(o, Qt::CaseInsensitive));
            QObject::connect(a, &QAction::toggled, edit, [edit, o](bool on) {
                QStringList now = parseTags(edit->text());
                now.removeIf([&o](const QString& x) { return x.compare(o, Qt::CaseInsensitive) == 0; });
                if (on) now << o;
                edit->setText(now.join(QStringLiteral(", ")));
            });
        }
    });
    h->addWidget(pick);
    return box;
}
} // namespace

BugDialog::BugDialog(TestCaseStore& cases, SettingsStore& settings, BugReportService& bugs, EvidenceService& evidence, QWidget* parent)
    : QDialog(parent), m_cases(cases), m_settings(settings), m_bugs(bugs), m_evidence(evidence) {
    setObjectName(QStringLiteral("bugDialog"));
    setWindowTitle(tr("Reportar bug"));
    setWindowIcon(ui::appIcon());
    if (const QScreen* screen = QGuiApplication::primaryScreen())
        resize(std::min(1060, screen->availableGeometry().width() - 80), std::min(820, screen->availableGeometry().height() - 80));
    else
        resize(1060, 820);

    auto* root = ui::hbox(this, 0, 0);
    root->addWidget(buildRail());
    auto* main = new QWidget;
    auto* mv = ui::vbox(main, 0, 0);
    m_pages = new QStackedWidget;
    m_pages->setObjectName(QStringLiteral("bugPages"));
    m_pages->addWidget(buildWhatHappened());
    m_pages->addWidget(buildContext());
    m_pages->addWidget(buildReview());
    mv->addWidget(m_pages, 1);
    mv->addWidget(buildFooter());
    root->addWidget(main, 1);
    onBreakpoint(this, 980, [this](bool narrow) { fitRail(narrow); });

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
    showPage(WhatHappened);
}

// ---- Construcción --------------------------------------------------------------------------

QWidget* BugDialog::buildRail() {
    auto* rail = ui::card("rail");
    rail->setObjectName(QStringLiteral("bugRail"));
    rail->setFixedWidth(220);
    m_rail = rail;
    auto* v = ui::vbox(rail, 0, 6);
    v->setContentsMargins(16, 22, 16, 18);
    m_eyebrow = ui::label(QString(), "eyebrow");
    m_eyebrow->setTextFormat(Qt::RichText);
    m_eyebrow->setWordWrap(true);
    v->addWidget(m_eyebrow);
    m_railTitle = ui::label(tr("Reportar bug"), "h1-sm");
    v->addWidget(m_railTitle);
    v->addSpacing(10);

    const QString names[3] = {tr("Qué pasó"), tr("Contexto"), tr("Revisar y enviar")};
    const QString subs[3] = {tr("Resultado y pasos"), tr("Entorno, usuarios, evidencia"), tr("Título y campos del gestor")};
    for (int i = 0; i < 3; ++i) {
        auto* b = ui::button(QString(), "nav");
        b->setObjectName(QStringLiteral("bugNav%1").arg(i));
        b->setAccessibleName(QStringLiteral("%1. %2").arg(i + 1).arg(names[i]));
        b->setToolTip(QStringLiteral("%1 · %2").arg(names[i], subs[i]));
        b->setMinimumHeight(56);
        auto* h = ui::hbox(b, 0, 10);
        h->setContentsMargins(10, 8, 10, 8);
        m_navBadge[i] = new QLabel(QString::number(i + 1));
        m_navBadge[i]->setFixedSize(24, 24);
        m_navBadge[i]->setAlignment(Qt::AlignCenter);
        h->addWidget(m_navBadge[i], 0, Qt::AlignTop);
        auto* texts = new QWidget;
        auto* tv = ui::vbox(texts, 0, 2);
        auto* name = new QLabel(names[i]);
        name->setWordWrap(true);
        tv->addWidget(name);
        auto* sub = ui::label(subs[i], "muted-sm");
        sub->setWordWrap(true);
        tv->addWidget(sub);
        h->addWidget(texts, 1);
        m_navText[i] = texts;
        for (auto* child : b->findChildren<QWidget*>()) child->setAttribute(Qt::WA_TransparentForMouseEvents);
        connect(b, &QPushButton::clicked, this, [this, i]() { showPage(i); });
        m_nav[i] = b;
        v->addWidget(b);
    }
    v->addStretch(1);
    m_context = ui::label(QString(), "muted-sm");
    m_context->setObjectName(QStringLiteral("bugContext"));
    m_context->setTextFormat(Qt::RichText);
    m_context->setWordWrap(true);
    v->addWidget(m_context);
    return rail;
}

void BugDialog::fitRail(bool narrow) {
    // Estrecho (pantallas pequeñas o con zoom), el carril se queda en los números de los pasos —el nombre,
    // en el tooltip— para dejarle el ancho al formulario.
    m_rail->setFixedWidth(narrow ? 72 : 220);
    m_rail->layout()->setContentsMargins(narrow ? 10 : 16, 22, narrow ? 10 : 16, 18);
    m_eyebrow->setVisible(!narrow);
    m_railTitle->setVisible(!narrow);
    m_context->setVisible(!narrow);
    m_rail->ensurePolished();
    for (int i = 0; i < 3; ++i) {
        m_navText[i]->setVisible(!narrow);
        // Un QPushButton no deja que su layout le pida alto por ancho: con textos que parten línea (más en
        // Windows, con letras más anchas) se lo damos a mano para el ancho que le deja el carril.
        const int inner = m_rail->width() - m_rail->layout()->contentsMargins().left() - m_rail->layout()->contentsMargins().right();
        const int needed = narrow ? 40 : m_nav[i]->layout()->totalHeightForWidth(inner);
        m_nav[i]->setMinimumHeight(std::max(narrow ? 40 : 56, needed));
    }
}

QWidget* BugDialog::buildWhatHappened() {
    QVBoxLayout* v;
    auto* sa = pageArea(tr("¿Qué pasó?"), &v);

    m_actual = new TextArea(4);
    m_actual->setObjectName(QStringLiteral("bugActual"));
    m_actual->setProperty("role", QStringLiteral("danger-soft"));
    m_actual->setPlaceholderText(tr("Qué ocurrió realmente"));
    connect(m_actual, &TextArea::edited, this, [this]() { if (m_touched) ui::setFlag(m_actual, "invalid", m_actual->toPlainText().trimmed().isEmpty()); });
    v->addWidget(field(tr("Resultado actual *"), m_actual, theme::RedSoft));
    m_expected = new TextArea(3);
    m_expected->setObjectName(QStringLiteral("bugExpected"));
    v->addWidget(field(tr("Resultado esperado"), m_expected));

    auto* steps = new QWidget;
    auto* sv = ui::vbox(steps, 0, 8);
    sv->addWidget(eyebrow(tr("PASOS PARA REPRODUCIR")));
    auto* rows = new QWidget;
    m_stepRows = ui::vbox(rows, 0, 6);
    sv->addWidget(rows);
    auto* actions = new QWidget;
    auto* ah = new FlowLayout(actions, 0, 8, 6);
    auto* add = ui::button(tr("+ Añadir paso"), "dashed");
    add->setObjectName(QStringLiteral("bugAddStep"));
    connect(add, &QPushButton::clicked, this, [this]() { addStepRow(QString(), -1, true); });
    ah->addWidget(add);
    auto* import = smallButton(tr("Importar pasos del caso"), "ghost");
    import->setObjectName(QStringLiteral("bugImportSteps"));
    import->setToolTip(tr("Sustituye la lista por los pasos del caso, con sus datos de prueba, para editarlos"));
    connect(import, &QPushButton::clicked, this, &BugDialog::importCaseSteps);
    ah->addWidget(import);
    sv->addWidget(actions);
    auto* hint = ui::label(tr("Los pasos que de verdad llevan al fallo, que pueden no ser los del caso. Enter añade otro."), "muted-sm");
    hint->setWordWrap(true);
    sv->addWidget(hint);
    v->addWidget(steps);
    v->addStretch(1);
    return sa;
}

QWidget* BugDialog::buildContext() {
    QVBoxLayout* v;
    auto* sa = pageArea(tr("Contexto de la prueba"), &v);

    m_env = editableCombo(tr("Dónde se probó"));
    m_env->setObjectName(QStringLiteral("bugEnvironment"));
    m_envNote = ui::label(QString(), "muted-sm");
    m_envNote->setObjectName(QStringLiteral("bugEnvironmentNote"));
    m_envNote->setWordWrap(true);
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
    // Un bug es de un paso concreto: es lo que hace que al publicar la ejecución el defecto cuelgue
    // del resultado de ese paso y no del caso entero.
    m_linkedStep = new QComboBox;
    m_linkedStep->setObjectName(QStringLiteral("bugStep"));
    m_linkedStep->setToolTip(tr("Paso en el que se vio el fallo; el defecto se cuelga de él al publicar la ejecución"));
    connect(m_linkedStep, &QComboBox::currentIndexChanged, this, [this](int) { refreshContext(); });

    auto* env = new QWidget;
    auto* ev = ui::vbox(env, 0, 6);
    ev->addWidget(field(tr("Entorno"), m_env));
    ev->addWidget(m_envNote);
    // Hasta dos columnas, y una sola si no caben (ventana estrecha o letra ancha): el paso, que lleva su
    // acción, tiene sitio.
    for (QComboBox* c : {m_env, m_severity, m_classification, m_linkedStep}) {
        c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        c->setMinimumContentsLength(12);
    }
    auto* meta = new AutoGrid(200, 2);
    meta->addWidget(env);
    meta->addWidget(field(tr("Severidad"), m_severity));
    meta->addWidget(field(tr("Clasificación"), m_classification));
    meta->addWidget(field(tr("Paso vinculado"), m_linkedStep));
    v->addWidget(meta);

    auto* users = new QWidget;
    auto* uv = ui::vbox(users, 0, 8);
    uv->addWidget(eyebrow(tr("USUARIOS USADOS EN EL SISTEMA")));
    auto* rows = new QWidget;
    m_userRows = ui::vbox(rows, 0, 6);
    uv->addWidget(rows);
    auto* actions = new QWidget;
    auto* recent = new FlowLayout(actions, 0, 6, 6);
    auto* add = ui::button(tr("+ Añadir usuario"), "dashed");
    add->setObjectName(QStringLiteral("bugAddUser"));
    connect(add, &QPushButton::clicked, this, [this]() { addUserRow(TestUser{}, true); });
    recent->addWidget(add);
    m_recentUsers = recent;
    uv->addWidget(actions);
    auto* hint = ui::label(tr("Usuario y rol o perfil, sin contraseñas. Se recuerdan para los próximos partes."), "muted-sm");
    hint->setWordWrap(true);
    uv->addWidget(hint);
    v->addWidget(users);

    auto* shotsBlock = new QWidget;
    auto* sv = ui::vbox(shotsBlock, 0, 8);
    auto* shHead = new QWidget;
    auto* shh = new FlowLayout(shHead, 0, 8, 6);
    m_shotsHeader = ui::label(QString(), "eyebrow");
    m_shotsHeader->setMinimumWidth(120);
    m_shotsHeader->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    shh->addWidget(m_shotsHeader);
    auto* capture = ui::button(tr("+ Capturar pantalla"), "dashed");
    capture->setObjectName(QStringLiteral("bugCapture"));
    capture->setToolTip(tr("La ventana se esconde mientras se captura y vuelve con la imagen adjunta"));
    connect(capture, &QPushButton::clicked, this, &BugDialog::captureScreen);
    shh->addWidget(capture);
    auto* attach = ui::button(tr("+ Adjuntar archivo…"), "dashed");
    attach->setToolTip(tr("Adjunta logs, vídeos o imágenes existentes; se suben al gestor con el bug"));
    connect(attach, &QPushButton::clicked, this, [this]() { addShots(m_evidence.copyForBug(evidence::pickFiles(this))); });
    shh->addWidget(attach);
    m_shotsHeader->setMinimumHeight(capture->sizeHint().height());   // a la altura de los botones
    sv->addWidget(shHead);
    auto* shots = new QWidget;
    m_shotsRow = new FlowLayout(shots, 0, 8, 8);
    sv->addWidget(shots);
    v->addWidget(shotsBlock);
    v->addStretch(1);
    return sa;
}

QWidget* BugDialog::buildReview() {
    QVBoxLayout* v;
    auto* sa = pageArea(tr("Revisar y enviar"), &v);

    auto* titleCard = ui::card("card-flat");
    auto* tv = ui::vbox(titleCard, 0, 8);
    tv->setContentsMargins(16, 14, 16, 14);
    tv->addWidget(eyebrow(tr("TÍTULO · PROPUESTO CON LO QUE ESCRIBISTE")));
    m_title = new QLineEdit;
    m_title->setObjectName(QStringLiteral("bugTitle"));
    m_title->setPlaceholderText(tr("Resumen corto: qué falla y dónde"));
    m_title->setStyleSheet(QStringLiteral("font-size:14px;padding:9px 12px;"));
    connect(m_title, &QLineEdit::textEdited, this, [this]() { m_titleEdited = true; });
    connect(m_title, &QLineEdit::textChanged, this, [this]() { if (m_touched) ui::setFlag(m_title, "invalid", m_title->text().trimmed().isEmpty()); });
    tv->addWidget(m_title);
    auto* suggestions = new QWidget;
    m_titleSuggestions = new FlowLayout(suggestions, 0, 6, 6);
    tv->addWidget(suggestions);
    v->addWidget(titleCard);

    // Rótulo y botón en una fila mientras quepan; la nota, debajo, para que nada imponga el ancho.
    auto* trackerHead = new QWidget;
    auto* th = new FlowLayout(trackerHead, 0, 8, 6);
    auto* trackerTitle = ui::label(tr("CAMPOS DEL GESTOR"), "eyebrow");
    th->addWidget(trackerTitle);
    m_loadMeta = smallButton(tr("Cargar valores del proyecto"), "outline");
    connect(m_loadMeta, &QPushButton::clicked, this, [this]() { loadMetadata(true); });
    th->addWidget(m_loadMeta);
    trackerTitle->setMinimumHeight(m_loadMeta->sizeHint().height());   // a la altura del botón
    v->addWidget(trackerHead);
    m_metaNote = ui::label(QString(), "muted-sm");
    m_metaNote->setStyleSheet(QStringLiteral("font-size:11px;"));
    m_metaNote->setWordWrap(true);
    v->addWidget(m_metaNote);

    // Hasta tres columnas, y menos si no caben.
    auto* tf = new AutoGrid(170, 3);
    m_issueType = editableCombo(BugReport::defaultIssueType({}));
    m_issueType->setObjectName(QStringLiteral("bugIssueType"));
    connect(m_issueType, &QComboBox::activated, this, [this]() { m_issueTypeEdited = true; refreshUnknownValues(); });
    connect(m_issueType->lineEdit(), &QLineEdit::textEdited, this, [this]() { m_issueTypeEdited = true; refreshUnknownValues(); });
    m_issueTypeNote = ui::label(QString(), "warn");
    m_issueTypeNote->setWordWrap(true);
    auto* type = new QWidget;
    auto* typeV = ui::vbox(type, 0, 4);
    typeV->addWidget(field(tr("Tipo"), m_issueType));
    typeV->addWidget(m_issueTypeNote);
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
    m_components->setObjectName(QStringLiteral("bugComponents"));
    m_components->setPlaceholderText(tr("Separados por comas"));
    connect(m_components, &QLineEdit::textChanged, this, &BugDialog::refreshUnknownValues);
    m_versions = new QLineEdit;
    m_versions->setPlaceholderText(tr("Separadas por comas"));
    connect(m_versions, &QLineEdit::textChanged, this, &BugDialog::refreshUnknownValues);
    m_labels = new QLineEdit;
    m_labels->setPlaceholderText(tr("Separadas por comas · siempre: qaflow, <caso>"));
    m_unknownNote = ui::label(QString(), "warn");
    m_unknownNote->setObjectName(QStringLiteral("bugUnknownValues"));
    m_unknownNote->setWordWrap(true);
    tf->addWidget(type);
    tf->addWidget(field(tr("Prioridad"), m_priority));
    tf->addWidget(field(tr("Asignado a"), m_assignee));
    tf->addWidget(field(tr("Componentes"), withPicker(m_components, tr("Elegir componentes del proyecto"), [this]() { return m_bugs.metadata().components; })));
    tf->addWidget(field(tr("Versión afectada"), withPicker(m_versions, tr("Elegir versiones del proyecto"), [this]() { return m_bugs.metadata().versions; })));
    tf->addWidget(field(tr("Etiquetas"), m_labels));
    for (QComboBox* c : {m_issueType, m_priority, m_assignee}) {
        c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        c->setMinimumContentsLength(8);
    }
    v->addWidget(tf);
    v->addWidget(m_unknownNote);

    m_summary = ui::label(QString(), "muted-sm");
    m_summary->setObjectName(QStringLiteral("bugSummary"));
    m_summary->setWordWrap(true);
    v->addWidget(m_summary);
    v->addStretch(1);
    return sa;
}

QWidget* BugDialog::buildFooter() {
    auto* footer = ui::card("dialog-footer");
    auto* h = ui::hbox(footer, 0, 10);
    h->setContentsMargins(28, 12, 28, 12);
    auto* cancel = ui::button(tr("Cancelar"), "ghost");
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    h->addWidget(cancel);
    h->addStretch(1);
    m_back = ui::button(tr("Atrás"), "ghost");
    m_back->setObjectName(QStringLiteral("bugBack"));
    connect(m_back, &QPushButton::clicked, this, [this]() { showPage(page() - 1); });
    h->addWidget(m_back);
    m_next = ui::button(tr("Siguiente"), "primary");
    m_next->setObjectName(QStringLiteral("bugNext"));
    connect(m_next, &QPushButton::clicked, this, [this]() { showPage(page() + 1); });
    h->addWidget(m_next);
    m_submit = ui::button(QString(), "primary");
    m_submit->setObjectName(QStringLiteral("bugSubmit"));
    connect(m_submit, &QPushButton::clicked, this, &BugDialog::submit);
    h->addWidget(m_submit);
    return footer;
}

// ---- Navegación ----------------------------------------------------------------------------

int BugDialog::page() const { return m_pages->currentIndex(); }

void BugDialog::showPage(int index) {
    index = std::clamp(index, 0, m_pages->count() - 1);
    m_pages->setCurrentIndex(index);
    if (index == Review) {
        refreshTitle();
        refreshSummary();
    }
    m_back->setVisible(index > WhatHappened);
    m_next->setVisible(index < Review);
    m_submit->setVisible(index == Review);
    (index == Review ? m_submit : m_next)->setDefault(true);
    refreshNav();
}

void BugDialog::refreshNav() {
    for (int i = 0; i < 3; ++i) {
        const bool current = i == page();
        const bool done = i < page();
        ui::setFlag(m_nav[i], "active", current);
        m_navBadge[i]->setText(done ? QStringLiteral("✓") : QString::number(i + 1));
        m_navBadge[i]->setStyleSheet(QStringLiteral("border-radius:12px;font-weight:700;font-size:12px;background:%1;color:%2;")
                                         .arg(current ? theme::Blue : theme::Elevated, current ? theme::OnAccent : done ? theme::Green : theme::Muted));
    }
}

// ---- Refrescos -----------------------------------------------------------------------------

void BugDialog::refreshHeader() {
    const TrackerSettings& t = m_settings.tracker();
    m_eyebrow->setText(tr("NUEVO DEFECTO · %1 <span style=\"color:%2;font-family:monospace\">%3</span>")
                           .arg(toString(t.kind).toUpper(), theme::Blue, t.project.isEmpty() ? tr("(sin proyecto)") : t.project));
    m_submit->setText(tr("Crear en %1").arg(toString(t.kind)));
}

void BugDialog::refreshContext() {
    QStringList lines;
    if (!m_linkedCaseId.isEmpty()) {
        const TestCase* c = m_cases.find(m_linkedCaseId);
        lines << tr("Caso <span style=\"font-family:monospace;color:%1\">%2</span>%3")
                     .arg(theme::TextSoft, m_linkedCaseId, c && !c->suite.isEmpty() ? QStringLiteral(" · ") + c->suite.toHtmlEscaped() : QString());
    }
    if (m_linkedStep->currentData().toInt() > 0) lines << m_linkedStep->currentText().toHtmlEscaped();
    const QString env = m_env->currentText().trimmed();
    if (!m_linkedPlanRunId.isEmpty()) {
        const QString cycle = m_bugs.cycleName();
        lines << tr("Ciclo %1 · entorno <b style=\"color:%2\">%3</b>").arg((cycle.isEmpty() ? m_linkedPlanRunId : cycle).toHtmlEscaped(), theme::Green, env.toHtmlEscaped());
    } else if (!env.isEmpty()) {
        lines << tr("Entorno <b>%1</b>").arg(env.toHtmlEscaped());
    }
    m_context->setText(lines.join(QStringLiteral("<br>")));
}

void BugDialog::refreshSummary() {
    const BugReport b = collect();
    QStringList parts;
    const auto count = [](qsizetype n, const QString& one, const QString& many) { return (n == 1 ? one : many).arg(n); };
    parts << count(b.stepsToReproduce.size(), tr("%1 paso"), tr("%1 pasos"));
    parts << count(b.testUsers.size(), tr("%1 usuario"), tr("%1 usuarios"));
    parts << count(b.attachmentPaths.size(), tr("%1 adjunto"), tr("%1 adjuntos"));
    if (!b.environment.isEmpty()) parts << b.environment;
    parts << BugReport::severityLabel(b.severity);
    m_summary->setText(tr("El parte lleva: %1").arg(parts.join(QStringLiteral(" · "))));
}

void BugDialog::refreshTitle() {
    const QStringList options = m_bugs.titleSuggestions(collect());
    if (!m_titleEdited && !options.isEmpty()) m_title->setText(options.first());
    ui::clearLayout(m_titleSuggestions);
    for (const auto& o : options) {
        if (o == m_title->text()) continue;
        auto* chip = ui::button(ui::elide(o, 56), "chip-lg");
        chip->setObjectName(QStringLiteral("bugTitleSuggestion"));
        chip->setToolTip(o);
        connect(chip, &QPushButton::clicked, this, [this, o]() {
            m_title->setText(o);
            m_titleEdited = true;   // elegido: ya no se cambia solo
            refreshTitle();
        });
        m_titleSuggestions->addWidget(chip);
    }
}

void BugDialog::refreshEnvironment(const QString& environment) {
    QStringList options = BugReport::environments();
    const QString cycleEnv = m_linkedPlanRunId.isEmpty() ? QString() : m_bugs.cycleEnvironment();
    if (!cycleEnv.isEmpty() && !options.contains(cycleEnv, Qt::CaseInsensitive)) options.prepend(cycleEnv);
    if (!environment.isEmpty() && !options.contains(environment, Qt::CaseInsensitive)) options.prepend(environment);
    m_env->clear();
    for (const auto& e : options) m_env->addItem(BugReport::environmentLabel(e), e);
    const int at = m_env->findData(environment, Qt::UserRole, Qt::MatchFixedString);
    m_env->setCurrentIndex(std::max(0, at));
    if (!cycleEnv.isEmpty()) {
        const QString cycle = m_bugs.cycleName();
        m_envNote->setText(cycle.isEmpty() ? tr("Tomado del ciclo en curso") : tr("Tomado del ciclo «%1»").arg(cycle));
    } else {
        m_envNote->setText(tr("Sin ciclo en curso: indica dónde se probó"));
    }
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
    // El tipo de un bug se llama distinto en cada proyecto («Bug», «Error»…): mientras nadie lo
    // cambie se propone el que tiene el proyecto, y en cuanto llegan sus tipos se corrige.
    const QString defaultType = t.kind == TrackerKind::GitLab   ? QStringLiteral("issue")
                                : t.kind == TrackerKind::GitHub ? QStringLiteral("Issue")
                                                                : BugReport::defaultIssueType(m.issueTypes);
    if (!m_issueTypeEdited || m_issueType->currentText().trimmed().isEmpty()) m_issueType->setCurrentText(defaultType);
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
    refreshUnknownValues();
}

void BugDialog::refreshUnknownValues() {
    // Sólo Jira rechaza el issue por un componente, una versión o un tipo que el proyecto no tiene; en
    // los demás gestores son etiquetas libres.
    const TrackerSettings& t = m_settings.tracker();
    const ProjectMetadata& m = m_bugs.metadata();
    const bool jira = t.kind == TrackerKind::Jira;
    const QString project = t.project.isEmpty() ? toString(t.kind) : t.project;
    QStringList unknown;
    if (jira) {
        knownValues(m_components->text(), m.components, &unknown);
        knownValues(m_versions->text(), m.versions, &unknown);
    }
    QStringList quoted;
    for (const auto& u : unknown) quoted << QStringLiteral("«%1»").arg(u);
    m_unknownNote->setText(quoted.isEmpty()            ? QString()
                           : quoted.size() == 1 ? tr("%1 no existe en %2: no se enviará").arg(quoted.first(), project)
                                                : tr("%1 no existen en %2: no se enviarán").arg(quoted.join(QStringLiteral(", ")), project));
    m_unknownNote->setVisible(!quoted.isEmpty());
    const QString type = m_issueType->currentText().trimmed();
    const bool typeUnknown = jira && !m.issueTypes.isEmpty() && !type.isEmpty() && !m.issueTypes.contains(type, Qt::CaseInsensitive);
    m_issueTypeNote->setText(typeUnknown ? tr("«%1» no es un tipo de %2").arg(type, project) : QString());
    m_issueTypeNote->setVisible(typeUnknown);
}

void BugDialog::loadDraft(int stepIndex) {
    const BugReport d = m_bugs.draftFromCurrentContext(stepIndex);
    m_touched = false;
    m_titleEdited = false;
    m_issueTypeEdited = false;
    ui::setFlag(m_title, "invalid", false);
    ui::setFlag(m_actual, "invalid", false);
    // El título se propone al final, con lo que se haya escrito: no se arrastra el del paso.
    m_title->clear();
    m_linkedCaseId = d.linkedCaseId;
    m_linkedPlanRunId = d.linkedPlanRunId;
    m_severity->setCurrentIndex(std::max(0, m_severity->findData(d.severity)));
    m_classification->setCurrentIndex(std::max(0, m_classification->findData(d.classification)));
    refreshEnvironment(d.environment);
    refreshStepOptions(d.linkedStep);
    m_priority->setCurrentText(m_settings.tracker().kind == TrackerKind::Jira ? d.priority : QString());
    m_assignee->setCurrentText(QString());
    m_components->setText(d.components.join(QStringLiteral(", ")));
    m_versions->clear();
    m_labels->clear();
    m_expected->setTextSilently(d.expected);
    m_actual->setTextSilently(d.actual);
    // Los pasos para reproducir empiezan en blanco: el camino que dio con el bug no tiene por qué ser
    // el del caso. Se pueden importar de él.
    clearStepRows();
    for (const auto& s : d.stepsToReproduce) addStepRow(s);
    if (d.stepsToReproduce.isEmpty()) addStepRow(QString());
    clearUserRows();
    for (const auto& u : d.testUsers) addUserRow(u);
    refreshRecentUsers();
    // El parte arranca con una copia de la última captura de la ejecución; el resto se añade a mano.
    clearShots(true);
    if (const TestCase* c = m_cases.selected()) {
        const QList<Screenshot> run = c->latestEvidence();
        const auto last = std::max_element(run.cbegin(), run.cend(), [](const Screenshot& a, const Screenshot& b) { return a.id < b.id; });
        if (last != run.cend()) addShots(m_evidence.copyForBug({last->path}));
    }
    refreshShots();
    refreshTrackerFields();
    refreshContext();
    showPage(WhatHappened);
    m_actual->setFocus();
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

// ---- Pasos y usuarios ----------------------------------------------------------------------

void BugDialog::addStepRow(const QString& text, int at, bool focus) {
    auto* row = new QWidget;
    auto* h = ui::hbox(row, 0, 8);
    auto* number = ui::label(QString(), "mono-muted");
    number->setFixedWidth(22);
    number->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    h->addWidget(number);
    auto* edit = new QLineEdit(text);
    edit->setObjectName(QStringLiteral("bugReproStep"));
    edit->setPlaceholderText(tr("Acción que hiciste"));
    h->addWidget(edit, 1);
    auto* remove = removeButton(tr("Quitar paso"));
    h->addWidget(remove);
    // Enter abre el paso siguiente justo debajo: la lista se escribe de corrido.
    connect(edit, &QLineEdit::returnPressed, this, [this, row]() { addStepRow(QString(), m_stepRows->indexOf(row) + 1, true); });
    connect(remove, &QPushButton::clicked, this, [this, row]() {
        // La fila se borra después: es su botón el que está avisando.
        m_stepRows->removeWidget(row);
        row->hide();
        row->deleteLater();
        if (m_stepRows->count() == 0) addStepRow(QString());   // siempre queda una fila donde escribir
        renumberSteps();
    });
    m_stepRows->insertWidget(at < 0 ? m_stepRows->count() : at, row);
    renumberSteps();
    if (focus) edit->setFocus();
}

void BugDialog::clearStepRows() {
    // Nunca desde una de sus filas (lo piden «Importar» y el borrador): se pueden borrar ya.
    while (QLayoutItem* item = m_stepRows->takeAt(0)) {
        delete item->widget();
        delete item;
    }
}

void BugDialog::renumberSteps() {
    for (int i = 0; i < m_stepRows->count(); ++i) {
        QWidget* row = m_stepRows->itemAt(i)->widget();
        if (auto* number = row ? row->findChild<QLabel*>() : nullptr) number->setText(QStringLiteral("%1.").arg(i + 1));
        if (auto* edit = row ? row->findChild<QLineEdit*>() : nullptr) edit->setAccessibleName(tr("Paso %1").arg(i + 1));
    }
}

QStringList BugDialog::reproSteps() const {
    QStringList out;
    for (int i = 0; i < m_stepRows->count(); ++i) {
        const QWidget* row = m_stepRows->itemAt(i)->widget();
        if (const auto* edit = row ? row->findChild<QLineEdit*>() : nullptr; edit && !edit->text().trimmed().isEmpty()) out << edit->text().trimmed();
    }
    return out;
}

void BugDialog::importCaseSteps() {
    const QStringList steps = m_bugs.caseSteps();
    if (steps.isEmpty()) { emit toast(tr("El caso no tiene pasos que importar"), theme::Amber); return; }
    clearStepRows();
    for (const auto& s : steps) addStepRow(s);
}

void BugDialog::addUserRow(const TestUser& user, bool focus) {
    // Si hay una fila vacía se usa esa antes que abrir otra.
    for (int i = 0; i < m_userRows->count() && !user.isEmpty(); ++i) {
        QWidget* row = m_userRows->itemAt(i)->widget();
        const auto edits = row ? row->findChildren<QLineEdit*>() : QList<QLineEdit*>{};
        if (edits.size() == 2 && edits[0]->text().trimmed().isEmpty() && edits[1]->text().trimmed().isEmpty()) {
            edits[0]->setText(user.user);
            edits[1]->setText(user.role);
            refreshRecentUsers();
            return;
        }
    }
    auto* row = new QWidget;
    auto* h = ui::hbox(row, 0, 8);
    auto* name = new QLineEdit(user.user);
    name->setObjectName(QStringLiteral("bugUserName"));
    name->setPlaceholderText(tr("Usuario"));
    name->setAccessibleName(tr("Usuario"));
    auto* role = new QLineEdit(user.role);
    role->setObjectName(QStringLiteral("bugUserRole"));
    role->setPlaceholderText(tr("Rol / perfil"));
    role->setAccessibleName(tr("Rol o perfil"));
    h->addWidget(name, 1);
    h->addWidget(role, 1);
    auto* remove = removeButton(tr("Quitar usuario"));
    h->addWidget(remove);
    connect(name, &QLineEdit::editingFinished, this, &BugDialog::refreshRecentUsers);
    connect(remove, &QPushButton::clicked, this, [this, row]() {
        m_userRows->removeWidget(row);
        row->hide();
        row->deleteLater();
        QTimer::singleShot(0, this, &BugDialog::refreshRecentUsers);   // cuando la fila ya no está
    });
    m_userRows->addWidget(row);
    refreshRecentUsers();
    if (focus) name->setFocus();
}

void BugDialog::clearUserRows() {
    while (QLayoutItem* item = m_userRows->takeAt(0)) {
        delete item->widget();
        delete item;
    }
}

QList<TestUser> BugDialog::testUsers() const {
    QList<TestUser> out;
    for (int i = 0; i < m_userRows->count(); ++i) {
        const QWidget* row = m_userRows->itemAt(i)->widget();
        const auto edits = row ? row->findChildren<QLineEdit*>() : QList<QLineEdit*>{};
        if (edits.size() != 2) continue;
        const TestUser u{edits[0]->text().trimmed(), edits[1]->text().trimmed()};
        if (!u.isEmpty()) out << u;
    }
    return out;
}

void BugDialog::refreshRecentUsers() {
    // El primero es «+ Añadir usuario»: se queda; los atajos de los recientes van detrás.
    while (m_recentUsers->count() > 1) {
        QLayoutItem* item = m_recentUsers->takeAt(1);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    const QList<TestUser> current = testUsers();
    for (const auto& u : m_bugs.recentUsers()) {
        const bool present = std::any_of(current.cbegin(), current.cend(), [&u](const TestUser& c) { return c.user.compare(u.user, Qt::CaseInsensitive) == 0; });
        if (present) continue;
        auto* chip = ui::button(u.role.isEmpty() ? u.user : QStringLiteral("%1 · %2").arg(u.user, u.role), "chip");
        chip->setObjectName(QStringLiteral("bugRecentUser"));
        chip->setToolTip(tr("Añadir al parte"));
        connect(chip, &QPushButton::clicked, this, [this, u]() { addUserRow(u); });
        m_recentUsers->addWidget(chip);
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
    // Lo escrito a mano manda sobre la opción elegida (el combo es editable).
    const int env = m_env->findText(m_env->currentText(), Qt::MatchFixedString);
    b.environment = env >= 0 ? m_env->itemData(env).toString() : m_env->currentText().trimmed();
    b.linkedCaseId = m_linkedCaseId;
    b.linkedPlanRunId = m_linkedPlanRunId;
    b.linkedStep = m_linkedStep->currentData().toInt();
    if (const TestCase* c = m_cases.selected(); c && c->id == b.linkedCaseId) b.linkedStoryKey = c->jiraKey;
    b.stepsToReproduce = reproSteps();
    b.testUsers = testUsers();
    b.expected = m_expected->toPlainText();
    b.actual = m_actual->toPlainText();
    b.issueType = m_issueType->currentText().trimmed();
    b.priority = m_priority->currentText().trimmed();
    b.assigneeName = m_assignee->currentText().trimmed();
    // El id (usuario en Jira Server, accountId en Cloud) sale de la persona elegida; si el texto no
    // corresponde a ninguna, se envía tal cual y que lo valide el gestor.
    const int ai = m_assignee->findText(b.assigneeName, Qt::MatchFixedString);
    b.assigneeId = ai >= 0 ? m_assignee->itemData(ai).toString() : b.assigneeName;
    // A Jira sólo van los componentes y versiones que tiene el proyecto: uno que no exista hace que
    // rechace el issue entero.
    const bool jira = m_settings.tracker().kind == TrackerKind::Jira;
    b.components = jira ? knownValues(m_components->text(), m_bugs.metadata().components) : parseTags(m_components->text());
    b.affectsVersions = jira ? knownValues(m_versions->text(), m_bugs.metadata().versions) : parseTags(m_versions->text());
    b.labels = parseTags(m_labels->text());
    for (const auto& s : m_shots) b.attachmentPaths << s.path;
    return b;
}

void BugDialog::submit() {
    if (m_sending) return;
    // Sin título escrito se usa el propuesto: el parte no se queda sin enviar por eso.
    if (m_title->text().trimmed().isEmpty()) refreshTitle();
    const BugReport b = collect();
    if (!b.isValid()) {
        m_touched = true;
        ui::setFlag(m_title, "invalid", b.title.trimmed().isEmpty());
        ui::setFlag(m_actual, "invalid", b.actual.trimmed().isEmpty());
        // Se lleva al paso donde falta: el resultado actual está en el primero.
        if (b.actual.trimmed().isEmpty()) { showPage(WhatHappened); m_actual->setFocus(); }
        else { showPage(Review); m_title->setFocus(); }
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
            if (!r.warning.isEmpty()) emit toast(r.warning, theme::Amber);
            else emit toast(tr("%1 creado en %2 con %3 adjuntos").arg(r.key, tracker).arg(r.attachmentsUploaded), theme::Blue);
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
