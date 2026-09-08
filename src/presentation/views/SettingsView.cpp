#include "SettingsView.h"

#include "application/BugReportService.h"
#include "application/SettingsStore.h"
#include "core/services/IGlobalHotkey.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/Ui.h"

#include <QCoreApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTimer>

namespace qaflow {

namespace {
QWidget* field(const QString& title, QWidget* w, QLabel** titleOut = nullptr) {
    auto* box = new QWidget;
    auto* v = ui::vbox(box, 0, 6);
    auto* l = ui::label(title.toUpper(), "eyebrow");
    v->addWidget(l);
    v->addWidget(w);
    if (titleOut) *titleOut = l;
    return box;
}
QFrame* section(const QString& accent, const QString& title, const QString& subtitle, QWidget* headerRight, QVBoxLayout** body, QLabel** subtitleOut = nullptr) {
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
    if (subtitleOut) *subtitleOut = sub;
    hh->addWidget(text, 1);
    if (headerRight) hh->addWidget(headerRight, 0, Qt::AlignTop);
    v->addWidget(head);
    h->addWidget(content, 1);
    *body = v;
    return card;
}

QString hintFor(TrackerKind k) {
    switch (k) {
        case TrackerKind::Jira: return QCoreApplication::translate("SettingsView", "Los bugs se crean como issues del tipo elegido en el proyecto indicado. Con correo → Jira Cloud (API token); sin correo → PAT de Jira Server/Data Center.");
        case TrackerKind::GitHub: return QCoreApplication::translate("SettingsView", "Los bugs se crean como issues del repositorio. URL de la API: <b>https://api.github.com</b> (o https://host/api/v3 en Enterprise). Token: PAT con permiso <i>issues</i>. La API no admite adjuntos.");
        case TrackerKind::GitLab: return QCoreApplication::translate("SettingsView", "Los bugs se crean como issues del proyecto y las capturas se suben como adjuntos. Token: PAT con ámbito <i>api</i>.");
        case TrackerKind::AzureDevOps: return QCoreApplication::translate("SettingsView", "Los bugs se crean como work items del tipo elegido. URL: <b>https://dev.azure.com/organización</b>. Token: PAT con permiso <i>Work Items (read &amp; write)</i>.");
    }
    return {};
}
} // namespace

SettingsView::SettingsView(SettingsStore& settings, BugReportService& bugs, IGlobalHotkey* hotkey, const QString& captureBackend, QWidget* parent)
    : QWidget(parent), m_settings(settings), m_bugs(bugs), m_hotkey(hotkey), m_captureBackend(captureBackend) {
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
    hv->addWidget(ui::label(tr("CONFIGURACIÓN"), "eyebrow"));
    hv->addWidget(ui::label(tr("Ajustes e integraciones"), "h1"));
    v->addWidget(head);

    // General: idioma, tema y bandeja
    QVBoxLayout* gb;
    auto* general = section(theme::Violet, QStringLiteral("General"),
                            tr("El idioma y el tema se aplican al instante reconstruyendo la ventana."), nullptr, &gb);
    auto* grow = new QWidget;
    auto* gg = new QGridLayout(grow);
    gg->setContentsMargins(0, 0, 0, 0);
    gg->setHorizontalSpacing(12);
    m_language = new QComboBox;
    m_language->addItem(tr("Como el sistema"), static_cast<int>(AppLanguage::System));
    m_language->addItem(QStringLiteral("Español"), static_cast<int>(AppLanguage::Spanish));
    m_language->addItem(QStringLiteral("English"), static_cast<int>(AppLanguage::English));
    m_theme = new QComboBox;
    m_theme->addItem(tr("Oscuro"), static_cast<int>(AppTheme::Dark));
    m_theme->addItem(tr("Claro"), static_cast<int>(AppTheme::Light));
    m_theme->addItem(tr("Como el sistema"), static_cast<int>(AppTheme::System));
    gg->addWidget(field(tr("Idioma"), m_language), 0, 0);
    gg->addWidget(field(tr("Tema"), m_theme), 0, 1);
    gg->setColumnStretch(0, 1);
    gg->setColumnStretch(1, 1);
    gb->addWidget(grow);
    m_closeToTray = new QCheckBox(tr("Al cerrar la ventana, seguir en la bandeja del sistema"));
    gb->addWidget(m_closeToTray);
    v->addWidget(general);
    connect(m_language, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_selfEdit) return;
        const auto l = static_cast<AppLanguage>(m_language->currentData().toInt());
        m_settings.updateApp([l](AppSettings& a) { a.language = l; });
    });
    connect(m_theme, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_selfEdit) return;
        const auto t = static_cast<AppTheme>(m_theme->currentData().toInt());
        m_settings.updateApp([t](AppSettings& a) { a.theme = t; });
    });
    connect(m_closeToTray, &QCheckBox::toggled, this, [this](bool on) {
        if (m_selfEdit) return;
        m_selfEdit = true;
        m_settings.updateApp([on](AppSettings& a) { a.closeToTray = on; });
        m_selfEdit = false;
    });

    // Gestor de incidencias
    m_badge = ui::button(QString(), "badge");
    m_badge->setToolTip(tr("Probar la conexión"));
    connect(m_badge, &QPushButton::clicked, this, &SettingsView::testConnection);
    QVBoxLayout* tb;
    auto* tracker = section(theme::Blue, tr("Gestor de incidencias"), QString(), m_badge, &tb, &m_kindHint);

    auto* krow = new QWidget;
    auto* kg = new QGridLayout(krow);
    kg->setContentsMargins(0, 0, 0, 0);
    kg->setHorizontalSpacing(12);
    m_kind = new QComboBox;
    for (auto k : {TrackerKind::Jira, TrackerKind::GitHub, TrackerKind::GitLab, TrackerKind::AzureDevOps}) m_kind->addItem(toString(k));
    connect(m_kind, &QComboBox::currentTextChanged, this, [this](const QString& t) {
        if (m_selfEdit) return;
        const TrackerKind kind = trackerKindFromString(t);
        m_selfEdit = true;
        m_settings.updateTracker([&](TrackerSettings& s) {
            const TrackerSettings defaults;
            // Al cambiar de gestor, URL y proyecto vuelven a un valor razonable si eran los de otro gestor.
            TrackerSettings prev = s;
            prev.kind = s.kind;
            if (s.url.trimmed().isEmpty() || s.url == prev.defaultUrl()) { s.kind = kind; s.url = s.defaultUrl(); }
            else s.kind = kind;
            if (s.project.trimmed().isEmpty() || s.project == defaults.project) s.project = QString();
            s.connected = false;
        });
        m_selfEdit = false;
        refreshTracker();
    });
    m_url = new QLineEdit;
    kg->addWidget(field(tr("Gestor"), m_kind), 0, 0);
    kg->addWidget(field(tr("URL"), m_url), 0, 1);
    kg->setColumnStretch(0, 1);
    kg->setColumnStretch(1, 2);
    tb->addWidget(krow);

    auto* prow = new QWidget;
    auto* pg = new QGridLayout(prow);
    pg->setContentsMargins(0, 0, 0, 0);
    pg->setHorizontalSpacing(12);
    m_project = new QLineEdit;
    m_project->setProperty("role", QStringLiteral("mono"));
    m_email = new QLineEdit;
    m_email->setPlaceholderText(tr("Sólo Jira Cloud · vacío para usar un PAT"));
    m_emailField = field(tr("Correo de la cuenta"), m_email);
    pg->addWidget(field(tr("Proyecto"), m_project, &m_projectLabel), 0, 0);
    pg->addWidget(m_emailField, 0, 1);
    pg->setColumnStretch(0, 1);
    pg->setColumnStretch(1, 1);
    tb->addWidget(prow);

    m_token = new QLineEdit;
    m_token->setEchoMode(QLineEdit::Password);
    tb->addWidget(field(tr("Token de API"), m_token));
    m_secretNote = ui::label(QString(), "muted-sm");
    m_secretNote->setWordWrap(true);
    tb->addWidget(m_secretNote);
    v->addWidget(tracker);

    auto bind = [this](QLineEdit* e, void (*apply)(TrackerSettings&, const QString&)) {
        connect(e, &QLineEdit::textEdited, this, [this, apply](const QString& t) {
            m_selfEdit = true;
            m_settings.updateTracker([&](TrackerSettings& s) { apply(s, t); s.connected = false; });
            m_selfEdit = false;
            ui::setFlag(m_badge, "active", false);
            m_badge->setText(tr("●  Desconectado"));
        });
    };
    bind(m_url, [](TrackerSettings& s, const QString& t) { s.url = t; });
    bind(m_project, [](TrackerSettings& s, const QString& t) { s.project = t; });
    bind(m_email, [](TrackerSettings& s, const QString& t) { s.email = t; });
    bind(m_token, [](TrackerSettings& s, const QString& t) { s.token = t; });

    // Capturas
    QVBoxLayout* cb;
    auto* cap = section(theme::Cyan, tr("Capturas de pantalla y grabaciones"),
                        tr("Se guardan localmente y se adjuntan al paso activo de la ejecución. Los ficheros existentes (logs, vídeos) se adjuntan con «Adjuntar archivo» o arrastrándolos a la ventana."), nullptr, &cb);
    auto* crow = new QWidget;
    auto* cg = new QGridLayout(crow);
    cg->setContentsMargins(0, 0, 0, 0);
    cg->setHorizontalSpacing(12);
    cg->setVerticalSpacing(12);
    m_shortcut = new QLineEdit;
    m_shortcut->setProperty("role", QStringLiteral("mono"));
    m_shortcut->setToolTip(tr("Atajo de captura. Con «atajo global» funciona aunque QAflow no tenga el foco"));
    m_recordShortcut = new QLineEdit;
    m_recordShortcut->setProperty("role", QStringLiteral("mono"));
    m_recordShortcut->setToolTip(tr("Inicia o detiene la grabación de GIF"));
    m_format = new QComboBox;
    m_format->addItems({QStringLiteral("PNG"), QStringLiteral("JPG"), QStringLiteral("WebP")});
    m_mode = new QComboBox;
    for (auto m : {CaptureMode::FullScreen, CaptureMode::ActiveWindow, CaptureMode::Region}) m_mode->addItem(label(m), static_cast<int>(m));
    m_mode->setToolTip(tr("Las grabaciones usan «Pantalla completa» o, en los demás modos, una región elegida con el ratón"));
    m_delay = new QComboBox;
    for (int secs : {0, 3, 5, 10}) m_delay->addItem(secs == 0 ? tr("Sin retardo") : tr("%1 s").arg(secs), secs);
    m_delay->setToolTip(tr("Cuenta atrás antes de capturar, para abrir menús o tooltips"));
    m_gifFps = new QSpinBox;
    m_gifFps->setRange(5, 20);
    m_gifFps->setSuffix(tr(" fps"));
    m_gifMaxSecs = new QSpinBox;
    m_gifMaxSecs->setRange(5, 120);
    m_gifMaxSecs->setSuffix(tr(" s"));
    m_gifMaxSecs->setToolTip(tr("La grabación se detiene sola al llegar a esta duración"));
    cg->addWidget(field(tr("Atajo de captura"), m_shortcut), 0, 0);
    cg->addWidget(field(tr("Formato"), m_format), 0, 1);
    cg->addWidget(field(tr("Modo"), m_mode), 0, 2);
    cg->addWidget(field(tr("Retardo"), m_delay), 0, 3);
    cg->addWidget(field(tr("Atajo de grabación"), m_recordShortcut), 1, 0);
    cg->addWidget(field(tr("GIF · fotogramas"), m_gifFps), 1, 1);
    cg->addWidget(field(tr("GIF · duración máxima"), m_gifMaxSecs), 1, 2);
    for (int i = 0; i < 4; ++i) cg->setColumnStretch(i, 1);
    cb->addWidget(crow);
    m_globalShortcut = new QCheckBox(tr("Atajo global: capturar aunque QAflow no tenga el foco"));
    cb->addWidget(m_globalShortcut);
    m_openEditor = new QCheckBox(tr("Abrir el editor de anotaciones después de cada captura"));
    cb->addWidget(m_openEditor);
    m_copyToClipboard = new QCheckBox(tr("Copiar la captura al portapapeles"));
    cb->addWidget(m_copyToClipboard);
    m_captureStatus = ui::label(QString(), "muted-sm");
    m_captureStatus->setWordWrap(true);
    m_captureStatus->setStyleSheet(QStringLiteral("font-size:11.5px;"));
    cb->addWidget(m_captureStatus);
    auto* frow = new QWidget;
    auto* fh = ui::hbox(frow, 0, 8);
    m_folder = new QLineEdit;
    m_folder->setProperty("role", QStringLiteral("mono"));
    fh->addWidget(m_folder, 1);
    auto* browse = ui::button(tr("Elegir…"), "outline");
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Carpeta de capturas"), m_folder->text());
        if (!dir.isEmpty()) m_settings.updateCapture([&](CaptureSettings& c) { c.folder = dir; });
    });
    fh->addWidget(browse);
    cb->addWidget(field(tr("Carpeta"), frow));
    v->addWidget(cap);

    connect(m_shortcut, &QLineEdit::editingFinished, this, [this]() {
        m_selfEdit = true;
        m_settings.updateCapture([&](CaptureSettings& c) { c.shortcut = m_shortcut->text().trimmed(); });
        m_selfEdit = false;
        refreshCaptureStatus();
    });
    connect(m_recordShortcut, &QLineEdit::editingFinished, this, [this]() {
        m_selfEdit = true;
        m_settings.updateCapture([&](CaptureSettings& c) { c.recordShortcut = m_recordShortcut->text().trimmed(); });
        m_selfEdit = false;
        refreshCaptureStatus();
    });
    connect(m_delay, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_selfEdit) return;
        const int secs = m_delay->currentData().toInt();
        m_selfEdit = true; m_settings.updateCapture([&](CaptureSettings& c) { c.delaySecs = secs; }); m_selfEdit = false;
    });
    connect(m_gifFps, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_selfEdit) return;
        m_selfEdit = true; m_settings.updateCapture([&](CaptureSettings& c) { c.gifFps = v; }); m_selfEdit = false;
    });
    connect(m_gifMaxSecs, &QSpinBox::valueChanged, this, [this](int v) {
        if (m_selfEdit) return;
        m_selfEdit = true; m_settings.updateCapture([&](CaptureSettings& c) { c.gifMaxSecs = v; }); m_selfEdit = false;
    });
    auto bindCheck = [this](QCheckBox* box, void (*apply)(CaptureSettings&, bool)) {
        connect(box, &QCheckBox::toggled, this, [this, apply](bool on) {
            if (m_selfEdit) return;
            m_selfEdit = true; m_settings.updateCapture([&](CaptureSettings& c) { apply(c, on); }); m_selfEdit = false;
            refreshCaptureStatus();
        });
    };
    bindCheck(m_globalShortcut, [](CaptureSettings& c, bool on) { c.globalShortcut = on; });
    bindCheck(m_openEditor, [](CaptureSettings& c, bool on) { c.openEditor = on; });
    bindCheck(m_copyToClipboard, [](CaptureSettings& c, bool on) { c.copyToClipboard = on; });
    connect(m_format, &QComboBox::currentTextChanged, this, [this](const QString& t) {
        if (m_selfEdit) return;
        m_selfEdit = true; m_settings.updateCapture([&](CaptureSettings& c) { c.format = t; }); m_selfEdit = false;
    });
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_selfEdit) return;
        const auto mode = static_cast<CaptureMode>(m_mode->currentData().toInt());
        m_selfEdit = true; m_settings.updateCapture([&](CaptureSettings& c) { c.mode = mode; }); m_selfEdit = false;
    });
    connect(m_folder, &QLineEdit::textEdited, this, [this](const QString& t) {
        m_selfEdit = true; m_settings.updateCapture([&](CaptureSettings& c) { c.folder = t; }); m_selfEdit = false;
    });

    connect(&m_settings, &SettingsStore::trackerChanged, this, &SettingsView::refreshTracker);
    connect(&m_settings, &SettingsStore::captureChanged, this, &SettingsView::refreshCapture);
    connect(&m_settings, &SettingsStore::appChanged, this, &SettingsView::refreshGeneral);
    refreshGeneral();
    refreshTracker();
    refreshCapture();
    refreshCaptureStatus();
}

/// Estado del atajo global y del método de captura. Se consulta con un pequeño retraso porque el
/// registro del atajo reacciona a la misma señal `captureChanged` que esta vista.
void SettingsView::refreshCaptureStatus() {
    QTimer::singleShot(0, this, [this]() {
        QStringList parts;
        if (!m_captureBackend.isEmpty()) parts << tr("Captura: %1").arg(m_captureBackend);
        if (!m_settings.capture().globalShortcut) parts << tr("Atajo sólo con QAflow en primer plano");
        else if (m_hotkey) parts << m_hotkey->status();
        m_captureStatus->setText(parts.join(QStringLiteral("  ·  ")));
    });
}

void SettingsView::refreshGeneral() {
    if (m_selfEdit) return;
    const AppSettings& a = m_settings.app();
    m_selfEdit = true;
    m_language->setCurrentIndex(std::max(0, m_language->findData(static_cast<int>(a.language))));
    m_theme->setCurrentIndex(std::max(0, m_theme->findData(static_cast<int>(a.theme))));
    m_closeToTray->setChecked(a.closeToTray);
    m_selfEdit = false;
}

void SettingsView::refreshTracker() {
    const TrackerSettings& t = m_settings.tracker();
    ui::setFlag(m_badge, "active", t.connected);
    m_badge->setText(t.connected ? tr("●  Conectado") : tr("●  Desconectado"));
    m_kindHint->setText(hintFor(t.kind));
    m_projectLabel->setText(t.projectLabel().toUpper());
    m_project->setPlaceholderText(t.projectPlaceholder());
    m_url->setPlaceholderText(t.defaultUrl());
    m_emailField->setVisible(t.kind == TrackerKind::Jira);
    m_token->setPlaceholderText(t.kind == TrackerKind::Jira ? tr("API token (Cloud) o PAT (Server)") : tr("Personal access token"));
    const bool secure = m_settings.secretsAreSecure();
    m_secretNote->setText(secure ? tr("🔒 Token guardado en: %1").arg(m_settings.secretBackend())
                                 : tr("⚠ Token guardado %1. Instala un llavero (secret-tool / libsecret en Linux) para cifrarlo.").arg(m_settings.secretBackend()));
    m_secretNote->setStyleSheet(QStringLiteral("font-size:11.5px;color:%1;").arg(secure ? theme::Muted : theme::AmberSoft));
    if (m_selfEdit) return;
    m_selfEdit = true;
    m_kind->setCurrentText(toString(t.kind));
    m_url->setText(t.url);
    m_project->setText(t.project);
    m_email->setText(t.email);
    m_token->setText(t.token);
    m_selfEdit = false;
}

void SettingsView::refreshCapture() {
    if (m_selfEdit) return;
    const CaptureSettings& c = m_settings.capture();
    m_selfEdit = true;
    m_shortcut->setText(c.shortcut);
    m_recordShortcut->setText(c.recordShortcut);
    m_format->setCurrentText(c.format);
    m_mode->setCurrentIndex(std::max(0, m_mode->findData(static_cast<int>(c.mode))));
    m_delay->setCurrentIndex(std::max(0, m_delay->findData(c.delaySecs)));
    m_gifFps->setValue(c.gifFps);
    m_gifMaxSecs->setValue(c.gifMaxSecs);
    m_globalShortcut->setChecked(c.globalShortcut);
    m_openEditor->setChecked(c.openEditor);
    m_copyToClipboard->setChecked(c.copyToClipboard);
    m_folder->setText(c.folder);
    m_selfEdit = false;
    refreshCaptureStatus();
}

void SettingsView::testConnection() {
    m_badge->setEnabled(false);
    m_badge->setText(tr("●  Probando…"));
    const QString name = toString(m_settings.tracker().kind);
    m_bugs.testConnection([this, name](const ConnectionResult& r) {
        m_badge->setEnabled(true);
        m_settings.updateTracker([&](TrackerSettings& s) { s.connected = r.ok; });
        if (r.ok) emit toast(tr("Conectado a %1 como %2").arg(name, r.displayName), theme::Green);
        else emit toast(tr("No se pudo conectar · %1").arg(r.error), theme::Red);
    });
}

} // namespace qaflow
