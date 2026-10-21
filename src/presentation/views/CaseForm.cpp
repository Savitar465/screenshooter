#include "CaseForm.h"

#include "application/TestCaseStore.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/FlowLayout.h"
#include "presentation/widgets/TextArea.h"
#include "presentation/widgets/Ui.h"

#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QDrag>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>

#include <algorithm>

namespace qaflow {

namespace {
// Hasta dónde crece cada campo antes de desplazarse por dentro.
constexpr int kTitleRows = 4;
constexpr int kPreRows = 10;
constexpr int kStepRows = 12;
// Lo que lleva un paso arrastrado: su posición (0..n-1) en el caso.
constexpr auto kStepMime = "application/x-qaflow-step";

/// Un campo de una línea (componente, etiquetas) tan ancho como su texto, entre `min` y `max`: lo corto
/// no deja un hueco y lo largo se ve hasta donde cabe (entero, en su tooltip).
void fitToText(QLineEdit* e, int min, int max) {
    e->ensurePolished();
    const QString shown = e->text().isEmpty() ? e->placeholderText() : e->text();
    e->setFixedWidth(std::clamp(e->fontMetrics().horizontalAdvance(shown) + 30, min, max));
    e->setToolTip(e->text().isEmpty() ? e->property("hint").toString() : e->text());
}

/// Un par «nombre campo» de la fila de propiedades.
QWidget* prop(const QString& name, QWidget* field, QWidget* extra = nullptr) {
    auto* w = new QWidget;
    auto* h = ui::hbox(w, 0, 6);
    h->addWidget(ui::label(name, "field-label"));
    h->addWidget(field);
    if (extra) h->addWidget(extra);
    return w;
}
} // namespace

CaseForm::CaseForm(TestCaseStore& store, Options options, QWidget* parent)
    : QWidget(parent), m_store(&store), m_options(options), m_suites(store.suites()) {
    build();
    load();
}

CaseForm::CaseForm(TestCase initial, const QStringList& suites, Options options, QWidget* parent)
    : QWidget(parent), m_local(std::move(initial)), m_options(options), m_suites(suites) {
    if (m_local.steps.isEmpty()) m_local.steps << TestStep{};   // siempre hay uno en el que escribir
    build();
    load();
}

const TestCase* CaseForm::current() const { return m_store ? m_store->selected() : &m_local; }

TestCase CaseForm::testCase() const {
    TestCase c = m_store && m_store->selected() ? *m_store->selected() : m_local;
    c.title = c.title.trimmed();
    c.suite = c.suite.trimmed();
    c.preconditions = c.preconditions.trimmed();
    QList<TestStep> steps;
    for (TestStep s : c.steps) {
        s = {s.action.trimmed(), s.data.trimmed(), s.expected.trimmed()};
        if (!s.action.isEmpty() || !s.data.isEmpty() || !s.expected.isEmpty()) steps << s;
    }
    c.steps = steps;
    return c;
}

TextArea* CaseForm::field(const QString& placeholder, int maxRows, const char* quietRole, const char* boxedRole) {
    auto* t = new TextArea(1);
    ui::setRole(t, m_options.quiet ? quietRole : boxedRole);
    t->setPlaceholderText(placeholder);
    t->setAutoHeight(1, maxRows);
    return t;
}

void CaseForm::build() {
    auto* v = ui::vbox(this, 0, 18);

    m_title = field(tr("Título del caso: qué se prueba"), kTitleRows, "title", "title-boxed");
    m_title->setObjectName(QStringLiteral("caseTitle"));
    m_title->setAccessibleName(tr("Título del caso"));
    m_title->setSingleParagraph(true);
    m_title->installEventFilter(this);
    connect(m_title, &TextArea::edited, this, [this](const QString& t) { edit([&](TestCase& c) { c.title = t; }); });
    v->addWidget(m_title);

    // Propiedades: una fila de pares «nombre campo» que baja de línea si no cabe.
    auto* props = new QWidget;
    auto* flow = new FlowLayout(props, 0, 20, 8);
    m_suite = new QComboBox;
    m_suite->setObjectName(QStringLiteral("caseSuite"));
    ui::setRole(m_suite, "prop");
    m_suite->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    m_suite->setMaximumWidth(260);   // una suite de nombre largo se corta; entera, en su tooltip
    connect(m_suite, &QComboBox::currentTextChanged, this, [this](const QString& t) {
        m_suite->setToolTip(t);
        edit([&](TestCase& c) { c.suite = t; });
    });
    auto* addSuite = ui::button(QStringLiteral("+"), "icon-quiet");
    addSuite->setObjectName(QStringLiteral("caseNewSuite"));
    addSuite->setToolTip(tr("Nueva suite"));
    addSuite->setFixedSize(24, 24);
    connect(addSuite, &QPushButton::clicked, this, &CaseForm::newSuite);
    flow->addWidget(prop(tr("Suite"), m_suite, addSuite));

    m_priority = new QComboBox;
    m_priority->setObjectName(QStringLiteral("casePriority"));
    ui::setRole(m_priority, "prop");
    for (const auto p : {Priority::Alta, Priority::Media, Priority::Baja}) m_priority->addItem(label(p), static_cast<int>(p));
    connect(m_priority, &QComboBox::currentIndexChanged, this, [this](int) {
        const auto p = static_cast<Priority>(m_priority->currentData().toInt());
        edit([&](TestCase& c) { c.priority = p; });
    });
    flow->addWidget(prop(tr("Prioridad"), m_priority));

    if (m_options.status) {
        m_status = new QComboBox;
        m_status->setObjectName(QStringLiteral("caseStatus"));
        ui::setRole(m_status, "prop");
        for (const auto st : {CaseStatus::Listo, CaseStatus::Obsoleto}) m_status->addItem(label(st), static_cast<int>(st));
        connect(m_status, &QComboBox::currentIndexChanged, this, [this](int) {
            const auto st = static_cast<CaseStatus>(m_status->currentData().toInt());
            edit([&](TestCase& c) { c.status = st; });
        });
        flow->addWidget(prop(tr("Estado"), m_status));
    }

    m_component = new QLineEdit;
    m_component->setObjectName(QStringLiteral("caseComponent"));
    ui::setRole(m_component, "prop");
    m_component->setPlaceholderText(tr("p. ej. Carrito"));
    m_component->setProperty("hint", tr("Módulo o componente del producto"));
    connect(m_component, &QLineEdit::textChanged, this, [this]() { fitToText(m_component, 110, 300); });
    connect(m_component, &QLineEdit::textEdited, this, [this](const QString& t) { edit([&](TestCase& c) { c.component = t.trimmed(); }); });
    flow->addWidget(prop(tr("Componente"), m_component));

    m_tags = new QLineEdit;
    m_tags->setObjectName(QStringLiteral("caseTags"));
    ui::setRole(m_tags, "prop");
    m_tags->setPlaceholderText(tr("regresión, smoke…"));
    m_tags->setProperty("hint", tr("Etiquetas separadas por comas"));
    connect(m_tags, &QLineEdit::textChanged, this, [this]() { fitToText(m_tags, 130, 320); });
    connect(m_tags, &QLineEdit::textEdited, this, [this](const QString& t) { edit([&](TestCase& c) { c.tags = parseTags(t); }); });
    flow->addWidget(prop(tr("Etiquetas"), m_tags));
    v->addWidget(props);

    // Precondiciones
    auto* preBlock = new QWidget;
    auto* pv = ui::vbox(preBlock, 0, 6);
    pv->addWidget(ui::label(tr("Precondiciones"), "field-label"));
    m_pre = field(tr("Estado inicial del sistema, datos de prueba, cuenta…"), kPreRows, "panel", "boxed");
    m_pre->setObjectName(QStringLiteral("casePreconditions"));
    m_pre->setAccessibleName(tr("Precondiciones"));
    m_pre->enableMarkupEditor(tr("Precondiciones"));
    m_pre->setMarkupButtonOnFocus(true);
    m_pre->installEventFilter(this);
    connect(m_pre, &TextArea::edited, this, [this](const QString& t) { edit([&](TestCase& c) { c.preconditions = t; }); });
    pv->addWidget(m_pre);
    v->addWidget(preBlock);

    // Pasos
    auto* stepsBlock = new QWidget;
    auto* sv = ui::vbox(stepsBlock, 0, 0);
    auto* head = new QWidget;
    auto* hh = ui::hbox(head, 0, 8);
    hh->setContentsMargins(0, 0, 0, 8);
    hh->addWidget(ui::label(tr("Pasos"), "h3"));
    m_stepsCount = ui::label(QString(), "muted");
    hh->addWidget(m_stepsCount);
    hh->addStretch(1);
    sv->addWidget(head);
    auto* list = m_stepsList = new QWidget;
    list->setObjectName(QStringLiteral("caseSteps"));
    m_stepsLayout = ui::vbox(list, 0, 0);
    // Los pasos se reordenan arrastrándolos por su asa: la línea marca el hueco en el que caerá.
    list->setAcceptDrops(true);
    list->installEventFilter(this);
    m_dropLine = new QFrame(list);
    m_dropLine->setFixedHeight(2);
    m_dropLine->setStyleSheet(QStringLiteral("background:%1;border:none;").arg(theme::Blue));
    m_dropLine->hide();
    m_autoScroll = new QTimer(this);
    m_autoScroll->setInterval(16);
    connect(m_autoScroll, &QTimer::timeout, this, &CaseForm::autoScrollTick);
    sv->addWidget(list);

    auto* addRow = ui::card("step-row");
    auto* ah = ui::hbox(addRow, 0, 10);
    ah->setContentsMargins(0, 8, 0, 0);
    auto* add = ui::button(tr("+ Añadir paso"), "text-link");
    add->setObjectName(QStringLiteral("caseAddStep"));
    connect(add, &QPushButton::clicked, this, [this]() { insertStep(current() ? int(current()->steps.size()) : 0); });
    ah->addWidget(add);
    auto* hint = ui::label(tr("o Ctrl+Intro desde un paso"), "muted-sm");
    hint->setWordWrap(true);
    ah->addWidget(hint, 1);
    sv->addWidget(addRow);
    v->addWidget(stepsBlock);
}

void CaseForm::load() {
    const TestCase* c = current();
    if (!c) return;
    m_selfEdit = true;
    m_title->setTextSilently(c->title);
    setSuites(m_suites);
    m_priority->setCurrentIndex(std::max(0, m_priority->findData(static_cast<int>(c->priority))));
    if (m_status) m_status->setCurrentIndex(std::max(0, m_status->findData(static_cast<int>(c->status))));
    if (m_component->text() != c->component) m_component->setText(c->component);
    if (parseTags(m_tags->text()) != c->tags) m_tags->setText(c->tags.join(QStringLiteral(", ")));
    fitToText(m_component, 110, 300);
    fitToText(m_tags, 130, 320);
    m_pre->setTextSilently(c->preconditions);
    m_selfEdit = false;
    refreshSteps();
}

void CaseForm::setSuites(const QStringList& suites) {
    m_suites = suites;
    const TestCase* c = current();
    const QString cur = c ? c->suite : m_suite->currentText();
    const bool wasEditing = m_selfEdit;
    m_selfEdit = true;   // rehacer la lista no es elegir otra suite
    m_suite->clear();
    m_suite->addItems(suites);
    if (!cur.isEmpty() && !suites.contains(cur)) m_suite->addItem(cur);
    m_suite->setCurrentText(cur);
    m_suite->setToolTip(cur);
    m_selfEdit = wasEditing;
}

void CaseForm::setNarrow(bool narrow) {
    if (narrow == m_narrow) return;
    m_narrow = narrow;
    // Llega en mitad de un cambio de tamaño: rehacer los pasos ahí deja a los de arriba con la altura
    // de antes, así que se rehacen al terminar.
    QMetaObject::invokeMethod(this, [this]() { refreshSteps(); }, Qt::QueuedConnection);
}

void CaseForm::focusTitle() {
    m_title->setFocus(Qt::OtherFocusReason);
    m_title->moveCursor(QTextCursor::End);
}

void CaseForm::edit(const std::function<void(TestCase&)>& mutate) {
    if (m_selfEdit) return;   // es el formulario cargándose, no alguien escribiendo
    m_selfEdit = true;
    if (!m_store) mutate(m_local);
    else if (!m_store->selectedId().isEmpty()) m_store->updateCase(m_store->selectedId(), mutate);
    m_selfEdit = false;
    emit edited();
}

void CaseForm::editStep(int index, const std::function<void(TestStep&)>& mutate) {
    if (m_selfEdit) return;
    m_selfEdit = true;
    if (!m_store) {
        if (index >= 0 && index < m_local.steps.size()) mutate(m_local.steps[index]);
    } else if (!m_store->selectedId().isEmpty()) {
        m_store->updateStep(m_store->selectedId(), index, mutate);
    }
    m_selfEdit = false;
    emit edited();
}

void CaseForm::changeSteps(int focus, const std::function<void(const QString&)>& inStore,
                           const std::function<void(QList<TestStep>&)>& local) {
    m_focusStep = focus;
    if (m_store) {
        // El catálogo avisa del cambio y quien tiene el formulario lo recarga.
        if (!m_store->selectedId().isEmpty()) inStore(m_store->selectedId());
    } else {
        local(m_local.steps);
        if (m_local.steps.isEmpty()) m_local.steps << TestStep{};
        refreshSteps();
    }
    emit edited();
}

void CaseForm::insertStep(int index) {
    changeSteps(index, [this, index](const QString& id) { m_store->insertStep(id, index); },
                [index](QList<TestStep>& s) { s.insert(std::clamp(index, 0, int(s.size())), TestStep{}); });
}

void CaseForm::duplicateStep(int index) {
    changeSteps(index + 1, [this, index](const QString& id) { m_store->duplicateStep(id, index); },
                [index](QList<TestStep>& s) { if (index >= 0 && index < s.size()) s.insert(index + 1, s[index]); });
}

void CaseForm::removeStep(int index) {
    const int n = current() ? int(current()->steps.size()) : 0;
    changeSteps(std::min(index, n - 2), [this, index](const QString& id) { m_store->removeStep(id, index); },
                [index](QList<TestStep>& s) { if (index >= 0 && index < s.size()) s.removeAt(index); });
}

void CaseForm::moveStep(int index, int delta) {
    changeSteps(index + delta, [this, index, delta](const QString& id) { m_store->moveStep(id, index, delta); },
                [index, delta](QList<TestStep>& s) {
                    const int j = index + delta;
                    if (index >= 0 && index < s.size() && j >= 0 && j < s.size()) s.move(index, j);
                });
}

void CaseForm::newSuite() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Nueva suite"), tr("Nombre de la suite:"), QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty()) return;
    if (m_suite->findText(name) < 0) m_suite->addItem(name);
    m_suite->setCurrentText(name);   // y el desplegable lo pone en el caso
}

void CaseForm::refreshSteps() {
    ui::clearLayout(m_stepsLayout);
    const TestCase* c = current();
    if (!c) return;
    const int n = c->steps.size();
    m_stepsCount->setText(QString::number(n));
    TextArea* focus = nullptr;
    const char* fieldRole = m_options.quiet ? "quiet" : "boxed";
    const char* actionRole = m_options.quiet ? "quiet-strong" : "boxed-strong";
    for (int i = 0; i < n; ++i) {
        const TestStep& step = c->steps[i];
        auto* row = ui::card("step-row");
        row->setObjectName(QStringLiteral("caseStep-%1").arg(i + 1));
        row->setProperty("stepRow", i);
        auto* h = ui::hbox(row, 0, 10);
        h->setContentsMargins(4, 10, 4, 10);

        auto* num = ui::label(QString::number(i + 1), "step-num");
        num->setAlignment(Qt::AlignCenter);
        num->setFixedSize(24, 24);
        auto* grip = ui::label(QStringLiteral("⠿"), "muted");
        grip->setObjectName(QStringLiteral("caseStepGrip-%1").arg(i + 1));
        grip->setAlignment(Qt::AlignCenter);
        grip->setFixedSize(24, 22);
        grip->setCursor(Qt::OpenHandCursor);
        grip->setToolTip(tr("Arrastra para mover el paso"));
        grip->setStyleSheet(QStringLiteral("font-size:15px;"));
        grip->setProperty("dragStep", i);
        grip->installEventFilter(this);
        auto* lead = new QWidget;
        auto* lv = ui::vbox(lead, 0, 4);
        lv->addWidget(num);
        lv->addWidget(grip);
        lv->addStretch(1);
        h->addWidget(lead, 0, Qt::AlignTop);

        auto make = [&](const char* name, const char* role, const QString& placeholder, const QString& title, const QString& text) {
            auto* t = new TextArea(1);
            t->setObjectName(QStringLiteral("case%1-%2").arg(QLatin1String(name)).arg(i + 1));
            ui::setRole(t, role);
            t->setPlaceholderText(placeholder);
            t->setAccessibleName(title);
            t->setTextSilently(text);
            t->setAutoHeight(1, kStepRows);
            t->enableMarkupEditor(title);
            t->setMarkupButtonOnFocus(true);
            t->setProperty("stepIndex", i);
            t->installEventFilter(this);
            return t;
        };
        auto* action = make("Action", actionRole, tr("Qué hace el tester…"), tr("Paso %1 · Acción").arg(i + 1), step.action);
        auto* data = make("Data", fieldRole, tr("Con qué datos (opcional)…"), tr("Paso %1 · Datos de la prueba").arg(i + 1), step.data);
        data->setToolTip(tr("Datos de la prueba: usuario, importe, archivo… Es el campo «data» del paso de Zephyr."));
        auto* expected = make("Expected", fieldRole, tr("Qué debe ocurrir…"), tr("Paso %1 · Resultado esperado").arg(i + 1), step.expected);
        connect(action, &TextArea::edited, this, [this, i](const QString& t) { editStep(i, [&](TestStep& s) { s.action = t; }); });
        connect(data, &TextArea::edited, this, [this, i](const QString& t) { editStep(i, [&](TestStep& s) { s.data = t; }); });
        connect(expected, &TextArea::edited, this, [this, i](const QString& t) { editStep(i, [&](TestStep& s) { s.expected = t; }); });

        // Debajo de la acción, los datos y el resultado esperado: lado a lado, o uno bajo otro si es estrecho.
        auto* body = new QWidget;
        auto* bv = ui::vbox(body, 0, 4);
        bv->addWidget(action);
        auto* details = new QWidget;
        auto* dl = m_narrow ? static_cast<QBoxLayout*>(ui::vbox(details, 0, 6)) : static_cast<QBoxLayout*>(ui::hbox(details, 0, 12));
        const std::pair<QString, TextArea*> parts[] = {{tr("Datos"), data}, {tr("Resultado esperado"), expected}};
        for (const auto& [caption, area] : parts) {
            auto* part = new QWidget;
            auto* pv = ui::vbox(part, 0, 2);
            pv->addWidget(ui::label(caption, "caption"));
            pv->addWidget(area);
            pv->addStretch(1);   // el más corto de los dos no se estira para igualar al otro
            dl->addWidget(part, 1);
        }
        bv->addWidget(details);
        h->addWidget(body, 1);

        auto* more = ui::button(QStringLiteral("⋯"), "icon-quiet");
        more->setObjectName(QStringLiteral("caseStepMenu-%1").arg(i + 1));
        more->setToolTip(tr("Mover, insertar, clonar o eliminar el paso"));
        more->setFixedSize(28, 28);
        auto* menu = new QMenu(more);
        menu->addAction(tr("Subir"), this, [this, i]() { moveStep(i, -1); })->setEnabled(i > 0);
        menu->addAction(tr("Bajar"), this, [this, i]() { moveStep(i, +1); })->setEnabled(i < n - 1);
        menu->addSeparator();
        menu->addAction(tr("Insertar paso debajo"), this, [this, i]() { insertStep(i + 1); });
        menu->addAction(tr("Clonar paso"), this, [this, i]() { duplicateStep(i); });
        menu->addSeparator();
        menu->addAction(m_store ? tr("Eliminar paso (se puede deshacer)") : tr("Eliminar paso"), this, [this, i]() { removeStep(i); });
        more->setMenu(menu);
        h->addWidget(more, 0, Qt::AlignTop);

        m_stepsLayout->addWidget(row);
        if (i == m_focusStep) focus = action;
    }
    m_focusStep = -1;
    // Que los de arriba vuelvan a medir la lista: con otros pasos pide otro alto.
    m_stepsLayout->parentWidget()->updateGeometry();
    if (focus) focus->setFocus(Qt::OtherFocusReason);
}

int CaseForm::dropGap(int y) const {
    // El hueco (0..n) más cercano: delante del primer paso cuya mitad queda por debajo del puntero.
    int gap = 0;
    for (auto* row : m_stepsList->findChildren<QFrame*>(Qt::FindDirectChildrenOnly)) {
        const QVariant index = row->property("stepRow");
        if (!index.isValid() || !row->isVisible()) continue;
        if (y < row->geometry().center().y()) return index.toInt();
        gap = index.toInt() + 1;
    }
    return gap;
}

void CaseForm::showDropLine(int gap) {
    int y = 0;
    for (auto* row : m_stepsList->findChildren<QFrame*>(Qt::FindDirectChildrenOnly)) {
        const QVariant index = row->property("stepRow");
        if (!index.isValid() || !row->isVisible()) continue;
        if (index.toInt() == gap) { y = row->y(); break; }
        y = row->geometry().bottom() + 1;
    }
    m_dropLine->setGeometry(0, std::clamp(y - 1, 0, std::max(0, m_stepsList->height() - 2)), m_stepsList->width(), 2);
    m_dropLine->show();
    m_dropLine->raise();
}

void CaseForm::dropStep(int from, int gap) {
    const int n = current() ? int(current()->steps.size()) : 0;
    if (from < 0 || from >= n) return;
    // Soltarlo en el hueco de debajo de sí mismo es dejarlo donde estaba.
    const int to = gap > from ? gap - 1 : gap;
    if (to == from || to < 0 || to >= n) return;
    moveStep(from, to - from);
}

void CaseForm::startDrag(QWidget* grip, int index) {
    QWidget* row = grip;
    while (row && !row->property("stepRow").isValid()) row = row->parentWidget();
    auto* mime = new QMimeData;
    mime->setData(QString::fromLatin1(kStepMime), QByteArray::number(index));
    auto* drag = new QDrag(grip);
    drag->setMimeData(mime);
    if (row) {
        // Lo que se arrastra es el paso, medio transparente.
        QPixmap shot = row->grab();
        QPixmap faded(shot.size());
        faded.setDevicePixelRatio(shot.devicePixelRatio());
        faded.fill(Qt::transparent);
        QPainter p(&faded);
        p.setOpacity(0.75);
        p.drawPixmap(0, 0, shot);
        p.end();
        drag->setPixmap(faded);
        drag->setHotSpot(grip->mapTo(row, grip->rect().center()));
    }
    drag->exec(Qt::MoveAction);
    stopDragFeedback();
}

void CaseForm::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (m_scroll) return;
    // Lo que desplaza al formulario (la pantalla Casos, el cuerpo del diálogo): mientras se arrastra un
    // paso, ahí también se puede soltar, y cerca de su borde se desplaza solo.
    for (QWidget* p = parentWidget(); p; p = p->parentWidget()) {
        if (auto* area = qobject_cast<QScrollArea*>(p)) {
            m_scroll = area;
            area->viewport()->setAcceptDrops(true);   // sólo acepta pasos: lo demás lo ignora el filtro
            area->viewport()->installEventFilter(this);
            break;
        }
    }
}

void CaseForm::autoScroll(int y) {
    // A menos de una franja del borde se desplaza, y más deprisa cuanto más cerca.
    constexpr int kZone = 56;
    const int height = m_scroll->viewport()->height();
    m_scrollStep = y < kZone ? -(1 + (kZone - y) / 3) : y > height - kZone ? 1 + (y - (height - kZone)) / 3 : 0;
    if (m_scrollStep == 0) m_autoScroll->stop();
    else if (!m_autoScroll->isActive()) m_autoScroll->start();
}

void CaseForm::autoScrollTick() {
    if (!m_scroll || m_scrollStep == 0) return m_autoScroll->stop();
    QScrollBar* bar = m_scroll->verticalScrollBar();
    const int before = bar->value();
    bar->setValue(before + m_scrollStep);
    if (bar->value() == before) return;
    // Con el puntero quieto no llegan más DragMove: la línea se recoloca con lo que ha pasado por debajo.
    showDropLine(dropGap(m_stepsList->mapFromGlobal(QCursor::pos()).y()));
}

void CaseForm::stopDragFeedback() {
    m_scrollStep = 0;
    m_autoScroll->stop();
    m_dropLine->hide();
}

bool CaseForm::eventFilter(QObject* watched, QEvent* event) {
    // Arrastrar un paso por su asa.
    if (watched->property("dragStep").isValid()) {
        auto* grip = static_cast<QWidget*>(watched);
        if (event->type() == QEvent::MouseButtonPress && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
            m_dragStart = static_cast<QMouseEvent*>(event)->position().toPoint();
            grip->setCursor(Qt::ClosedHandCursor);
            return true;
        }
        if (event->type() == QEvent::MouseMove && (static_cast<QMouseEvent*>(event)->buttons() & Qt::LeftButton)) {
            if ((static_cast<QMouseEvent*>(event)->position().toPoint() - m_dragStart).manhattanLength() >= QApplication::startDragDistance()) {
                grip->setCursor(Qt::OpenHandCursor);
                startDrag(grip, grip->property("dragStep").toInt());   // el asa puede no existir ya al volver
            }
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            grip->setCursor(Qt::OpenHandCursor);
            return true;
        }
    }
    // Y soltarlo: sobre la lista o en cualquier parte de lo que se desplaza (por encima de los pasos va
    // al principio, por debajo al final), que es también donde, cerca del borde, se desplaza solo.
    QWidget* viewport = m_scroll ? m_scroll->viewport() : nullptr;
    if (watched == m_stepsList || (viewport && watched == viewport)) {
        auto* target = static_cast<QWidget*>(watched);
        switch (event->type()) {
            case QEvent::DragEnter:
            case QEvent::DragMove: {
                auto* drag = static_cast<QDragMoveEvent*>(event);
                if (!drag->mimeData()->hasFormat(QString::fromLatin1(kStepMime))) return false;
                // Por la pantalla y no con mapFrom: éste no admite que el destino sea el propio widget.
                const QPoint at = target->mapToGlobal(drag->position().toPoint());
                showDropLine(dropGap(m_stepsList->mapFromGlobal(at).y()));
                if (viewport) autoScroll(viewport->mapFromGlobal(at).y());
                drag->acceptProposedAction();
                return true;
            }
            case QEvent::DragLeave:
                // Al pasar de la lista al resto de lo que se desplaza sigue el arrastre; al salir de ahí, no.
                if (watched != m_stepsList || !viewport) stopDragFeedback();
                return true;
            case QEvent::Drop: {
                auto* drop = static_cast<QDropEvent*>(event);
                stopDragFeedback();
                if (!drop->mimeData()->hasFormat(QString::fromLatin1(kStepMime))) return false;
                const int from = drop->mimeData()->data(QString::fromLatin1(kStepMime)).toInt();
                const int gap = dropGap(m_stepsList->mapFromGlobal(target->mapToGlobal(drop->position().toPoint())).y());
                drop->acceptProposedAction();
                // Después de que termine el arrastre: mover rehace los pasos, el asa que lo empezó incluida.
                QMetaObject::invokeMethod(this, [this, from, gap]() { dropStep(from, gap); }, Qt::QueuedConnection);
                return true;
            }
            default: break;
        }
    }
    // Ctrl+Intro: un paso nuevo debajo del que se escribe (desde el título o las precondiciones, al final).
    if (event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) && key->modifiers() == Qt::ControlModifier) {
            const QVariant index = watched->property("stepIndex");
            insertStep(index.isValid() ? index.toInt() + 1 : (current() ? int(current()->steps.size()) : 0));
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace qaflow
