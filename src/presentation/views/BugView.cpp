#include "BugView.h"

#include "application/BugReportService.h"
#include "application/BugStore.h"
#include "application/SettingsStore.h"
#include "application/TestCaseStore.h"
#include "presentation/theme/Theme.h"
#include "presentation/views/BugDetailWindow.h"
#include "presentation/widgets/Ui.h"

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>

#include <algorithm>

namespace qaflow {

namespace {
QString when(const QDateTime& dt) { return dt.isValid() ? dt.toString(QStringLiteral("dd/MM/yyyy HH:mm")) : QStringLiteral("—"); }
QPushButton* smallButton(const QString& text, const char* role) {
    auto* b = ui::button(text, role);
    b->setStyleSheet(QStringLiteral("padding:5px 10px;font-size:12px;border-radius:8px;"));
    return b;
}
} // namespace

BugView::BugView(TestCaseStore& cases, SettingsStore& settings, BugReportService& bugs, BugStore& ledger, QWidget* parent)
    : QWidget(parent), m_cases(cases), m_settings(settings), m_bugs(bugs), m_ledger(ledger) {
    auto* root = ui::hbox(this, 0, 0);
    QWidget* content;
    QVBoxLayout* outer;
    auto* sa = ui::scrollArea(&content, &outer);
    m_scroll = sa;
    outer->setContentsMargins(32, 28, 32, 28);
    auto* page = new QWidget;
    page->setMaximumWidth(1000);
    auto* v = ui::vbox(page, 0, 18);
    outer->addWidget(page, 0, Qt::AlignTop);
    root->addWidget(sa, 1);

    buildHeader(v);
    buildPending(v);
    buildList(v);

    // Deslizar hasta abajo alarga la lista; con el libro agotado, le pide otra página al gestor.
    connect(sa->verticalScrollBar(), &QScrollBar::valueChanged, this, [this, sa](int value) {
        if (value >= sa->verticalScrollBar()->maximum() - 8) loadMore();
    });
    connect(&m_settings, &SettingsStore::trackerChanged, this, [this]() { refreshHeader(); refreshIssues(); });
    connect(&m_ledger, &BugStore::bugsChanged, this, [this]() { refreshHeader(); refreshIssues(); refreshPending(); });
    refreshHeader();
    refreshIssues();
    refreshPending();
}

// ---- Construcción --------------------------------------------------------------------------

void BugView::buildHeader(QVBoxLayout* v) {
    auto* head = new QWidget;
    auto* hh = ui::hbox(head, 0, 10);
    auto* titles = new QWidget;
    auto* tv = ui::vbox(titles, 0, 0);
    m_eyebrow = ui::label(QString(), "eyebrow");
    m_eyebrow->setTextFormat(Qt::RichText);
    tv->addWidget(m_eyebrow);
    tv->addWidget(ui::label(tr("Bugs"), "h1"));
    hh->addWidget(titles, 1);

    // Traer del gestor es lo que hace que la lista sea la del proyecto y no la de este equipo.
    m_import = smallButton(tr("Traer de %1").arg(toString(m_settings.tracker().kind)), "outline");
    m_import->setObjectName(QStringLiteral("bugsImport"));
    connect(m_import, &QPushButton::clicked, this, [this]() { importFromTracker(0); });
    hh->addWidget(m_import, 0, Qt::AlignTop);
    m_refreshStatuses = smallButton(tr("Actualizar estados"), "outline");
    m_refreshStatuses->setObjectName(QStringLiteral("bugsRefreshStatuses"));
    connect(m_refreshStatuses, &QPushButton::clicked, this, &BugView::refreshStatuses);
    hh->addWidget(m_refreshStatuses, 0, Qt::AlignTop);
    auto* create = ui::button(tr("+ Reportar bug"), "primary");
    create->setObjectName(QStringLiteral("bugsCreate"));
    create->setToolTip(tr("Abre el parte en su ventana, con el caso y el paso de la ejecución en curso"));
    connect(create, &QPushButton::clicked, this, &BugView::createRequested);
    hh->addWidget(create, 0, Qt::AlignTop);
    v->addWidget(head);
}

void BugView::buildPending(QVBoxLayout* v) {
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
}

void BugView::buildList(QVBoxLayout* v) {
    auto* filters = new QWidget;
    auto* fh = ui::hbox(filters, 0, 8);
    const struct { Filter filter; QString text; const char* name; } chips[] = {
        {Filter::Todos, tr("Todos"), "bugsFilterAll"},
        {Filter::Abiertos, tr("Abiertos"), "bugsFilterOpen"},
        {Filter::Resueltos, tr("Resueltos"), "bugsFilterClosed"},
    };
    for (const auto& c : chips) {
        auto* b = ui::button(c.text, "chip");
        b->setObjectName(QString::fromLatin1(c.name));
        b->setCheckable(true);
        b->setStyleSheet(QStringLiteral("padding:5px 12px;font-size:12px;border-radius:8px;"));
        connect(b, &QPushButton::clicked, this, [this, f = c.filter]() { setFilter(f); });
        m_chips.append(b);
        fh->addWidget(b);
    }
    fh->addSpacing(8);
    m_searchBox = new QLineEdit;
    m_searchBox->setObjectName(QStringLiteral("bugsSearch"));
    m_searchBox->setPlaceholderText(tr("Buscar por clave, título o caso…"));
    m_searchBox->setClearButtonEnabled(true);
    connect(m_searchBox, &QLineEdit::textChanged, this, [this](const QString& t) { m_search = t.trimmed(); m_shown = kPage; refreshIssues(); });
    fh->addWidget(m_searchBox, 1);
    v->addWidget(filters);

    m_issuesHeader = ui::label(QString(), "eyebrow");
    v->addWidget(m_issuesHeader);
    auto* ilist = new QWidget;
    m_issuesList = ui::vbox(ilist, 0, 6);
    v->addWidget(ilist);
    m_more = ui::label(QString(), "muted-sm");
    m_more->setAlignment(Qt::AlignCenter);
    m_more->setStyleSheet(QStringLiteral("padding:10px 0;"));
    v->addWidget(m_more);
    setFilter(m_filter);   // marca el chip activo; ya con la lista construida, que es lo que refresca
}

// ---- Refrescos -----------------------------------------------------------------------------

void BugView::refreshHeader() {
    const TrackerSettings& t = m_settings.tracker();
    const int total = m_ledger.issues().size();
    const int open = m_ledger.openIssueCount();
    m_eyebrow->setText(tr("%1 BUGS · %2 ABIERTOS · %3 <span style=\"color:%4;font-family:monospace\">%5</span>")
                           .arg(total).arg(open).arg(toString(t.kind).toUpper(), theme::Blue,
                                                     t.project.isEmpty() ? tr("(sin proyecto)") : t.project));
    const bool canImport = m_bugs.canImportFromTracker();
    m_import->setVisible(canImport);
    m_import->setText(tr("Traer de %1").arg(toString(t.kind)));
    m_import->setToolTip(tr("Trae del gestor los errores y mejoras que creó QAflow en este proyecto, aunque se reportaran desde otro equipo; el resto llegan al deslizar"));
    m_refreshStatuses->setVisible(total > 0);
}

QList<IssueLink> BugView::visibleIssues() const {
    QList<IssueLink> out;
    const QString needle = m_search.toLower();
    for (int i = m_ledger.issues().size() - 1; i >= 0; --i) {   // el más reciente primero
        const IssueLink& l = m_ledger.issues()[i];
        if (m_filter == Filter::Abiertos && l.resolved) continue;
        if (m_filter == Filter::Resueltos && !l.resolved) continue;
        if (!needle.isEmpty()
            && !l.key.toLower().contains(needle) && !l.title.toLower().contains(needle) && !l.caseId.toLower().contains(needle))
            continue;
        out << l;
    }
    return out;
}

void BugView::setFilter(Filter f) {
    m_filter = f;
    m_shown = kPage;   // otro filtro, otra lista: se empieza por arriba
    for (int i = 0; i < m_chips.size(); ++i) {
        const bool active = i == static_cast<int>(f);
        m_chips[i]->setChecked(active);
        // El chip activo se ve porque la hoja de estilos pinta `active`; `checked` a secas no cambia nada.
        ui::setFlag(m_chips[i], "active", active);
    }
    refreshIssues();
}

void BugView::refreshIssues() {
    ui::clearLayout(m_issuesList);
    const auto issues = visibleIssues();
    const int total = m_ledger.issues().size();
    m_issuesHeader->setText(issues.size() == total ? tr("BUGS REPORTADOS · %1").arg(total)
                                                   : tr("BUGS REPORTADOS · %1 DE %2").arg(issues.size()).arg(total));
    m_more->clear();
    if (total == 0) {
        m_issuesList->addWidget(ui::label(tr("Todavía no se ha reportado ningún bug en este proyecto. Los que se "
                                             "reporten durante una ejecución aparecen aquí con su estado."), "muted-sm"));
        return;
    }
    if (issues.isEmpty()) {
        m_issuesList->addWidget(ui::label(tr("Ningún bug encaja con el filtro."), "muted-sm"));
        return;
    }
    // Sólo las filas que se han pedido deslizando: una lista larga no se pinta entera de golpe.
    const int shown = std::min<int>(m_shown, issues.size());
    for (int i = 0; i < shown; ++i) {
        const IssueLink& l = issues[i];
        // La fila entera abre la ficha del bug en su ventana; la clave lleva al gestor.
        auto* row = ui::button(QString(), "row");
        row->setObjectName(QStringLiteral("bugRow-%1").arg(l.key));
        row->setToolTip(tr("Ver la ficha de este bug"));
        connect(row, &QPushButton::clicked, this, [this, key = l.key]() { openDetail(key); });
        auto* h = ui::hbox(row, 0, 10);
        h->setContentsMargins(12, 8, 12, 8);
        auto* key = ui::label(l.key, "mono-muted");
        key->setStyleSheet(QStringLiteral("font-size:12px;font-weight:700;color:%1;font-family:'Consolas','DejaVu Sans Mono',monospace;").arg(theme::Blue));
        h->addWidget(key);
        auto* title = new QLabel(l.title.isEmpty() ? tr("(sin título)") : l.title);
        title->setWordWrap(true);
        h->addWidget(title, 1);
        // Lo que se trae del gestor son errores y mejoras: cuál es cada uno se ve de un vistazo.
        if (!l.issueType.trimmed().isEmpty())
            h->addWidget(ui::pill(l.issueType.trimmed().toUpper(), theme::tint(theme::Muted, 30), theme::Muted));
        if (!l.classification.trimmed().isEmpty())
            h->addWidget(ui::label(l.classification.trimmed().toUpper(), "mono-muted"));
        if (!l.caseId.isEmpty())
            h->addWidget(ui::label(l.step > 0 ? tr("%1 · paso %2").arg(l.caseId).arg(l.step) : l.caseId, "mono-muted"));
        h->addWidget(ui::label(when(l.createdAt), "muted-sm"));
        const QString statusText = l.status.isEmpty() ? tr("SIN CONSULTAR") : l.status.toUpper();
        h->addWidget(ui::pill(statusText, l.status.isEmpty() ? theme::tint(theme::Muted, 38) : l.resolved ? theme::Green : theme::tint(theme::Blue, 38),
                              l.status.isEmpty() ? theme::Muted : l.resolved ? theme::Bg : theme::Blue));
        // Los hijos no se comen el clic de la fila; el enlace al gestor está en la ficha.
        for (auto* child : row->findChildren<QWidget*>()) child->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_issuesList->addWidget(row);
    }
    // El pie dice por qué la lista se corta aquí: o quedan filas por pintar, o quedan bugs en el gestor.
    if (shown < issues.size()) m_more->setText(tr("Desliza para ver los %1 restantes").arg(issues.size() - shown));
    else if (m_busy) m_more->setText(tr("Trayendo más bugs de %1…").arg(toString(m_settings.tracker().kind)));
    else if (m_trackerNext >= 0) m_more->setText(tr("Desliza para traer más de %1 · %2 de %3")
                                                     .arg(toString(m_settings.tracker().kind)).arg(m_trackerNext).arg(m_trackerTotal));
    fillViewport();
}

void BugView::loadMore(bool mayAskTracker) {
    if (m_shown < visibleIssues().size()) {
        m_shown += kPage;
        refreshIssues();
        return;
    }
    if (!mayAskTracker) return;
    // El libro se ha acabado: lo que quede está en el gestor, y sólo si ya se trajo una vez (traer
    // por primera vez es una decisión del usuario, no algo que pase por deslizar).
    if (m_trackerNext >= 0 && !m_busy) importFromTracker(m_trackerNext);
}

void BugView::fillViewport() {
    // Con menos filas que hueco no hay barra que deslizar y la lista se quedaría corta para siempre:
    // se completa sola en cuanto el layout se asienta. Sin barra que mover no hay bucle: cada vuelta
    // pinta una página más y se para cuando ya no quedan.
    if (!m_scroll || m_busy) return;
    if (m_shown >= visibleIssues().size()) return;
    QTimer::singleShot(0, this, [this]() {
        if (!m_scroll->verticalScrollBar()->isVisible() || m_scroll->verticalScrollBar()->maximum() == 0) loadMore(false);
    });
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

void BugView::openDetail(const QString& key) {
    if (auto* open = m_detailWindows.value(key).data()) {
        if (const IssueLink* bug = m_ledger.findIssue(key)) open->setBug(*bug);
        open->show();
        open->raise();
        open->activateWindow();
        return;
    }
    const IssueLink* bug = m_ledger.findIssue(key);
    if (!bug) return;
    const TestCase* c = m_cases.find(bug->caseId);
    const QString stepAction = c && bug->step > 0 && bug->step <= c->steps.size() ? c->steps[bug->step - 1].action : QString();
    auto* window = new BugDetailWindow(*bug, c ? c->title : QString(), stepAction, this);
    window->setAttribute(Qt::WA_DeleteOnClose);
    connect(window, &BugDetailWindow::openUrlRequested, this, &BugView::openIssueRequested);
    m_detailWindows.insert(key, window);
    window->show();
}

void BugView::importFromTracker(int startAt) {
    if (m_busy) return;
    m_busy = true;
    m_import->setEnabled(false);
    m_import->setText(tr("Trayendo…"));
    // Traer de nuevo desde el principio vuelve a recorrer el gestor; una página más sigue donde iba.
    const bool first = startAt <= 0;
    if (first) { m_trackerNext = -1; m_trackerTotal = 0; }
    refreshIssues();
    m_bugs.importFromTracker(startAt, [this, first](const BugReportService::ImportResult& r) {
        m_busy = false;
        m_import->setEnabled(true);
        refreshHeader();   // vuelve a poner el texto del botón
        m_trackerNext = r.nextStart;
        m_trackerTotal = r.total;
        refreshIssues();   // lo que entró se va viendo según se desliza, como el resto de la lista
        if (!r.ok) { emit toast(tr("No se pudieron traer los bugs · %1").arg(r.error), theme::Red); return; }
        if (!first) return;   // las páginas siguientes llegan solas: no hace falta avisar de cada una
        if (r.imported == 0 && r.updated == 0) { emit toast(tr("El gestor no tiene bugs de QAflow en este proyecto"), theme::Amber); return; }
        emit toast(tr("%1 bugs nuevos · %2 actualizados").arg(r.imported).arg(r.updated), theme::Green);
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
