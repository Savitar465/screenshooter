#include "CycleStartDialog.h"

#include "presentation/theme/Theme.h"
#include "presentation/widgets/ChoiceDialog.h"
#include "presentation/widgets/Ui.h"
#include "presentation/widgets/WrapCheckBox.h"

#include <QCheckBox>
#include <QComboBox>

#include <algorithm>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>

namespace qaflow {

namespace {
constexpr int CaseIdRole = Qt::UserRole;
constexpr int AddedRole = Qt::UserRole + 1;

QString caseText(const CycleStartDialog::Setup::CaseOption& c) {
    QString text = c.title.trimmed().isEmpty() ? c.id : QStringLiteral("%1 · %2").arg(c.id, c.title.trimmed());
    if (!c.hint.trimmed().isEmpty()) text += QStringLiteral("  —  %1").arg(c.hint.trimmed());
    return text;
}
} // namespace

CycleStartDialog::CycleStartDialog(const Setup& setup, QWidget* parent) : QDialog(parent) {
    const bool continuing = !setup.continuation.trimmed().isEmpty();
    setObjectName(QStringLiteral("cycleStartDialog"));
    setWindowTitle(continuing ? tr("Continuar ciclo") : tr("Arrancar ciclo"));
    setWindowIcon(ui::appIcon());
    setMinimumWidth(460);

    QVBoxLayout* footer = nullptr;
    auto* v = ui::scrollingBody(this, 18, 12, &footer);
    v->addWidget(ui::label(setup.planName.trimmed().isEmpty() ? tr("Arrancar el ciclo") : setup.planName.trimmed(), "h2"));
    auto* subtitle = ui::label(setup.context.trimmed().isEmpty()
                                   ? tr("Este ciclo no prueba ningún requerimiento: es una ejecución suelta del plan.")
                                   : tr("El ciclo quedará anotado en %1.").arg(setup.context.trimmed()),
                               "muted-sm");
    subtitle->setWordWrap(true);
    v->addWidget(subtitle);

    // Continuar no repite el plan entero: conviene ver qué se va a ejecutar antes de arrancar.
    if (continuing) {
        auto* note = ui::label(setup.continuation.trimmed(), "muted-sm");
        note->setObjectName(QStringLiteral("cycleStartContinuation"));
        note->setWordWrap(true);
        note->setStyleSheet(QStringLiteral("color:%1;").arg(theme::Amber));
        v->addWidget(note);
    }

    auto* field = new QWidget;
    auto* fv = ui::vbox(field, 0, 6);
    fv->addWidget(ui::label(setup.fixedEnvironment || setup.phases ? tr("FASE") : tr("AMBIENTE"), "eyebrow"));
    m_environment = new QComboBox;
    m_environment->setObjectName(QStringLiteral("cycleStartEnvironment"));
    m_blocked = setup.blocked;
    m_notes = setup.notes;
    if (setup.fixedEnvironment) {
        m_environment->addItem(setup.environment.trimmed());
        m_environment->setEnabled(false);
    } else if (setup.phases) {
        // Las fases del requerimiento, sin escribir otra: la fase decide la revisión y su ciclo de Zephyr.
        m_environment->addItems(setup.environments);
        m_environment->setCurrentIndex(std::max(0, int(setup.environments.indexOf(setup.environment.trimmed()))));
    } else {
        m_environment->setEditable(true);   // los ambientes de cada organización no son sólo éstos
        m_environment->addItems(setup.environments);
        m_environment->setCurrentText(setup.environment.trimmed());
    }
    fv->addWidget(m_environment);
    v->addWidget(field);

    if (setup.fixedEnvironment || setup.phases) {
        auto* phase = ui::label(setup.fixedEnvironment
                                    ? tr("Continúa en la fase del ciclo que continúa.")
                                    : tr("Se propone la fase que le toca a la revisión; aprobada una fase desde el issue, se pasa a la siguiente."),
                                "muted-sm");
        phase->setObjectName(QStringLiteral("cycleStartPhase"));
        phase->setWordWrap(true);
        v->addWidget(phase);
        m_blockedNote = ui::label(QString(), "muted-sm");
        m_blockedNote->setObjectName(QStringLiteral("cycleStartBlocked"));
        m_blockedNote->setWordWrap(true);
        m_blockedNote->setStyleSheet(QStringLiteral("color:%1;").arg(theme::Amber));
        v->addWidget(m_blockedNote);
    }

    auto* hint = ui::label(tr("Va con el ciclo a Zephyr: en el nombre del ciclo y en su campo «environment», "
                              "para saber dónde se obtuvieron estos resultados."), "muted-sm");
    hint->setWordWrap(true);
    v->addWidget(hint);

    // Qué se prueba en este ciclo: el plan entero salvo lo que se desmarque, más lo que se añada.
    if (!continuing && !setup.cases.isEmpty()) {
        m_catalog = setup.catalog;
        auto* header = new QWidget;
        auto* hh = ui::hbox(header, 0, 8);
        hh->addWidget(ui::label(tr("CASOS DE ESTE CICLO"), "eyebrow"));
        hh->addStretch(1);
        m_caseCount = ui::label(QString(), "muted-sm");
        m_caseCount->setObjectName(QStringLiteral("cycleStartCaseCount"));
        hh->addWidget(m_caseCount);
        v->addWidget(header);

        auto* casesHint = ui::label(setup.previousCycle.trimmed().isEmpty()
                                        ? tr("Desmarca los que no se van a probar en este ciclo: no salen del plan, sólo de este ciclo.")
                                        : tr("Desmarca los que no se van a probar en este ciclo: no salen del plan, sólo de este ciclo. "
                                             "Al lado, cómo quedó cada uno en %1.").arg(setup.previousCycle.trimmed()),
                                    "muted-sm");
        casesHint->setWordWrap(true);
        v->addWidget(casesHint);

        m_cases = new QListWidget;
        m_cases->setObjectName(QStringLiteral("cycleStartCases"));
        m_cases->setMinimumHeight(120);
        m_cases->setMaximumHeight(260);
        for (const auto& c : setup.cases) {
            auto* item = new QListWidgetItem(caseText(c), m_cases);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Checked);
            item->setData(CaseIdRole, c.id);
            item->setData(AddedRole, false);
            item->setToolTip(caseText(c));
        }
        v->addWidget(m_cases);

        auto* tools = new QWidget;
        auto* th = ui::hbox(tools, 0, 8);
        auto* all = ui::button(tr("Todos"), "ghost");
        all->setObjectName(QStringLiteral("cycleStartAll"));
        auto* none = ui::button(tr("Ninguno"), "ghost");
        none->setObjectName(QStringLiteral("cycleStartNone"));
        auto* add = ui::button(tr("Añadir caso…"), "outline");
        add->setObjectName(QStringLiteral("cycleStartAddCase"));
        add->setEnabled(!m_catalog.isEmpty());
        if (m_catalog.isEmpty()) add->setToolTip(tr("El plan ya tiene todos los casos del proyecto"));
        th->addWidget(all);
        th->addWidget(none);
        th->addStretch(1);
        th->addWidget(add);
        v->addWidget(tools);

        m_addToPlan = new WrapCheckBox(tr("Añadir también al plan los casos añadidos"));
        m_addToPlan->setObjectName(QStringLiteral("cycleStartAddToPlan"));
        m_addToPlan->setToolTip(tr("Sin marcar, los añadidos son sólo de este ciclo; marcado, los siguientes ciclos del plan también los tendrán."));
        m_addToPlan->setVisible(false);
        v->addWidget(m_addToPlan);

        auto setAll = [this](Qt::CheckState state) {
            for (int i = 0; i < m_cases->count(); ++i) m_cases->item(i)->setCheckState(state);
        };
        connect(all, &QPushButton::clicked, this, [setAll]() { setAll(Qt::Checked); });
        connect(none, &QPushButton::clicked, this, [setAll]() { setAll(Qt::Unchecked); });
        connect(add, &QPushButton::clicked, this, &CycleStartDialog::pickCase);
        connect(m_cases, &QListWidget::itemChanged, this, [this](QListWidgetItem*) { refreshBlocked(); });
    }

    auto* buttons = new QWidget;
    auto* bh = ui::hbox(buttons, 0, 8);
    bh->addStretch(1);
    auto* cancel = ui::button(tr("Cancelar"), "outline");
    auto* accept = ui::button(continuing ? tr("Continuar") : tr("Arrancar"), "primary");
    accept->setObjectName(QStringLiteral("cycleStartAccept"));
    m_accept = accept;
    accept->setDefault(true);
    bh->addWidget(cancel);
    bh->addWidget(accept);
    footer->addWidget(buttons);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(accept, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_environment, &QComboBox::currentTextChanged, this, [this](const QString&) { refreshBlocked(); });
    refreshBlocked();
    if (setup.fixedEnvironment || setup.phases) {
        accept->setFocus();
    } else {
        m_environment->setFocus();
        if (auto* edit = m_environment->lineEdit()) edit->selectAll();
    }
}

QString CycleStartDialog::environment() const { return m_environment->currentText().trimmed(); }

QStringList CycleStartDialog::caseIds() const {
    QStringList plan, added;
    if (!m_cases) return {};
    for (int i = 0; i < m_cases->count(); ++i) {
        const QListWidgetItem* item = m_cases->item(i);
        if (item->checkState() != Qt::Checked) continue;
        (item->data(AddedRole).toBool() ? added : plan) << item->data(CaseIdRole).toString();
    }
    return plan + added;
}

QStringList CycleStartDialog::excludedCaseIds() const {
    QStringList out;
    if (!m_cases) return out;
    for (int i = 0; i < m_cases->count(); ++i) {
        const QListWidgetItem* item = m_cases->item(i);
        if (!item->data(AddedRole).toBool() && item->checkState() != Qt::Checked) out << item->data(CaseIdRole).toString();
    }
    return out;
}

QStringList CycleStartDialog::addedCaseIds() const {
    QStringList out;
    if (!m_cases) return out;
    for (int i = 0; i < m_cases->count(); ++i) {
        const QListWidgetItem* item = m_cases->item(i);
        if (item->data(AddedRole).toBool() && item->checkState() == Qt::Checked) out << item->data(CaseIdRole).toString();
    }
    return out;
}

bool CycleStartDialog::addToPlan() const { return m_addToPlan && m_addToPlan->isChecked() && !addedCaseIds().isEmpty(); }

void CycleStartDialog::addCase(const QString& caseId) {
    if (!m_cases) return;
    for (int i = 0; i < m_cases->count(); ++i) {
        QListWidgetItem* item = m_cases->item(i);
        if (item->data(CaseIdRole).toString() == caseId) {   // ya estaba: basta con marcarlo
            item->setCheckState(Qt::Checked);
            m_cases->scrollToItem(item);
            return;
        }
    }
    const auto it = std::find_if(m_catalog.cbegin(), m_catalog.cend(), [&caseId](const Setup::CaseOption& c) { return c.id == caseId; });
    if (it == m_catalog.cend()) return;
    Setup::CaseOption option = *it;
    option.hint = tr("añadido a este ciclo");
    auto* item = new QListWidgetItem(caseText(option));
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setData(CaseIdRole, caseId);
    item->setData(AddedRole, true);
    item->setToolTip(caseText(option));
    {
        const QSignalBlocker block(m_cases);
        m_cases->addItem(item);
        item->setCheckState(Qt::Checked);
    }
    m_cases->scrollToItem(item);
    refreshBlocked();
}

void CycleStartDialog::pickCase() {
    // Los del proyecto que la lista todavía no tiene.
    QList<Choice> choices;
    for (const auto& c : std::as_const(m_catalog)) {
        bool listed = false;
        for (int i = 0; i < m_cases->count() && !listed; ++i) listed = m_cases->item(i)->data(CaseIdRole).toString() == c.id;
        if (!listed) choices << Choice{c.id, c.title.trimmed().isEmpty() ? tr("(sin título)") : c.title, c.hint};
    }
    auto* dialog = new ChoiceDialog(tr("Añadir caso al ciclo"), tr("Cargando los casos…"),
                                    [choices](const ChoiceDialog::Loaded& done) {
                                        done(choices, choices.isEmpty() ? tr("Ya están en la lista todos los casos del proyecto") : QString());
                                    },
                                    QString(), this);
    dialog->setObjectName(QStringLiteral("cycleStartPickCase"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &ChoiceDialog::chosen, this, &CycleStartDialog::addCase);
    dialog->open();
}

void CycleStartDialog::refreshCases() {
    if (!m_cases) return;
    int planned = 0, chosen = 0;
    for (int i = 0; i < m_cases->count(); ++i) {
        const QListWidgetItem* item = m_cases->item(i);
        if (!item->data(AddedRole).toBool()) ++planned;
    }
    chosen = int(caseIds().size());
    const int added = int(addedCaseIds().size());
    m_caseCount->setText(added > 0 ? tr("%1 caso(s) · %2 añadido(s)").arg(chosen).arg(added)
                                   : tr("%1 de %2 casos").arg(chosen).arg(planned));
    m_addToPlan->setVisible(added > 0);
}

void CycleStartDialog::refreshBlocked() {
    const QString reason = m_blocked.value(environment());
    // Lo que impide arrancar va en ámbar; lo que sólo conviene saber, en verde.
    const QString text = reason.isEmpty() ? m_notes.value(environment()) : reason;
    if (m_blockedNote) {
        m_blockedNote->setText(text);
        m_blockedNote->setVisible(!text.isEmpty());
        m_blockedNote->setStyleSheet(QStringLiteral("color:%1;").arg(reason.isEmpty() ? theme::Green : theme::Amber));
    }
    refreshCases();
    // Sin casos marcados no hay ciclo que arrancar.
    const bool empty = m_cases && caseIds().isEmpty();
    if (m_accept) {
        m_accept->setEnabled(reason.isEmpty() && !empty);
        m_accept->setToolTip(empty ? tr("Marca al menos un caso para arrancar el ciclo") : QString());
    }
}

} // namespace qaflow
