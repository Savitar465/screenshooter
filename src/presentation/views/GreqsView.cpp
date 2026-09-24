#include "GreqsView.h"

#include "application/AppContext.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/FlowLayout.h"
#include "presentation/widgets/Ui.h"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>

namespace qaflow {

namespace {
/// Qué filas enseña el filtro de issue.
enum class IssueScope { All = 0, Without = 1, With = 2 };

QPushButton* smallButton(const QString& text, const char* role, const QString& tip = QString()) {
    auto* b = ui::button(text, role);
    b->setStyleSheet(QStringLiteral("padding:5px 10px;font-size:12px;border-radius:8px;"));
    if (!tip.isEmpty()) b->setToolTip(tip);
    return b;
}

QString day(const QDate& d) { return d.isValid() ? d.toString(QStringLiteral("dd/MM/yyyy")) : QString(); }

/// Texto en el que busca el filtro de la lista: número, sistema, descripción, estados y solicitante.
QString searchText(const ExternalRequirement& r) {
    return QStringList{r.id, r.system, r.systemCode, r.summary, r.states.join(QLatin1Char(' ')), r.requester, r.requestingUnit, r.priority}
        .join(QLatin1Char(' '));
}

bool matches(const ExternalRequirement& r, const QString& filter) {
    const QString text = searchText(r);
    for (const auto& word : filter.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts))
        if (!text.contains(word, Qt::CaseInsensitive)) return false;
    return true;
}
} // namespace

GreqsView::GreqsView(const AppContext& ctx, QWidget* tabs, QWidget* parent)
    : QWidget(parent), m_issues(*ctx.issues), m_directory(ctx.issueDirectory), m_projects(ctx.projects),
      m_requirements(ctx.requirements), m_projectId(ctx.projectId) {
    auto* root = ui::vbox(this, 0, 0);

    auto* head = new QWidget;
    auto* hh = ui::hbox(head, 0, 10);
    hh->setContentsMargins(22, 16, 22, 14);
    hh->addWidget(tabs);
    m_count = ui::label(QString(), "muted-sm");
    m_count->setObjectName(QStringLiteral("greqsCount"));
    hh->addWidget(m_count);
    hh->addStretch(1);

    m_filterText = new QLineEdit;
    m_filterText->setObjectName(QStringLiteral("greqsFilter"));
    m_filterText->setPlaceholderText(tr("Filtrar por número, sistema, descripción, solicitante…"));
    m_filterText->setClearButtonEnabled(true);
    m_filterText->setMinimumWidth(220);
    connect(m_filterText, &QLineEdit::textChanged, this, &GreqsView::rebuild);
    hh->addWidget(m_filterText);

    m_issueFilter = new QComboBox;
    m_issueFilter->setObjectName(QStringLiteral("greqsIssueFilter"));
    m_issueFilter->setStyleSheet(QStringLiteral("font-size:11.5px;padding:3px 6px;"));
    m_issueFilter->addItem(tr("Todos"), static_cast<int>(IssueScope::All));
    m_issueFilter->addItem(tr("Sin issue"), static_cast<int>(IssueScope::Without));
    m_issueFilter->addItem(tr("Con issue"), static_cast<int>(IssueScope::With));
    connect(m_issueFilter, &QComboBox::currentIndexChanged, this, &GreqsView::rebuild);
    hh->addWidget(m_issueFilter);

    m_reload = smallButton(tr("Actualizar"), "outline", tr("Volver a leer tu bandeja de control de calidad en GESREQ"));
    m_reload->setObjectName(QStringLiteral("greqsReload"));
    connect(m_reload, &QPushButton::clicked, this, &GreqsView::reload);
    hh->addWidget(m_reload);
    root->addWidget(head);

    // Buscar un GREQ por su número, esté o no asignado al usuario.
    auto* searchBar = new QWidget;
    auto* sh = ui::hbox(searchBar, 0, 8);
    sh->setContentsMargins(22, 0, 22, 12);
    sh->addWidget(ui::label(tr("¿Uno que no es tuyo?"), "muted-sm"));
    m_number = new QLineEdit;
    m_number->setObjectName(QStringLiteral("greqsNumber"));
    m_number->setPlaceholderText(tr("Nº de GREQ"));
    m_number->setClearButtonEnabled(true);
    m_number->setMaximumWidth(160);
    connect(m_number, &QLineEdit::returnPressed, this, [this]() { findRequirement(m_number->text()); });
    sh->addWidget(m_number);
    m_find = smallButton(tr("Buscar GREQ"), "outline", tr("Leer la ficha de ese requerimiento en GESREQ, aunque no esté asignado a ti"));
    m_find->setObjectName(QStringLiteral("greqsFind"));
    connect(m_find, &QPushButton::clicked, this, [this]() { findRequirement(m_number->text()); });
    sh->addWidget(m_find);
    sh->addStretch(1);
    root->addWidget(searchBar);

    auto* divider = new QFrame;
    divider->setFixedHeight(1);
    divider->setStyleSheet(QStringLiteral("background:%1;").arg(theme::Border));
    root->addWidget(divider);

    QWidget* content;
    auto* scroll = ui::scrollArea(&content, &m_list);
    scroll->setObjectName(QStringLiteral("greqsList"));
    m_list->setContentsMargins(22, 14, 22, 18);
    m_list->setSpacing(8);
    root->addWidget(scroll, 1);

    // Lo que dice cada fila (si tiene issue y dónde) cambia con los issues de cualquier proyecto.
    auto rebuildIfVisible = [this]() { if (isVisible()) rebuild(); };
    if (m_directory) connect(m_directory, &IssueDirectory::changed, this, rebuildIfVisible);
    connect(&m_issues, &IssueStore::issuesChanged, this, rebuildIfVisible);
    if (m_projects) connect(m_projects, &ProjectStore::projectsChanged, this, rebuildIfVisible);
    rebuild();
}

void GreqsView::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    rebuild();
}

std::optional<IssueDirectory::Entry> GreqsView::issueFor(const QString& requirementId) const {
    const QString connection = m_requirements ? m_requirements->connection() : QString();
    if (m_directory) return m_directory->findByRequirement(connection, requirementId);
    if (const Issue* issue = m_issues.findByRequirement(connection, requirementId)) return IssueDirectory::Entry{m_projectId, *issue};
    return std::nullopt;
}

QString GreqsView::projectName(const QString& projectId) const {
    const Project* project = m_projects ? m_projects->find(projectId) : nullptr;
    return project ? project->name : projectId;
}

void GreqsView::setLoading(bool on) {
    m_loading = on;
    m_reload->setEnabled(!on);
    m_reload->setText(on ? tr("Leyendo…") : tr("Actualizar"));
}

void GreqsView::ensureLoaded() {
    if (!m_loaded && !m_loading) reload();
}

void GreqsView::reload() {
    if (!m_requirements || m_loading) return;
    if (m_requirements->connection().isEmpty()) {
        m_loaded = true;
        m_error.clear();
        rebuild();
        return;
    }
    setLoading(true);
    rebuild();
    QPointer<GreqsView> self(this);
    m_requirements->fetchInbox([self](const RequirementInboxResult& r) {
        if (!self) return;
        self->setLoading(false);
        self->m_loaded = true;
        if (!r.ok) {
            self->m_error = r.error;
            self->rebuild();
            return;
        }
        self->m_error.clear();
        self->m_inbox = r.requirements;
        self->m_fetchedAt = r.fetchedAt.isValid() ? r.fetchedAt : QDateTime::currentDateTime();
        // Leer la bandeja pone al día lo importado: qué salió de ella y qué cambió en lo que sigue.
        const QString connection = self->m_requirements->connection();
        if (self->m_directory) self->m_directory->applyInbox(self->m_inbox, connection, self->m_fetchedAt);
        else self->m_issues.applyInbox(self->m_inbox, connection, self->m_fetchedAt);
        self->rebuild();
    });
}

void GreqsView::findRequirement(const QString& number) {
    const QString id = number.trimmed();
    if (!m_requirements || m_searching) return;
    if (id.isEmpty()) {
        m_found.reset();
        m_searchError.clear();
        rebuild();
        return;
    }
    if (!QRegularExpression(QStringLiteral("^\\d+$")).match(id).hasMatch()) {
        m_found.reset();
        m_searchError = tr("«%1» no es un número de GREQ").arg(id);
        rebuild();
        return;
    }
    // Si está en la bandeja ya se sabe todo de él: se enseña tal cual, sin leer la ficha.
    for (const auto& r : m_inbox) {
        if (r.id != id) continue;
        m_found = r;
        m_foundAssigned = true;
        m_searchError.clear();
        rebuild();
        return;
    }
    m_searching = true;
    m_find->setEnabled(false);
    m_find->setText(tr("Buscando…"));
    QPointer<GreqsView> self(this);
    m_requirements->fetchDetail(id, [self, id](const RequirementDetailResult& r) {
        if (!self) return;
        self->m_searching = false;
        self->m_find->setEnabled(true);
        self->m_find->setText(tr("Buscar GREQ"));
        if (r.ok) {
            self->m_found = requirementFromDetail(r.detail);
            if (self->m_found->id.isEmpty()) self->m_found->id = id;
            self->m_foundAssigned = false;
            self->m_searchError.clear();
        } else {
            self->m_found.reset();
            self->m_searchError = r.failure == RequirementSourceFailure::NotFound
                                      ? tr("GESREQ no tiene el requerimiento %1 (o no puedes verlo)").arg(id)
                                      : tr("No se pudo leer el requerimiento %1 · %2").arg(id, r.error);
        }
        self->rebuild();
    });
}

void GreqsView::rebuild() {
    ui::clearLayout(m_list);
    const bool configured = m_requirements && !m_requirements->connection().isEmpty();
    m_find->setEnabled(configured && !m_searching);
    m_reload->setEnabled(configured && !m_loading);

    auto notice = [this](const QString& text, const QString& color) {
        auto* l = ui::label(text, "muted");
        l->setObjectName(QStringLiteral("greqsNotice"));
        l->setWordWrap(true);
        if (!color.isEmpty()) l->setStyleSheet(QStringLiteral("color:%1;").arg(color));
        m_list->addWidget(l);
        return l;
    };
    auto section = [this](const QString& text) {
        auto* l = ui::label(text, "eyebrow");
        l->setContentsMargins(0, 6, 0, 0);
        m_list->addWidget(l);
    };

    if (!configured) {
        m_count->clear();
        notice(tr("Configura la conexión con GESREQ (dirección, usuario y contraseña) para ver los requerimientos que tienes asignados."), QString());
        auto* open = smallButton(tr("Configurar GESREQ…"), "primary");
        open->setObjectName(QStringLiteral("greqsSettings"));
        connect(open, &QPushButton::clicked, this, &GreqsView::settingsRequested);
        m_list->addWidget(open, 0, Qt::AlignLeft);
        m_list->addStretch(1);
        return;
    }

    // Lo buscado por número va primero: es lo que se acaba de pedir.
    if (m_found || !m_searchError.isEmpty()) {
        section(m_found ? tr("BÚSQUEDA · GREQ %1").arg(m_found->id) : tr("BÚSQUEDA"));
        if (m_found) m_list->addWidget(row(*m_found, m_foundAssigned));
        else notice(m_searchError, theme::Amber);
    }

    const auto scope = static_cast<IssueScope>(m_issueFilter->currentData().toInt());
    int shown = 0, withIssue = 0;
    QList<QWidget*> rows;
    for (const auto& r : m_inbox) {
        const bool has = issueFor(r.id).has_value();
        if (has) ++withIssue;
        if (!matches(r, m_filterText->text())) continue;
        if ((scope == IssueScope::With && !has) || (scope == IssueScope::Without && has)) continue;
        ++shown;
        rows << row(r, true);
    }
    m_count->setText(m_inbox.isEmpty() ? QString()
                                       : tr("%1 asignados · %2 con issue · %3 sin issue")
                                             .arg(m_inbox.size())
                                             .arg(withIssue)
                                             .arg(m_inbox.size() - withIssue));

    section(tr("ASIGNADOS A TI"));
    if (m_loading && m_inbox.isEmpty()) notice(tr("Leyendo tu bandeja de control de calidad…"), QString());
    else if (!m_error.isEmpty()) notice(tr("No se pudo consultar GESREQ · %1").arg(m_error), theme::Red);
    else if (!m_loaded) notice(tr("Todavía no se leyó la bandeja."), QString());
    else if (m_inbox.isEmpty()) notice(tr("Tu bandeja de control de calidad no tiene requerimientos."), QString());
    else if (shown == 0) notice(tr("Ningún requerimiento coincide con los filtros."), QString());
    for (auto* r : rows) m_list->addWidget(r);
    m_list->addStretch(1);
}

QWidget* GreqsView::row(const ExternalRequirement& r, bool assigned) {
    const std::optional<IssueDirectory::Entry> issue = issueFor(r.id);
    const bool here = issue && issue->projectId == m_projectId;

    auto* card = ui::card("card-flat");
    card->setObjectName(QStringLiteral("greqRow-%1").arg(r.id));
    auto* h = ui::hbox(card, 0, 14);
    h->setContentsMargins(14, 11, 12, 11);
    // Con issue en este proyecto, la fila resalta como su tarjeta del tablero.
    if (here) card->setStyleSheet(QStringLiteral("QFrame#%1{border-left:3px solid %2;}").arg(card->objectName(), theme::Blue));

    auto* info = new QWidget;
    auto* v = ui::vbox(info, 0, 5);
    auto* top = new QWidget;
    auto* th = new FlowLayout(top, 0, 6, 4);
    auto* number = ui::label(QStringLiteral("GREQ %1").arg(r.id), "mono-muted");
    number->setTextInteractionFlags(Qt::TextSelectableByMouse);
    th->addWidget(number);
    if (!r.systemCode.isEmpty()) th->addWidget(ui::pill(r.systemCode, theme::tint(theme::Muted, 26), theme::Muted));
    if (!r.priority.isEmpty()) th->addWidget(ui::pill(r.priority.toUpper(), theme::tint(theme::Amber, 30), theme::AmberSoft));
    for (const auto& state : r.states) th->addWidget(ui::pill(state, theme::tint(theme::Cyan, 26), theme::Cyan));
    if (!assigned) th->addWidget(ui::pill(tr("NO ASIGNADO A TI"), theme::tint(theme::Violet, 30), theme::Violet));
    v->addWidget(top);

    auto* summary = ui::label(r.summary.isEmpty() ? tr("Requerimiento %1").arg(r.id) : r.summary);
    summary->setWordWrap(true);
    summary->setStyleSheet(QStringLiteral("font-size:13px;font-weight:600;color:%1;").arg(theme::Text));
    v->addWidget(summary);

    QStringList meta;
    if (!r.requester.isEmpty())
        meta << (r.requestingUnit.isEmpty() ? tr("Solicita %1").arg(r.requester) : tr("Solicita %1 (%2)").arg(r.requester, r.requestingUnit));
    if (r.assignedFrom.isValid() || r.assignedUntil.isValid())
        meta << tr("Asignado %1 – %2").arg(day(r.assignedFrom), day(r.assignedUntil));
    const QString target = m_projects ? m_projects->projectForRequirementSystem(r.systemCode) : QString();
    meta << (target.isEmpty() ? tr("Ningún proyecto trabaja %1").arg(r.systemCode)
                              : (target == m_projectId ? tr("Se prueba en este proyecto") : tr("Se prueba en «%1»").arg(projectName(target))));
    auto* metaLabel = ui::label(meta.join(QStringLiteral(" · ")), "muted-sm");
    metaLabel->setWordWrap(true);
    v->addWidget(metaLabel);
    h->addWidget(info, 1);

    // A la derecha, dónde está su issue y lo que toca: seguir con él o empezar las pruebas.
    auto* side = new QWidget;
    auto* sv = ui::vbox(side, 0, 6);
    QLabel* status;
    if (issue) {
        const QString text = here ? issue->issue.id : QStringLiteral("%1 · %2").arg(issue->issue.id, projectName(issue->projectId));
        status = here ? ui::pill(text, theme::tint(theme::Green, 38), theme::Green) : ui::pill(text, theme::tint(theme::Blue, 30), theme::Blue);
        status->setToolTip(here ? tr("Tiene issue en este proyecto: %1").arg(issue->issue.title)
                                : tr("Tiene issue en el proyecto «%1»: %2").arg(projectName(issue->projectId), issue->issue.title));
    } else {
        status = ui::pill(tr("SIN ISSUE"), theme::tint(theme::Amber, 38), theme::AmberSoft);
    }
    status->setObjectName(QStringLiteral("greqStatus-%1").arg(r.id));
    sv->addWidget(status, 0, Qt::AlignRight);

    QPushButton* action;
    if (issue) {
        action = smallButton(here ? tr("Abrir issue") : tr("Abrir en «%1»…").arg(projectName(issue->projectId)), here ? "primary" : "outline");
        connect(action, &QPushButton::clicked, this, [this, entry = *issue]() { emit openIssueRequested(entry.projectId, entry.issue.id); });
    } else {
        const QString text = target.isEmpty() ? tr("Iniciar pruebas · Elegir proyecto…")
                                              : (target == m_projectId ? tr("Iniciar pruebas") : tr("Iniciar pruebas en «%1»").arg(projectName(target)));
        action = smallButton(text, "primary",
                             tr("Crea su issue (con su plan y su issue en el gestor) en el proyecto que trabaja %1").arg(r.systemCode));
        connect(action, &QPushButton::clicked, this, [this, r]() {
            emit startTestingRequested(r, m_requirements->connection(), m_fetchedAt.isValid() ? m_fetchedAt : QDateTime::currentDateTime());
        });
    }
    action->setObjectName(QStringLiteral("greqAction-%1").arg(r.id));
    sv->addWidget(action, 0, Qt::AlignRight);
    sv->addStretch(1);
    h->addWidget(side, 0, Qt::AlignTop);
    return card;
}

} // namespace qaflow
