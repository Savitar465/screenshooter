#include "SettingsView.h"

#include "application/BugReportService.h"
#include "application/SettingsStore.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/Ui.h"

#include <QComboBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>

namespace qaflow {

namespace {
QWidget* field(const QString& title, QWidget* w) {
    auto* box = new QWidget;
    auto* v = ui::vbox(box, 0, 6);
    v->addWidget(ui::label(title.toUpper(), "eyebrow"));
    v->addWidget(w);
    return box;
}
QFrame* section(const QString& accent, const QString& title, const QString& subtitle, QWidget* headerRight, QVBoxLayout** body) {
    auto* card = ui::card("card-lg");
    auto* h = ui::hbox(card, 0, 0);
    h->addWidget(ui::accentBar(accent));
    auto* content = new QWidget;
    auto* v = ui::vbox(content, 0, 14);
    v->setContentsMargins(22, 22, 24, 22);
    auto* head = new QWidget;
    auto* hh = ui::hbox(head, 0, 12);
    auto* text = new QWidget;
    auto* tv = ui::vbox(text, 0, 0);
    tv->addWidget(ui::label(title, "h2"));
    auto* sub = ui::label(subtitle, "muted");
    sub->setTextFormat(Qt::RichText);
    sub->setWordWrap(true);
    tv->addWidget(sub);
    hh->addWidget(text, 1);
    if (headerRight) hh->addWidget(headerRight, 0, Qt::AlignTop);
    v->addWidget(head);
    h->addWidget(content, 1);
    *body = v;
    return card;
}
} // namespace

SettingsView::SettingsView(SettingsStore& settings, BugReportService& bugs, QWidget* parent)
    : QWidget(parent), m_settings(settings), m_bugs(bugs) {
    auto* root = ui::hbox(this, 0, 0);
    QWidget* content;
    QVBoxLayout* outer;
    auto* sa = ui::scrollArea(&content, &outer);
    outer->setContentsMargins(32, 28, 32, 28);
    auto* page = new QWidget;
    page->setMaximumWidth(760);
    auto* v = ui::vbox(page, 0, 18);
    outer->addWidget(page, 0, Qt::AlignTop);
    root->addWidget(sa, 1);

    auto* head = new QWidget;
    auto* hv = ui::vbox(head, 0, 0);
    hv->addWidget(ui::label(QStringLiteral("CONFIGURACIÓN"), "eyebrow"));
    hv->addWidget(ui::label(QStringLiteral("Ajustes e integraciones"), "h1"));
    v->addWidget(head);

    // Jira
    m_badge = ui::button(QString(), "badge");
    m_badge->setToolTip(QStringLiteral("Probar la conexión con Jira"));
    connect(m_badge, &QPushButton::clicked, this, &SettingsView::testConnection);
    QVBoxLayout* jb;
    auto* jira = section(QStringLiteral("#3b82f6"), QStringLiteral("Jira"),
                         QStringLiteral("Los bugs se crean como issues tipo <b style=\"color:#e6edf3\">Bug</b> en el proyecto indicado."), m_badge, &jb);
    auto* jrow = new QWidget;
    auto* jg = new QGridLayout(jrow);
    jg->setContentsMargins(0, 0, 0, 0);
    jg->setHorizontalSpacing(12);
    m_url = new QLineEdit;
    m_project = new QLineEdit;
    m_project->setProperty("role", QStringLiteral("mono"));
    jg->addWidget(field(QStringLiteral("URL de la instancia"), m_url), 0, 0);
    jg->addWidget(field(QStringLiteral("Proyecto"), m_project), 0, 1);
    jg->setColumnStretch(0, 2);
    jg->setColumnStretch(1, 1);
    jb->addWidget(jrow);
    auto* arow = new QWidget;
    auto* ag = new QGridLayout(arow);
    ag->setContentsMargins(0, 0, 0, 0);
    ag->setHorizontalSpacing(12);
    m_email = new QLineEdit;
    m_email->setPlaceholderText(QStringLiteral("Sólo Jira Cloud · vacío para usar un PAT"));
    m_token = new QLineEdit;
    m_token->setEchoMode(QLineEdit::Password);
    ag->addWidget(field(QStringLiteral("Correo de la cuenta"), m_email), 0, 0);
    ag->addWidget(field(QStringLiteral("Token de API"), m_token), 0, 1);
    ag->setColumnStretch(0, 1);
    ag->setColumnStretch(1, 2);
    jb->addWidget(arow);
    v->addWidget(jira);

    auto bindJira = [this](QLineEdit* e, void (*apply)(JiraSettings&, const QString&)) {
        connect(e, &QLineEdit::textEdited, this, [this, apply](const QString& t) {
            m_selfEdit = true;
            m_settings.updateJira([&](JiraSettings& j) { apply(j, t); j.connected = false; });
            m_selfEdit = false;
        });
    };
    bindJira(m_url, [](JiraSettings& j, const QString& t) { j.url = t; });
    bindJira(m_project, [](JiraSettings& j, const QString& t) { j.project = t; });
    bindJira(m_email, [](JiraSettings& j, const QString& t) { j.email = t; });
    bindJira(m_token, [](JiraSettings& j, const QString& t) { j.token = t; });

    // Capturas
    QVBoxLayout* cb;
    auto* cap = section(theme::Cyan, QStringLiteral("Capturas de pantalla"),
                        QStringLiteral("Se guardan localmente y se adjuntan al paso activo de la ejecución."), nullptr, &cb);
    auto* crow = new QWidget;
    auto* cg = new QGridLayout(crow);
    cg->setContentsMargins(0, 0, 0, 0);
    cg->setHorizontalSpacing(12);
    m_shortcut = new QLineEdit;
    m_shortcut->setProperty("role", QStringLiteral("mono"));
    m_shortcut->setToolTip(QStringLiteral("Atajo activo mientras QAflow tiene el foco"));
    m_format = new QComboBox;
    m_format->addItems({QStringLiteral("PNG"), QStringLiteral("JPG"), QStringLiteral("WebP")});
    m_mode = new QComboBox;
    m_mode->addItems({toString(CaptureMode::FullScreen), toString(CaptureMode::ActiveWindow), toString(CaptureMode::Region)});
    cg->addWidget(field(QStringLiteral("Atajo"), m_shortcut), 0, 0);
    cg->addWidget(field(QStringLiteral("Formato"), m_format), 0, 1);
    cg->addWidget(field(QStringLiteral("Modo"), m_mode), 0, 2);
    for (int i = 0; i < 3; ++i) cg->setColumnStretch(i, 1);
    cb->addWidget(crow);
    auto* frow = new QWidget;
    auto* fh = ui::hbox(frow, 0, 8);
    m_folder = new QLineEdit;
    m_folder->setProperty("role", QStringLiteral("mono"));
    fh->addWidget(m_folder, 1);
    auto* browse = ui::button(QStringLiteral("Elegir…"), "outline");
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Carpeta de capturas"), m_folder->text());
        if (!dir.isEmpty()) m_settings.updateCapture([&](CaptureSettings& c) { c.folder = dir; });
    });
    fh->addWidget(browse);
    cb->addWidget(field(QStringLiteral("Carpeta"), frow));
    v->addWidget(cap);

    connect(m_shortcut, &QLineEdit::editingFinished, this, [this]() {
        m_selfEdit = true;
        m_settings.updateCapture([&](CaptureSettings& c) { c.shortcut = m_shortcut->text().trimmed(); });
        m_selfEdit = false;
    });
    connect(m_format, &QComboBox::currentTextChanged, this, [this](const QString& t) {
        if (m_selfEdit) return;
        m_selfEdit = true; m_settings.updateCapture([&](CaptureSettings& c) { c.format = t; }); m_selfEdit = false;
    });
    connect(m_mode, &QComboBox::currentTextChanged, this, [this](const QString& t) {
        if (m_selfEdit) return;
        m_selfEdit = true; m_settings.updateCapture([&](CaptureSettings& c) { c.mode = captureModeFromString(t); }); m_selfEdit = false;
    });
    connect(m_folder, &QLineEdit::textEdited, this, [this](const QString& t) {
        m_selfEdit = true; m_settings.updateCapture([&](CaptureSettings& c) { c.folder = t; }); m_selfEdit = false;
    });

    connect(&m_settings, &SettingsStore::jiraChanged, this, &SettingsView::refreshJira);
    connect(&m_settings, &SettingsStore::captureChanged, this, &SettingsView::refreshCapture);
    refreshJira();
    refreshCapture();
}

void SettingsView::refreshJira() {
    const JiraSettings& j = m_settings.jira();
    ui::setFlag(m_badge, "active", j.connected);
    m_badge->setText((j.connected ? QStringLiteral("●  Conectado") : QStringLiteral("●  Desconectado")));
    if (m_selfEdit) return;
    m_url->setText(j.url);
    m_project->setText(j.project);
    m_email->setText(j.email);
    m_token->setText(j.token);
}

void SettingsView::refreshCapture() {
    if (m_selfEdit) return;
    const CaptureSettings& c = m_settings.capture();
    m_selfEdit = true;
    m_shortcut->setText(c.shortcut);
    m_format->setCurrentText(c.format);
    m_mode->setCurrentText(toString(c.mode));
    m_folder->setText(c.folder);
    m_selfEdit = false;
}

void SettingsView::testConnection() {
    m_badge->setEnabled(false);
    m_badge->setText(QStringLiteral("●  Probando…"));
    m_bugs.testConnection([this](const ConnectionResult& r) {
        m_badge->setEnabled(true);
        m_settings.updateJira([&](JiraSettings& j) { j.connected = r.ok; });
        if (r.ok) emit toast(QStringLiteral("Conectado a Jira como %1").arg(r.displayName), theme::Green);
        else emit toast(QStringLiteral("No se pudo conectar · %1").arg(r.error), theme::Red);
    });
}

} // namespace qaflow
