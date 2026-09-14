#include "ProjectSetupDialog.h"

#include "application/BugReportService.h"
#include "application/ProjectStore.h"
#include "application/RequirementSourceService.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/ChoiceDialog.h"
#include "presentation/widgets/Ui.h"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>

namespace qaflow {

namespace {
QWidget* field(const QString& title, QWidget* w) {
    auto* box = new QWidget;
    auto* v = ui::vbox(box, 0, 6);
    v->addWidget(ui::label(title.toUpper(), "eyebrow"));
    v->addWidget(w);
    return box;
}
QWidget* withButton(QWidget* edit, QPushButton* button) {
    auto* row = new QWidget;
    auto* h = ui::hbox(row, 0, 8);
    h->addWidget(edit, 1);
    h->addWidget(button);
    return row;
}
} // namespace

ProjectSetupDialog::ProjectSetupDialog(ProjectStore& projects, const QString& system, bool allowExisting,
                                       BugReportService* bugs, RequirementSourceService* requirements, QWidget* parent)
    : QDialog(parent), m_projects(projects), m_bugs(bugs), m_requirements(requirements) {
    setObjectName(QStringLiteral("projectSetupDialog"));
    setWindowTitle(system.isEmpty() ? tr("Nuevo proyecto") : tr("Proyecto para %1").arg(system));
    setWindowIcon(ui::appIcon());
    setMinimumWidth(520);

    auto* v = ui::vbox(this, 18, 12);
    v->addWidget(ui::label(system.isEmpty() ? tr("Nuevo proyecto")
                                            : tr("Ningún proyecto trabaja los requerimientos de %1").arg(system), "h2"));
    auto* subtitle = ui::label(system.isEmpty()
                                   ? tr("El código de Jira y el sistema de GESREQ son opcionales: se pueden dejar para luego "
                                        "en Ajustes → Configuración del proyecto.")
                                   : tr("Crea el proyecto en el que se probarán, o elige uno que ya exista para vincularle "
                                        "ese sistema. Después se abre allí el requerimiento."), "muted-sm");
    subtitle->setWordWrap(true);
    v->addWidget(subtitle);

    if (allowExisting) {
        m_target = new QComboBox;
        m_target->setObjectName(QStringLiteral("projectSetupTarget"));
        m_target->addItem(tr("Proyecto nuevo…"), QString());
        for (const auto& p : m_projects.projects()) m_target->addItem(p.name, p.id);
        v->addWidget(field(tr("Proyecto"), m_target));
        connect(m_target, &QComboBox::currentIndexChanged, this, [this]() { refresh(); });
    }

    m_name = new QLineEdit;
    m_name->setObjectName(QStringLiteral("projectSetupName"));
    m_name->setPlaceholderText(tr("Tránsitos"));
    // Del sistema sale un nombre razonable de partida, que se puede cambiar antes de crear.
    m_name->setText(system);
    m_nameField = field(tr("Nombre del proyecto"), m_name);
    v->addWidget(m_nameField);

    m_jira = new QLineEdit;
    m_jira->setObjectName(QStringLiteral("projectSetupJira"));
    m_jira->setProperty("role", QStringLiteral("mono"));
    m_jira->setPlaceholderText(QStringLiteral("SHOP"));
    m_jiraPick = ui::button(tr("Buscar…"), "outline");
    m_jiraPick->setObjectName(QStringLiteral("projectSetupJiraPick"));
    m_jiraPick->setToolTip(tr("Elegir entre los proyectos que ves en Jira"));
    connect(m_jiraPick, &QPushButton::clicked, this, &ProjectSetupDialog::pickJiraProject);
    m_jiraField = field(tr("Código del proyecto Jira (opcional)"), withButton(m_jira, m_jiraPick));
    v->addWidget(m_jiraField);

    m_system = new QLineEdit;
    m_system->setObjectName(QStringLiteral("projectSetupSystem"));
    m_system->setProperty("role", QStringLiteral("mono"));
    m_system->setPlaceholderText(QStringLiteral("SUMA TRANSITO"));
    m_system->setToolTip(tr("Código del sistema en GESREQ: lo que va antes del guion en la columna «Sistema» de la bandeja"));
    m_system->setText(system);
    m_systemPick = ui::button(tr("Buscar…"), "outline");
    m_systemPick->setObjectName(QStringLiteral("projectSetupSystemPick"));
    m_systemPick->setToolTip(tr("Elegir del catálogo de sistemas de GESREQ"));
    m_systemPick->setVisible(m_requirements != nullptr);
    connect(m_systemPick, &QPushButton::clicked, this, &ProjectSetupDialog::pickRequirementSystem);
    v->addWidget(field(tr("Sistema de GESREQ (opcional)"), withButton(m_system, m_systemPick)));
    connect(m_system, &QLineEdit::textEdited, this, &ProjectSetupDialog::refreshNote);

    m_note = ui::label(QString(), "muted-sm");
    m_note->setObjectName(QStringLiteral("projectSetupNote"));
    m_note->setWordWrap(true);
    v->addWidget(m_note);

    auto* buttons = new QWidget;
    auto* bh = ui::hbox(buttons, 0, 8);
    bh->addStretch(1);
    auto* cancel = ui::button(tr("Cancelar"), "outline");
    auto* accept = ui::button(system.isEmpty() ? tr("Crear proyecto") : tr("Crear y empezar"), "primary");
    accept->setObjectName(QStringLiteral("projectSetupAccept"));
    accept->setDefault(true);
    bh->addWidget(cancel);
    bh->addWidget(accept);
    v->addWidget(buttons);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(accept, &QPushButton::clicked, this, &ProjectSetupDialog::accept);

    refresh();
    m_name->setFocus();
    m_name->selectAll();
}

QString ProjectSetupDialog::target() const { return m_target ? m_target->currentData().toString() : QString(); }

void ProjectSetupDialog::refresh() {
    const bool creating = target().isEmpty();
    m_nameField->setVisible(creating);
    // El código Jira de un proyecto que ya existe vive en sus ajustes, que sólo tiene abiertos su sesión:
    // aquí sólo se ofrece al crearlo, y luego se cambia en «Configuración del proyecto».
    m_jiraField->setVisible(creating);
    m_jiraPick->setEnabled(m_bugs && m_bugs->canListProjects());
    refreshNote();
}

void ProjectSetupDialog::setNote(const QString& text, bool warning) {
    m_note->setText(text);
    m_note->setStyleSheet(QStringLiteral("font-size:11.5px;color:%1;").arg(warning ? theme::AmberSoft : theme::Muted));
}

void ProjectSetupDialog::refreshNote() {
    const QString system = m_system->text().simplified();
    const QString chosen = target();
    if (system.isEmpty()) {
        setNote(tr("Sin sistema de GESREQ el proyecto no importa requerimientos; se le puede vincular uno después."), false);
        return;
    }
    if (const QString owner = m_projects.projectForRequirementSystem(system, chosen); !owner.isEmpty()) {
        setNote(tr("⚠ «%1» ya está vinculado al proyecto «%2»: cada sistema de GESREQ se trabaja en un único proyecto.")
                    .arg(system, m_projects.find(owner)->name), true);
        return;
    }
    const Project* project = chosen.isEmpty() ? nullptr : m_projects.find(chosen);
    if (project && !project->requirementSystem.isEmpty() && project->requirementSystem.compare(system, Qt::CaseInsensitive) != 0) {
        setNote(tr("⚠ «%1» deja de trabajar «%2» y pasa a trabajar «%3».").arg(project->name, project->requirementSystem, system), true);
        return;
    }
    setNote(tr("Los requerimientos de GESREQ del sistema «%1» se trabajarán en este proyecto.").arg(system), false);
}

void ProjectSetupDialog::pickJiraProject() {
    if (!m_bugs) return;
    auto* dialog = new ChoiceDialog(tr("Proyecto de Jira"), tr("Consultando los proyectos de Jira…"),
                                    [this](const ChoiceDialog::Loaded& done) {
                                        m_bugs->fetchProjects([done](const TrackerProjectList& r) {
                                            if (!r.ok) { done({}, tr("No se pudieron consultar los proyectos de Jira · %1").arg(r.error)); return; }
                                            QList<Choice> choices;
                                            for (const auto& p : r.projects) choices << Choice{p.key, p.name, {}};
                                            done(choices, {});
                                        });
                                    },
                                    m_jira->text(), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &ChoiceDialog::chosen, this, [this](const QString& key) { m_jira->setText(key); });
    dialog->open();
}

void ProjectSetupDialog::pickRequirementSystem() {
    if (!m_requirements) return;
    QPointer<ProjectSetupDialog> self(this);
    auto* dialog = new ChoiceDialog(tr("Sistema de GESREQ"), tr("Consultando el catálogo de sistemas de GESREQ…"),
                                    [self](const ChoiceDialog::Loaded& done) {
                                        if (!self) return;
                                        self->m_requirements->fetchSystems([self, done](const RequirementSystemsResult& r) {
                                            if (!self) return;
                                            if (!r.ok) { done({}, tr("No se pudo consultar el catálogo de sistemas de GESREQ · %1").arg(r.error)); return; }
                                            const QStringList inInbox = self->m_requirements->systems();
                                            const QString chosen = self->target();
                                            QList<Choice> assigned, rest;
                                            for (const auto& system : r.systems) {
                                                QStringList hints;
                                                const bool mine = inInbox.contains(system.code, Qt::CaseInsensitive);
                                                if (mine) hints << tr("en tu bandeja");
                                                const QString owner = self->m_projects.projectForRequirementSystem(system.code, chosen);
                                                if (!owner.isEmpty()) hints << tr("vinculado a «%1»").arg(self->m_projects.find(owner)->name);
                                                (mine ? assigned : rest) << Choice{system.code, system.name, hints.join(QStringLiteral(" · "))};
                                            }
                                            // Los sistemas con requerimientos en tu bandeja van primero: son los que se buscan.
                                            done(assigned + rest, {});
                                        });
                                    },
                                    m_system->text(), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &ChoiceDialog::chosen, this, [this](const QString& code) {
        m_system->setText(code);
        refreshNote();
    });
    dialog->open();
}

void ProjectSetupDialog::accept() {
    const QString system = m_system->text().simplified();
    const QString chosen = target();
    // Lo que no se puede guardar se dice aquí mismo y el diálogo sigue abierto: nada a medias.
    if (!system.isEmpty() && !m_projects.projectForRequirementSystem(system, chosen).isEmpty()) {
        refreshNote();
        m_system->setFocus();
        return;
    }
    QString id = chosen;
    if (id.isEmpty()) id = m_created;   // un intento anterior ya lo creó; no se crea otro
    if (id.isEmpty()) {
        const QString name = m_name->text().trimmed();
        if (name.isEmpty()) {
            setNote(tr("Escribe el nombre del proyecto."), true);
            m_name->setFocus();
            return;
        }
        id = m_projects.create(name);
        if (id.isEmpty()) return;   // ProjectStore ya avisa de por qué no pudo
        m_created = id;
    }
    if (!system.isEmpty() && !m_projects.setRequirementSystem(id, system)) {
        setNote(tr("El proyecto está creado, pero no se pudo guardar su sistema de GESREQ. Inténtalo otra vez."), true);
        return;
    }
    m_projectId = id;
    m_jiraProject = m_jiraField->isVisible() ? m_jira->text().trimmed() : QString();
    QDialog::accept();
}

} // namespace qaflow
