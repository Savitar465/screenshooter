#include "RequirementWindow.h"

#include "presentation/theme/Theme.h"
#include "presentation/widgets/ElidedLabel.h"
#include "presentation/widgets/FlowLayout.h"
#include "presentation/widgets/Icons.h"
#include "presentation/widgets/ProgressCells.h"
#include "presentation/widgets/Ui.h"

#include <QApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

namespace qaflow {

namespace {
/// Etiqueta o título para compararlo: minúsculas, sin tildes ni los dos puntos del final.
QString plain(const QString& text) {
    QString out;
    for (const QChar c : text.normalized(QString::NormalizationForm_D))
        if (c.category() != QChar::Mark_NonSpacing) out += c;
    out = out.trimmed().toLower();
    if (out.endsWith(QLatin1Char(':'))) out.chop(1);
    return out.trimmed();
}
} // namespace

namespace requirement {

int workingDaysTo(const QDate& from, const QDate& to) {
    if (!from.isValid() || !to.isValid()) return 0;
    if (to < from) return -workingDaysTo(to, from);
    // Semanas enteras de cinco días y, del resto, los que no caen en fin de semana.
    const int days = static_cast<int>(from.daysTo(to));
    int count = days / 7 * 5;
    for (int i = 1, day = from.dayOfWeek(); i <= days % 7; ++i)
        if ((day + i - 1) % 7 + 1 < Qt::Saturday) ++count;
    return count;
}

QaPeriod qaPeriod(const ExternalRequirement& r, const QDate& today) {
    QaPeriod p;
    if (!r.assignedFrom.isValid() || !r.assignedUntil.isValid() || r.assignedUntil < r.assignedFrom) return p;
    p.valid = true;
    p.started = today >= r.assignedFrom;
    const QDate dayBefore = r.assignedFrom.addDays(-1);
    p.total = std::max(workingDaysTo(dayBefore, r.assignedUntil), 1);
    p.elapsed = std::clamp(workingDaysTo(dayBefore, today), 0, p.total);
    p.left = workingDaysTo(today, r.assignedUntil);
    // Vencido el viernes, el sábado ya está vencido aunque no haya pasado ningún día hábil.
    if (today > r.assignedUntil) p.left = std::min(p.left, -1);
    if (!p.started) p.untilStart = std::max(workingDaysTo(today, r.assignedFrom), 1);
    return p;
}

QString stateColor(const QString& state) {
    const QString s = state.toUpper();
    if (s.contains(QStringLiteral("OBSERVADO"))) return theme::Amber;
    if (s.contains(QStringLiteral("CONFORME")) || s.contains(QStringLiteral("APROBADO")) || s.contains(QStringLiteral("ACEPTADO"))
        || s.contains(QStringLiteral("FINALIZADO")))
        return theme::Green;
    return theme::Blue;
}

QString priorityColor(const QString& priority) {
    const QString p = priority.trimmed().toUpper();
    if (p == QStringLiteral("ALTA") || p == QStringLiteral("URGENTE") || p == QStringLiteral("CRÍTICA")) return theme::RedSoft;
    if (p == QStringLiteral("MEDIA")) return theme::AmberSoft;
    return theme::Muted;
}

QString sentenceCase(const QString& text) {
    QString s = text.trimmed().toLower();
    if (!s.isEmpty()) s[0] = s[0].toUpper();
    return s;
}

const RequirementSection* block(const RequirementDetail& d, Block b) {
    for (const auto& section : d.sections) {
        const QString t = plain(section.title);
        const bool quality = t.contains(QStringLiteral("calidad"));
        switch (b) {
            case Block::Development:
                if (t.contains(QStringLiteral("asignado")) && !quality) return &section;
                break;
            case Block::QaRequest:
                if (quality && t.contains(QStringLiteral("solicitado"))) return &section;
                break;
            case Block::QaAssignment:
                if (quality && t.contains(QStringLiteral("asignado"))) return &section;
                break;
            case Block::Functional:
                if (t.contains(QStringLiteral("funcional"))) return &section;
                break;
        }
    }
    return nullptr;
}

QString blockField(const RequirementDetail& d, Block b, const QString& labelPrefix) {
    const RequirementSection* section = block(d, b);
    if (!section) return {};
    for (const auto& f : section->fields)
        if (!f.value.trimmed().isEmpty() && plain(f.label).startsWith(labelPrefix)) return f.value.trimmed();
    return {};
}

QString qaResource(const RequirementDetail& d) { return blockField(d, Block::QaAssignment, QStringLiteral("recurso")); }

} // namespace requirement

namespace {
using requirement::Block;

QString when(const QDateTime& dt) { return dt.isValid() ? dt.toString(QStringLiteral("dd/MM/yyyy HH:mm")) : QStringLiteral("—"); }
QString day(const QDate& d) { return d.isValid() ? d.toString(QStringLiteral("dd/MM/yyyy")) : QStringLiteral("—"); }
const QString kMono = QStringLiteral("font-family:\"Consolas\",\"DejaVu Sans Mono\",monospace;font-size:12px;");

/// "MOLLO VICTOR MIGUEL" → "Mollo Victor Miguel".
QString nameCase(const QString& name) {
    QStringList words = name.simplified().toLower().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (QString& w : words) w[0] = w[0].toUpper();
    return words.join(QLatin1Char(' '));
}

/// "MOLLO VICTOR MIGUEL" → "Mollo V.": el apellido y la inicial, para el recorrido.
QString shortName(const QString& name) {
    const QStringList words = nameCase(name).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.size() < 2) return words.value(0);
    return QStringLiteral("%1 %2.").arg(words[0], words[1].left(1));
}

QList<QDate> datesIn(const QString& text) {
    static const QRegularExpression date(QStringLiteral("(\\d{2}/\\d{2}/\\d{4})"));
    QList<QDate> dates;
    for (auto it = date.globalMatch(text); it.hasNext();)
        if (const QDate d = QDate::fromString(it.next().captured(1), QStringLiteral("dd/MM/yyyy")); d.isValid()) dates << d;
    return dates;
}

/// Un periodo escrito por GESREQ ("18/09/2026 - 23/09/2026", con o sin horas) en corto: "18/09 – 23/09/2026".
QString span(const QDate& from, const QDate& until) {
    if (!from.isValid()) return {};
    if (!until.isValid() || until == from) return day(from);
    return QStringLiteral("%1 – %2").arg(from.toString(from.year() == until.year() ? QStringLiteral("dd/MM") : QStringLiteral("dd/MM/yyyy")), day(until));
}
QString span(const QString& text) {
    const QList<QDate> dates = datesIn(text);
    return dates.isEmpty() ? QString() : span(dates.first(), dates.size() > 1 ? dates.last() : QDate());
}

/// Las fechas del campo "Fecha solicitado(S), observado(O), aceptado(A)": "S: 21/08/2026, A: 15/09/2026".
QDate milestone(const RequirementDetail& d, QChar letter, bool last) {
    static const QRegularExpression mark(QStringLiteral("([SOA])\\s*:\\s*(\\d{2}/\\d{2}/\\d{4})"));
    QString text;
    for (const auto& f : d.fields)
        if (plain(f.label).startsWith(QStringLiteral("fecha solicitado"))) text = f.value;
    QDate found;
    for (auto it = mark.globalMatch(text); it.hasNext();) {
        const auto m = it.next();
        if (m.captured(1) != letter) continue;
        found = QDate::fromString(m.captured(2), QStringLiteral("dd/MM/yyyy"));
        if (!last) break;
    }
    return found;
}

/// Qué es el adjunto, en corto: "Archivo de respaldo inicial" → "Respaldo inicial";
/// "Datos Control Funcional · Archivo Control" → "Control funcional".
QString attachmentName(const RequirementAttachment& a) {
    QString name = a.label.trimmed();
    QString owner;
    if (const int sep = name.lastIndexOf(QStringLiteral(" · ")); sep >= 0) {
        owner = name.left(sep);
        name = name.mid(sep + 3);
    }
    for (const QString& prefix : {QStringLiteral("archivo de "), QStringLiteral("archivo ")})
        if (plain(name).startsWith(prefix)) {
            name = name.mid(prefix.size());
            break;
        }
    if (!name.contains(QLatin1Char(' ')) && !owner.isEmpty()) {
        name = owner;
        if (plain(name).startsWith(QStringLiteral("datos "))) name = name.mid(6);
    }
    return requirement::sentenceCase(name.isEmpty() ? a.fileName : name);
}

/// Un valor de la ficha: texto seleccionable y, si es una dirección, un enlace.
QLabel* valueLabel(const QString& value, RequirementWindow* window) {
    auto* l = new QLabel;
    l->setWordWrap(true);
    const QString v = value.trimmed();
    if (!v.contains(QLatin1Char(' ')) && (v.startsWith(QStringLiteral("http://")) || v.startsWith(QStringLiteral("https://")))) {
        l->setTextFormat(Qt::RichText);
        l->setText(QStringLiteral("<a href='%1' style='color:%2;'>%3</a>").arg(v.toHtmlEscaped(), theme::Blue, v.toHtmlEscaped()));
        l->setTextInteractionFlags(Qt::TextBrowserInteraction);
        QObject::connect(l, &QLabel::linkActivated, window, &RequirementWindow::openUrlRequested);
    } else {
        l->setTextFormat(Qt::PlainText);
        l->setText(v);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }
    return l;
}

/// Una palabra tan larga (una cadena de conexión, un hash) que, partida por palabras, ensancharía la
/// ventana y cortaría el texto: se recorta con «…» y se copia entera con su botón.
bool unbreakable(const QString& value) {
    const QStringList words = value.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    return std::any_of(words.cbegin(), words.cend(), [](const QString& w) { return w.size() > 32; });
}

QPushButton* copyButton(const QString& value) {
    auto* b = new QPushButton;
    b->setObjectName(QStringLiteral("requirementCopy"));
    b->setIcon(QIcon(icons::pixmap(icons::Glyph::Copy, theme::Muted, 16)));
    b->setFixedSize(28, 28);
    b->setCursor(Qt::PointingHandCursor);
    b->setToolTip(RequirementWindow::tr("Copiar «%1»").arg(ui::elide(value, 60)));
    b->setAccessibleName(RequirementWindow::tr("Copiar"));
    QObject::connect(b, &QPushButton::clicked, b, [value]() { QApplication::clipboard()->setText(value); });
    return b;
}

QFrame* separator() {
    auto* line = new QFrame;
    line->setFixedHeight(1);
    line->setStyleSheet(QStringLiteral("background:%1;").arg(theme::Border));
    return line;
}

/// Un dato de la columna fija: el nombre pequeño encima y el valor debajo, partido en líneas.
QWidget* railFact(const QString& name, QWidget* value, QLabel* extra = nullptr) {
    auto* w = new QWidget;
    auto* v = ui::vbox(w, 0, 3);
    v->addWidget(ui::label(name, "muted-sm"));
    v->addWidget(value);
    if (extra) v->addWidget(extra);
    return w;
}

QLabel* wrapped(const QString& text, const char* role = nullptr) {
    auto* l = ui::label(text, role);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}
} // namespace

RequirementWindow::RequirementWindow(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("requirementWindow"));
    setWindowIcon(ui::appIcon());
    // Ventana de verdad, no un diálogo modal: la ficha se consulta mientras se preparan las pruebas.
    setWindowFlag(Qt::Window);
    setModal(false);
    setMinimumSize(760, 520);
    resize(1000, 700);

    auto* root = ui::hbox(this, 0, 0);

    // Columna fija con lo esencial.
    auto* rail = new QFrame;
    rail->setObjectName(QStringLiteral("requirementRail"));
    auto* railBody = new QWidget;
    m_rail = ui::vbox(railBody, 0, 16);
    m_rail->setContentsMargins(20, 20, 20, 18);
    auto* railScroll = ui::scrollable(railBody);
    railScroll->setStyleSheet(QStringLiteral("background:transparent;"));
    auto* rv = ui::vbox(rail, 0, 0);
    rv->addWidget(railScroll, 1);
    // Las acciones, al pie y siempre a la vista: arriba a la derecha le quitaban sitio al índice.
    auto* actions = new QWidget;
    auto* av = ui::vbox(actions, 0, 8);
    av->setContentsMargins(20, 10, 20, 16);
    m_load = ui::button(tr("Cargar ficha"), "outline");
    m_load->setObjectName(QStringLiteral("requirementLoad"));
    m_load->setToolTip(tr("Leer de GESREQ la ficha completa: alcance, secciones y adjuntos"));
    connect(m_load, &QPushButton::clicked, this, &RequirementWindow::loadRequested);
    av->addWidget(m_load);
    m_open = ui::button(tr("Abrir en GESREQ"), "outline");
    m_open->setObjectName(QStringLiteral("requirementOpen"));
    m_open->setToolTip(tr("Abre la ficha en el navegador (hace falta haber entrado en GESREQ)"));
    connect(m_open, &QPushButton::clicked, this, [this]() { emit openUrlRequested(m_link.data.detailUrl); });
    av->addWidget(m_open);
    m_readAt = wrapped(QString(), "muted-sm");
    av->addWidget(m_readAt);
    rv->addWidget(actions);
    rail->setFixedWidth(290);
    root->addWidget(rail);

    // Columna de contenido: el índice arriba, las secciones debajo.
    auto* main = new QWidget;
    auto* mv = ui::vbox(main, 0, 0);
    auto* bar = new QFrame;
    bar->setObjectName(QStringLiteral("requirementBar"));
    auto* bh = ui::hbox(bar, 0, 0);
    bh->setContentsMargins(20, 10, 16, 10);
    auto* nav = new QWidget;
    m_nav = new FlowLayout(nav, 0, 4, 4);
    bh->addWidget(nav, 1);
    mv->addWidget(bar);

    QWidget* content;
    m_scroll = ui::scrollArea(&content, &m_content);
    m_content->setContentsMargins(24, 20, 24, 24);
    m_content->setSpacing(26);
    connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged, this, &RequirementWindow::markCurrentSection);
    connect(m_scroll->verticalScrollBar(), &QScrollBar::rangeChanged, this, &RequirementWindow::markCurrentSection);
    mv->addWidget(m_scroll, 1);
    root->addWidget(main, 1);
}

void RequirementWindow::setRequirement(const QString& issueId, const RequirementLink& link, bool canLoad) {
    const bool same = issueId == m_issueId;
    m_issueId = issueId;
    m_link = link;
    m_canLoad = canLoad;
    // Un refresco del mismo issue (cualquier cambio en el libro) no debe devolver la ficha al principio.
    const int scrolled = same ? m_scroll->verticalScrollBar()->value() : 0;
    refresh();
    QPointer<QScrollArea> scroll(m_scroll);
    QTimer::singleShot(0, this, [scroll, scrolled]() {
        if (scroll) scroll->verticalScrollBar()->setValue(scrolled);
    });
}

void RequirementWindow::setReading(bool reading) {
    m_reading = reading;
    m_load->setEnabled(!reading);
    if (reading) m_load->setText(tr("Leyendo…"));
    else m_load->setText(m_link.detail.id.isEmpty() ? tr("Cargar ficha") : tr("Actualizar ficha"));
}

QVBoxLayout* RequirementWindow::addSection(const QString& title) {
    auto* section = new QWidget;
    auto* v = ui::vbox(section, 0, 10);
    auto* heading = new QLabel(title);
    heading->setStyleSheet(QStringLiteral("font-size:15px;font-weight:700;"));
    v->addWidget(heading);
    m_content->addWidget(section);

    auto* jump = ui::button(title, "ghost");
    jump->setObjectName(QStringLiteral("requirementJump"));
    QPointer<QWidget> target(section);
    connect(jump, &QPushButton::clicked, this, [this, target]() {
        if (target) m_scroll->verticalScrollBar()->setValue(target->y() - m_content->contentsMargins().top());
    });
    m_nav->addWidget(jump);
    m_jumps.append({jump, target});
    return v;
}

void RequirementWindow::markCurrentSection() {
    // La sección que se lee es la última cuyo título ya pasó por arriba; al final del todo, la última.
    const QScrollBar* bar = m_scroll->verticalScrollBar();
    const int top = bar->value() + m_content->contentsMargins().top() + 8;
    int current = 0;
    for (int i = 0; i < m_jumps.size(); ++i)
        if (m_jumps[i].second && m_jumps[i].second->y() <= top) current = i;
    if (bar->maximum() > 0 && bar->value() >= bar->maximum()) current = static_cast<int>(m_jumps.size()) - 1;
    for (int i = 0; i < m_jumps.size(); ++i) ui::setFlag(m_jumps[i].first, "current", i == current);
}

void RequirementWindow::refresh() {
    const ExternalRequirement& r = m_link.data;
    const bool hasDetail = !m_link.detail.id.isEmpty();
    setWindowTitle(tr("Requerimiento %1").arg(r.id));
    m_load->setVisible(m_canLoad);
    setReading(m_reading);
    m_open->setEnabled(!r.detailUrl.isEmpty());

    buildRail();

    ui::clearLayout(m_nav);
    ui::clearLayout(m_content);
    m_jumps.clear();
    buildWhatToTest(hasDetail);
    if (hasDetail) buildWhereToTest();
    buildJourney();
    buildRecord(hasDetail);
    m_content->addStretch(1);
    QTimer::singleShot(0, this, &RequirementWindow::markCurrentSection);   // con las secciones ya colocadas
}

void RequirementWindow::buildRail() {
    const ExternalRequirement& r = m_link.data;
    const RequirementDetail& d = m_link.detail;
    ui::clearLayout(m_rail);

    QStringList identity{QStringLiteral("GREQ %1").arg(r.id)};
    if (!r.systemCode.isEmpty()) identity << r.systemCode;
    m_rail->addWidget(ui::label(identity.join(QStringLiteral(" · ")), "mono-muted"));
    auto* title = wrapped(d.reference.trimmed().isEmpty() ? r.summary : d.reference);
    title->setObjectName(QStringLiteral("requirementTitle"));
    title->setStyleSheet(QStringLiteral("font-size:16px;font-weight:700;"));
    m_rail->addWidget(title);

    QStringList states = r.states;
    if (states.isEmpty() && !d.state.isEmpty()) states << d.state;
    if (!states.isEmpty()) {
        auto* list = new QWidget;
        auto* lv = ui::vbox(list, 0, 6);
        for (const QString& s : states) {
            auto* row = new QWidget;
            auto* h = ui::hbox(row, 0, 8);
            h->addWidget(ui::dot(requirement::stateColor(s), 8), 0, Qt::AlignVCenter);
            h->addWidget(wrapped(requirement::sentenceCase(s)), 1);
            lv->addWidget(row);
        }
        m_rail->addWidget(list);
    }
    if (m_link.missing) {
        auto* gone = wrapped(tr("Ya no está en tu bandeja de control de calidad (se vio por última vez el %1).").arg(when(m_link.fetchedAt)), "muted-sm");
        gone->setStyleSheet(QStringLiteral("color:%1;").arg(theme::AmberSoft));
        m_rail->addWidget(gone);
    }

    auto* facts = new QWidget;
    auto* fv = ui::vbox(facts, 0, 12);
    if (const auto period = requirement::qaPeriod(r, QDate::currentDate()); period.valid) {
        auto days = [](int n) { return n == 1 ? tr("1 día") : tr("%1 días").arg(n); };
        QString left;
        QString color = theme::Muted;
        if (m_link.missing) left.clear();
        else if (!period.started) left = tr("empieza en %1").arg(days(period.untilStart));
        else if (period.left < 0) {
            left = tr("vencido hace %1").arg(days(-period.left));
            color = theme::RedSoft;
        } else if (period.left == 0) {
            left = tr("vence hoy");
            color = theme::AmberSoft;
        } else {
            left = period.left == 1 ? tr("falta 1 día") : tr("faltan %1 días").arg(period.left);
            color = period.left <= 3 ? theme::AmberSoft : theme::Muted;
        }
        auto* value = new QWidget;
        auto* v = ui::vbox(value, 0, 6);
        auto* dates = wrapped(span(r.assignedFrom, r.assignedUntil));
        dates->setObjectName(QStringLiteral("requirementDeadline"));
        if (!left.isEmpty()) {
            dates->setTextFormat(Qt::RichText);
            dates->setText(QStringLiteral("%1 <span style='color:%2;'>· %3</span>").arg(span(r.assignedFrom, r.assignedUntil), color, left.toHtmlEscaped()));
        }
        v->addWidget(dates);
        auto* cells = new ProgressCells;
        cells->setFixedHeight(4);
        QStringList colors;
        const int filled = period.total ? (period.elapsed * 10 + period.total / 2) / period.total : 0;
        for (int i = 0; i < 10; ++i) colors << (i < filled ? (period.left < 0 ? theme::Red : theme::Amber) : theme::Border);
        cells->setColors(colors);
        v->addWidget(cells);
        fv->addWidget(railFact(tr("Plazo QA"), value));
    }
    const QString priority = d.priority.trimmed().isEmpty() ? r.priority : d.priority;
    const QString type = requirement::sentenceCase(d.requestType);
    if (!priority.trimmed().isEmpty() || !type.isEmpty()) {
        QStringList parts;
        if (!priority.trimmed().isEmpty())
            parts << QStringLiteral("<span style='color:%1;font-weight:700;'>%2</span>").arg(requirement::priorityColor(priority), requirement::sentenceCase(priority).toHtmlEscaped());
        if (!type.isEmpty()) parts << type.toHtmlEscaped();
        auto* value = wrapped(parts.join(QStringLiteral(" · ")));
        value->setTextFormat(Qt::RichText);
        fv->addWidget(railFact(type.isEmpty() ? tr("Prioridad") : tr("Prioridad · tipo"), value));
    }
    const QString requester = d.requester.trimmed().isEmpty() ? r.requester : d.requester;
    const QString unit = d.requestingUnit.trimmed().isEmpty() ? r.requestingUnit : d.requestingUnit;
    if (!requester.trimmed().isEmpty() || !unit.trimmed().isEmpty()) {
        auto* who = wrapped(requester.trimmed().isEmpty() ? requirement::sentenceCase(unit) : nameCase(requester));
        who->setObjectName(QStringLiteral("requirementRequester"));
        QLabel* where = !requester.trimmed().isEmpty() && !unit.trimmed().isEmpty() ? wrapped(requirement::sentenceCase(unit), "muted-sm") : nullptr;
        fv->addWidget(railFact(tr("Solicita"), who, where));
    }
    if (const QString qa = requirement::qaResource(d); !qa.isEmpty()) {
        auto* value = wrapped(nameCase(qa));
        value->setObjectName(QStringLiteral("requirementQaResource"));
        fv->addWidget(railFact(tr("QA asignado a"), value));
    }
    m_rail->addWidget(facts);

    if (!d.attachments.isEmpty()) {
        auto* files = new QWidget;
        auto* av = ui::vbox(files, 0, 6);
        av->addWidget(ui::label(tr("Adjuntos · %1").arg(d.attachments.size()), "eyebrow"));
        for (const auto& a : d.attachments) {
            auto* b = ui::button(QString(), "outline");
            b->setObjectName(QStringLiteral("requirementAttachment"));
            b->setToolTip(tr("%1 · abrir en el navegador").arg(a.fileName));
            b->setStyleSheet(QStringLiteral("text-align:left;padding:7px 10px;font-size:12px;"));
            // No ensancha la columna: con un nombre largo se recorta y el entero va en el tooltip.
            b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            const QString kind = QFileInfo(a.fileName).suffix().toUpper();
            const QFontMetrics fm(b->font());
            b->setText(fm.elidedText(kind.isEmpty() ? attachmentName(a) : QStringLiteral("%1 · %2").arg(attachmentName(a), kind), Qt::ElideRight, 220));
            connect(b, &QPushButton::clicked, this, [this, url = a.url]() { emit openUrlRequested(url); });
            av->addWidget(b);
        }
        m_rail->addWidget(files);
    }
    m_rail->addStretch(1);
    m_readAt->setText(!d.id.isEmpty() ? tr("Ficha leída el %1").arg(when(m_link.detailFetchedAt)) : tr("Ficha sin leer de GESREQ"));
}

void RequirementWindow::buildWhatToTest(bool hasDetail) {
    const RequirementDetail& d = m_link.detail;
    QVBoxLayout* s = addSection(tr("Qué probar"));
    if (!hasDetail) {
        s->addWidget(wrapped(m_canLoad ? tr("Todavía no se leyó la ficha de GESREQ: el alcance, dónde probar y los adjuntos llegan al pulsar «Cargar ficha».")
                                       : tr("Todavía no se leyó la ficha de GESREQ."),
                             "muted"));
        return;
    }
    if (!d.description.trimmed().isEmpty()) {
        auto* box = ui::card("grid-cell");
        auto* bv = ui::vbox(box, 0, 0);
        bv->setContentsMargins(16, 14, 16, 14);
        auto* text = wrapped(d.description);
        text->setObjectName(QStringLiteral("requirementScope"));
        text->setTextFormat(Qt::PlainText);
        text->setStyleSheet(QStringLiteral("font-size:13.5px;"));
        bv->addWidget(text);
        s->addWidget(box);
    } else {
        s->addWidget(wrapped(tr("La ficha no trae el alcance."), "muted"));
    }
    // Lo que el desarrollador y QA escribieron para la prueba, si lo escribieron.
    const QList<RequirementField> notes{
        {tr("Entrega"), requirement::blockField(d, Block::QaRequest, QStringLiteral("detalle"))},
        {tr("Indicaciones para QA"), requirement::blockField(d, Block::QaAssignment, QStringLiteral("observaciones"))},
        {tr("Notas de desarrollo"), requirement::blockField(d, Block::Development, QStringLiteral("observaciones"))},
    };
    for (const auto& n : notes) {
        if (n.value.isEmpty()) continue;
        auto* row = new QWidget;
        auto* h = ui::hbox(row, 0, 10);
        auto* name = ui::label(n.label, "muted-sm");
        name->setFixedWidth(130);
        h->addWidget(name, 0, Qt::AlignTop);
        auto* value = wrapped(n.value);
        value->setTextFormat(Qt::PlainText);
        h->addWidget(value, 1);
        s->addWidget(row);
    }
}

void RequirementWindow::buildWhereToTest() {
    const RequirementDetail& d = m_link.detail;
    QString functionalUser = requirement::blockField(d, Block::Functional, QStringLiteral("sistema/usuario"));
    if (const int slash = functionalUser.lastIndexOf(QLatin1Char('/')); slash >= 0) functionalUser = functionalUser.mid(slash + 1).trimmed();
    struct Row { QString name; QString value; bool mono; };
    const QList<Row> rows{
        {tr("Aplicación"), requirement::blockField(d, Block::QaRequest, QStringLiteral("url")), true},
        {tr("Servidor"), requirement::blockField(d, Block::QaRequest, QStringLiteral("servidor")), true},
        {tr("Base de datos"), requirement::blockField(d, Block::QaRequest, QStringLiteral("acceso bd")), true},
        {tr("Esquema"), requirement::blockField(d, Block::QaRequest, QStringLiteral("esquema")), true},
        {tr("Usuario BD"), requirement::blockField(d, Block::QaRequest, QStringLiteral("usuario bd")), true},
        {tr("Tablas"), requirement::blockField(d, Block::QaRequest, QStringLiteral("tablas")), true},
        {tr("Usuario aplicación"), requirement::blockField(d, Block::QaRequest, QStringLiteral("usuario aplicacion")), false},
        {tr("Usuario control funcional"), functionalUser, true},
        {tr("Ruta SVN"), requirement::blockField(d, Block::Development, QStringLiteral("ruta svn")), true},
    };
    if (std::none_of(rows.cbegin(), rows.cend(), [](const Row& row) { return !row.value.isEmpty(); })) return;

    QVBoxLayout* s = addSection(tr("Dónde probar"));
    auto* table = ui::card("grid-cell");
    table->setObjectName(QStringLiteral("requirementWhere"));
    auto* grid = new QGridLayout(table);
    grid->setContentsMargins(14, 4, 8, 4);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(0);
    grid->setColumnMinimumWidth(0, 150);
    grid->setColumnStretch(1, 1);
    int line = 0;
    for (const auto& row : rows) {
        if (row.value.isEmpty()) continue;
        if (line > 0) grid->addWidget(separator(), line++, 0, 1, 3);
        auto* name = ui::label(row.name, "muted-sm");
        name->setMinimumHeight(36);
        grid->addWidget(name, line, 0);
        const bool link = row.value.startsWith(QStringLiteral("http://")) || row.value.startsWith(QStringLiteral("https://"));
        QLabel* value;
        if (link) {
            value = valueLabel(row.value, this);
        } else if (unbreakable(row.value)) {
            value = new ElidedLabel(row.value);   // entero en el tooltip y en el botón de copiar
        } else {
            value = wrapped(row.value);
            value->setTextFormat(Qt::PlainText);
        }
        if (row.mono) value->setStyleSheet(kMono);
        grid->addWidget(value, line, 1);
        grid->addWidget(copyButton(row.value), line, 2);
        ++line;
    }
    s->addWidget(table);
}

void RequirementWindow::buildJourney() {
    const ExternalRequirement& r = m_link.data;
    const RequirementDetail& d = m_link.detail;
    struct Step { QString title; QStringList lines; QString color; QString textColor; };
    auto joined = [](std::initializer_list<QString> parts) {
        QStringList out;
        for (const QString& p : parts)
            if (!p.trimmed().isEmpty()) out << p;
        return out;
    };

    const QDate requested = milestone(d, QLatin1Char('S'), false);
    const QDate observed = milestone(d, QLatin1Char('O'), true);
    const QDate accepted = milestone(d, QLatin1Char('A'), true);
    QList<Step> steps;
    steps.append({tr("Solicitado"), joined({day(requested.isValid() ? requested : r.requestedOn)}), theme::Muted, QString()});
    if (!requested.isValid() && !r.requestedOn.isValid()) steps.last().lines.clear();
    steps.append({tr("Aceptado"), joined({accepted.isValid() ? day(accepted) : QString(), observed.isValid() ? tr("observado el %1").arg(day(observed)) : QString()}),
                  theme::Muted, QString()});
    steps.append({tr("Desarrollo"),
                  joined({span(requirement::blockField(d, Block::Development, QStringLiteral("fecha desde"))),
                          shortName(requirement::blockField(d, Block::Development, QStringLiteral("recurso")))}),
                  theme::Muted, QString()});
    const QString functionalState = requirement::blockField(d, Block::Functional, QStringLiteral("estado"));
    QString functionalStatus = functionalState;
    functionalStatus.remove(QRegularExpression(QStringLiteral("^\\s*control funcional\\s*"), QRegularExpression::CaseInsensitiveOption));
    const bool functionalDone = functionalState.contains(QStringLiteral("FINALIZADO"), Qt::CaseInsensitive);
    steps.append({tr("Control funcional"),
                  joined({span(requirement::blockField(d, Block::Functional, QStringLiteral("fecha control"))),
                          shortName(requirement::blockField(d, Block::Functional, QStringLiteral("usuario control"))), functionalStatus.toLower()}),
                  functionalDone ? theme::Green : theme::Muted, QString()});

    QString qaStatus, qaColor = theme::Blue, qaText;
    if (const auto period = requirement::qaPeriod(r, QDate::currentDate()); period.valid && !m_link.missing) {
        if (period.left < 0) {
            qaStatus = tr("vencido");
            qaColor = theme::Red;
            qaText = theme::RedSoft;
        } else if (period.started && period.left <= 3) {
            qaStatus = period.left == 0 ? tr("vence hoy") : tr("en curso");
            qaColor = theme::Amber;
            qaText = theme::AmberSoft;
        } else {
            qaStatus = period.started ? tr("en curso") : tr("por empezar");
        }
    }
    QDate qaFrom = r.assignedFrom, qaUntil = r.assignedUntil;
    if (!qaFrom.isValid()) {
        const QList<QDate> dates = datesIn(requirement::blockField(d, Block::QaAssignment, QStringLiteral("fecha desde")));
        qaFrom = dates.value(0);
        qaUntil = dates.value(1);
    }
    steps.append({tr("Control de calidad"), joined({span(qaFrom, qaUntil), shortName(requirement::qaResource(d)), qaStatus}), m_link.missing ? theme::Muted : qaColor, qaText});

    QVBoxLayout* s = addSection(tr("Recorrido"));
    auto* row = new QWidget;
    row->setObjectName(QStringLiteral("requirementJourney"));
    auto* grid = new QGridLayout(row);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(8);
    for (int i = 0; i < steps.size(); ++i) {
        const Step& step = steps[i];
        const bool empty = step.lines.isEmpty();
        auto* cell = new QWidget;
        auto* v = ui::vbox(cell, 0, 4);
        auto* mark = new QFrame;
        mark->setFixedHeight(3);
        mark->setStyleSheet(QStringLiteral("background:%1;border-radius:1px;").arg(empty ? theme::Border : step.color));
        v->addWidget(mark);
        v->addSpacing(4);
        auto* name = wrapped(step.title);
        name->setStyleSheet(QStringLiteral("font-size:12.5px;font-weight:700;%1").arg(empty ? QStringLiteral("color:%1;").arg(theme::Muted) : QString()));
        v->addWidget(name);
        auto* text = wrapped(empty ? QStringLiteral("—") : step.lines.join(QStringLiteral(" · ")), "muted-sm");
        if (!step.textColor.isEmpty()) text->setStyleSheet(QStringLiteral("color:%1;").arg(step.textColor));
        v->addWidget(text);
        v->addStretch(1);
        grid->addWidget(cell, 0, i);
        grid->setColumnStretch(i, 1);
    }
    s->addWidget(row);
}

void RequirementWindow::buildRecord(bool hasDetail) {
    const ExternalRequirement& r = m_link.data;
    const RequirementDetail& d = m_link.detail;
    QVBoxLayout* s = addSection(tr("Ficha GESREQ"));
    auto* note = ui::label(QString(), "muted-sm");
    note->setWordWrap(true);
    s->addWidget(note);
    int shown = 0, hidden = 0;

    auto group = [&](const QString& title, const QList<RequirementField>& fields) {
        QList<RequirementField> filled;
        for (const auto& f : fields) {
            if (f.value.trimmed().isEmpty()) ++hidden;
            else filled << f;
        }
        if (filled.isEmpty()) return;
        shown += static_cast<int>(filled.size());
        auto* box = new QWidget;
        auto* bv = ui::vbox(box, 0, 0);
        auto* heading = ui::label(title, "eyebrow");
        heading->setContentsMargins(0, 8, 0, 6);
        bv->addWidget(heading);
        bv->addWidget(separator());
        auto* rows = new QWidget;
        auto* grid = new QGridLayout(rows);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(12);
        grid->setVerticalSpacing(0);
        grid->setColumnMinimumWidth(0, 170);
        grid->setColumnStretch(1, 1);
        int line = 0;
        for (const auto& f : filled) {
            QString label = f.label.trimmed();
            if (label.endsWith(QLatin1Char(':'))) label.chop(1);
            auto* name = wrapped(label, "muted-sm");
            name->setContentsMargins(0, 7, 0, 7);
            grid->addWidget(name, line, 0, Qt::AlignTop);
            QLabel* value;
            if (unbreakable(f.value) && !f.value.trimmed().startsWith(QStringLiteral("http"))) {
                value = new ElidedLabel(f.value.trimmed());
                value->setStyleSheet(kMono);
            } else {
                value = valueLabel(f.value, this);
            }
            value->setContentsMargins(0, 7, 0, 7);
            grid->addWidget(value, line, 1, Qt::AlignTop);
            if (unbreakable(f.value)) grid->addWidget(copyButton(f.value.trimmed()), line, 2, Qt::AlignTop);
            ++line;
            grid->addWidget(separator(), line++, 0, 1, 3);
        }
        bv->addWidget(rows);
        s->addWidget(box);
    };

    if (hasDetail) {
        // Lo que ya dice la columna de la izquierda (y los adjuntos, que están allí) no se repite.
        static const QStringList inRail{QStringLiteral("tipo solicitud"), QStringLiteral("referencia"), QStringLiteral("unidad solicitante"),
                                        QStringLiteral("funcionario solicitante"), QStringLiteral("prioridad"), QStringLiteral("estado"),
                                        QStringLiteral("sistema"), QStringLiteral("archivo de respaldo")};
        QList<RequirementField> general;
        for (const auto& f : d.fields) {
            const QString l = plain(f.label);
            if (std::none_of(inRail.cbegin(), inRail.cend(), [&l](const QString& p) { return l.startsWith(p); })) general << f;
        }
        group(tr("General"), general);
        for (const auto& section : d.sections) {
            QString title = section.title.trimmed();
            if (plain(title).startsWith(QStringLiteral("datos "))) title = title.mid(6);
            if (&section == requirement::block(d, Block::Development)) title = tr("Desarrollo");
            group(requirement::sentenceCase(title), section.fields);
        }
    }
    // Lo que trae la fila de la bandeja y no está en la ficha.
    group(tr("Bandeja"), {
                             {tr("Sistema"), r.system},
                             {tr("Usuario"), r.user},
                             {tr("Solicitado"), r.requestedOn.isValid() ? day(r.requestedOn) : QString()},
                             {tr("Importado"), m_link.importedAt.isValid() ? tr("%1 · última lectura %2").arg(when(m_link.importedAt), when(m_link.fetchedAt))
                                                                           : tr("Sin issue · última lectura %1").arg(when(m_link.fetchedAt))},
                         });
    const QString data = shown == 1 ? tr("1 dato") : tr("%1 datos").arg(shown);
    if (hidden == 0) note->setText(data);
    else note->setText(hidden == 1 ? tr("%1 · 1 vacío oculto").arg(data) : tr("%1 · %2 vacíos ocultos").arg(data).arg(hidden));
}

} // namespace qaflow
