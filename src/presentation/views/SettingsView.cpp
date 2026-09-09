#include "SettingsView.h"

#include "application/BugReportService.h"
#include "application/SettingsStore.h"
#include "application/TestPublishService.h"
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

QString jiraHint(JiraAuth a) {
    switch (a) {
        case JiraAuth::CloudToken:
            return QCoreApplication::translate("SettingsView", "Los bugs se crean como issues del tipo elegido en el proyecto indicado. Jira Cloud: URL <b>https://empresa.atlassian.net</b>, el correo de la cuenta y un <i>API token</i> creado en id.atlassian.com.");
        case JiraAuth::ServerBasic:
            return QCoreApplication::translate("SettingsView", "Los bugs se crean como issues del tipo elegido en el proyecto indicado. Jira Server / Data Center con usuario y contraseña (Basic auth sobre la API v2): es la forma de conectar con Jira 8.13 y anteriores (por ejemplo <b>8.5.1</b>), que todavía no tienen tokens personales. La URL lleva el context path si lo hay: <b>https://jira.empresa.com</b> o <b>https://empresa.com/jira</b>. Tras varios intentos fallidos Jira exige resolver un CAPTCHA en el navegador antes de volver a aceptar la API.");
        case JiraAuth::ServerToken:
            return QCoreApplication::translate("SettingsView", "Los bugs se crean como issues del tipo elegido en el proyecto indicado. Jira Server / Data Center con un <i>token personal</i> (Perfil → Personal Access Tokens), disponible desde la versión 8.14.");
    }
    return {};
}

QString hintFor(const TrackerSettings& t) {
    switch (t.kind) {
        case TrackerKind::Jira: return jiraHint(t.jiraAuth);
        case TrackerKind::GitHub: return QCoreApplication::translate("SettingsView", "Los bugs se crean como issues del repositorio. URL de la API: <b>https://api.github.com</b> (o https://host/api/v3 en Enterprise). Token: PAT con permiso <i>issues</i>. La API no admite adjuntos.");
        case TrackerKind::GitLab: return QCoreApplication::translate("SettingsView", "Los bugs se crean como issues del proyecto y las capturas se suben como adjuntos. Token: PAT con ámbito <i>api</i>.");
        case TrackerKind::AzureDevOps: return QCoreApplication::translate("SettingsView", "Los bugs se crean como work items del tipo elegido. URL: <b>https://dev.azure.com/organización</b>. Token: PAT con permiso <i>Work Items (read &amp; write)</i>.");
    }
    return {};
}
} // namespace

SettingsView::SettingsView(SettingsStore& settings, BugReportService& bugs, IGlobalHotkey* hotkey, const QString& captureBackend,
                           TestPublishService* publish, QWidget* parent)
    : QWidget(parent), m_settings(settings), m_bugs(bugs), m_publish(publish), m_hotkey(hotkey), m_captureBackend(captureBackend) {
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
    m_theme->setObjectName(QStringLiteral("settingsTheme"));
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
    m_kind->setObjectName(QStringLiteral("settingsKind"));
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
    m_jiraAuth = new QComboBox;
    for (auto a : {JiraAuth::CloudToken, JiraAuth::ServerBasic, JiraAuth::ServerToken}) m_jiraAuth->addItem(label(a), static_cast<int>(a));
    m_jiraAuth->setToolTip(tr("Jira Cloud usa correo y API token; Jira Server, usuario y contraseña o un token personal (8.14+)"));
    m_authField = field(tr("Autenticación"), m_jiraAuth);
    connect(m_jiraAuth, &QComboBox::currentIndexChanged, this, [this](int) {
        if (m_selfEdit) return;
        const auto auth = static_cast<JiraAuth>(m_jiraAuth->currentData().toInt());
        m_selfEdit = true;
        m_settings.updateTracker([&](TrackerSettings& s) {
            // La URL de ejemplo cambia entre Cloud y Server: si no la habían tocado, se ajusta sola.
            const TrackerSettings prev = s;
            s.jiraAuth = auth;
            if (s.url.trimmed().isEmpty() || s.url == prev.defaultUrl()) s.url = s.defaultUrl();
            s.connected = false;
        });
        m_selfEdit = false;
        refreshTracker();
    });
    pg->addWidget(field(tr("Proyecto"), m_project, &m_projectLabel), 0, 0);
    pg->addWidget(m_authField, 0, 1);
    pg->setColumnStretch(0, 1);
    pg->setColumnStretch(1, 1);
    m_projectGrid = pg;
    tb->addWidget(prow);

    auto* credrow = new QWidget;
    auto* cgrid = new QGridLayout(credrow);
    cgrid->setContentsMargins(0, 0, 0, 0);
    cgrid->setHorizontalSpacing(12);
    m_user = new QLineEdit;
    m_userField = field(tr("Usuario"), m_user, &m_userLabel);
    m_token = new QLineEdit;
    m_token->setEchoMode(QLineEdit::Password);
    cgrid->addWidget(m_userField, 0, 0);
    cgrid->addWidget(field(tr("Token de API"), m_token, &m_tokenLabel), 0, 1);
    cgrid->setColumnStretch(0, 1);
    cgrid->setColumnStretch(1, 1);
    m_credGrid = cgrid;
    tb->addWidget(credrow);
    m_secretNote = ui::label(QString(), "muted-sm");
    m_secretNote->setWordWrap(true);
    tb->addWidget(m_secretNote);

    // Gestión de pruebas: Zephyr vive en la misma instancia de Jira y con las mismas credenciales.
    m_zephyrBlock = new QWidget;
    auto* zv = ui::vbox(m_zephyrBlock, 0, 10);
    zv->addWidget(ui::label(tr("GESTIÓN DE PRUEBAS"), "eyebrow"));
    m_zephyr = new QCheckBox(tr("Publicar los ciclos de plan en Zephyr"));
    m_zephyr->setObjectName(QStringLiteral("settingsZephyr"));
    m_zephyr->setToolTip(tr("Al terminar un ciclo, el informe puede crear en Zephyr el ciclo con sus ejecuciones, el veredicto de cada paso y las evidencias"));
    connect(m_zephyr, &QCheckBox::toggled, this, [this](bool on) {
        if (m_selfEdit) return;
        m_selfEdit = true;
        m_settings.updateTracker([&](TrackerSettings& s) { s.zephyr = on; });
        m_selfEdit = false;
        refreshZephyr();
    });
    zv->addWidget(m_zephyr);
    auto* zrow = new QWidget;
    auto* zg = new QGridLayout(zrow);
    zg->setContentsMargins(0, 0, 0, 0);
    zg->setHorizontalSpacing(12);
    m_zephyrVersion = new QLineEdit;
    m_zephyrVersion->setPlaceholderText(tr("Sin programar (Unscheduled)"));
    m_zephyrVersion->setToolTip(tr("Versión del proyecto a la que van los ciclos; vacío los deja sin programar"));
    connect(m_zephyrVersion, &QLineEdit::textEdited, this, [this](const QString& text) {
        m_selfEdit = true;
        m_settings.updateTracker([&](TrackerSettings& s) { s.zephyrVersion = text; });
        m_selfEdit = false;
    });
    m_zephyrTestType = new QLineEdit;
    m_zephyrTestType->setPlaceholderText(QStringLiteral("Test"));
    m_zephyrTestType->setToolTip(tr("Tipo de incidencia con el que se crean los Tests; vacío usa «Test», el que instala Zephyr"));
    connect(m_zephyrTestType, &QLineEdit::textEdited, this, [this](const QString& text) {
        m_selfEdit = true;
        m_settings.updateTracker([&](TrackerSettings& s) { s.zephyrTestType = text; });
        m_selfEdit = false;
    });
    m_zephyrTest = ui::button(tr("Probar Zephyr"), "outline");
    connect(m_zephyrTest, &QPushButton::clicked, this, &SettingsView::testZephyr);
    zg->addWidget(field(tr("Versión del proyecto"), m_zephyrVersion), 0, 0);
    zg->addWidget(field(tr("Tipo de incidencia del Test"), m_zephyrTestType), 0, 1);
    zg->addWidget(m_zephyrTest, 0, 2, Qt::AlignBottom);
    zg->setColumnStretch(0, 1);
    zg->setColumnStretch(1, 1);
    zv->addWidget(zrow);
    m_zephyrNote = ui::label(QString(), "muted-sm");
    m_zephyrNote->setWordWrap(true);
    zv->addWidget(m_zephyrNote);
    tb->addWidget(m_zephyrBlock);
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
    bind(m_user, [](TrackerSettings& s, const QString& t) { s.user = t; });
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
    m_globalShortcut = new QCheckBox(tr("Atajos globales: capturar y avanzar de paso aunque QAflow no tenga el foco"));
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

    // Atajos de la ejecución: avanzar de paso desde la aplicación que se está probando.
    QVBoxLayout* rb;
    auto* runCard = section(theme::Green, tr("Atajos de la ejecución"),
                            tr("Marcan el paso actual y pasan al siguiente sin traer QAflow al frente, para no cortar la prueba entre captura y captura. "
                               "Necesitan «atajos globales» activado; si el sistema rechaza alguno, sigue funcionando con la ventana en primer plano."),
                            nullptr, &rb);
    auto* rrow = new QWidget;
    auto* rg = new QGridLayout(rrow);
    rg->setContentsMargins(0, 0, 0, 0);
    rg->setHorizontalSpacing(12);
    const struct { QLineEdit** field; QString title; QString tip; } runFields[] = {
        {&m_stepPass, tr("Pasa y siguiente"), tr("Marca el paso actual como superado y avanza al siguiente")},
        {&m_stepFail, tr("Falla y siguiente"), tr("Marca el paso actual como fallido y avanza al siguiente")},
        {&m_stepBack, tr("Paso anterior"), tr("Deshace el último veredicto y vuelve a ese paso")}};
    int col = 0;
    for (const auto& f : runFields) {
        *f.field = new QLineEdit;
        (*f.field)->setProperty("role", QStringLiteral("mono"));
        (*f.field)->setToolTip(f.tip);
        rg->addWidget(field(f.title, *f.field), 0, col);
        rg->setColumnStretch(col++, 1);
    }
    rb->addWidget(rrow);
    v->addWidget(runCard);
    const struct { QLineEdit** field; void (*apply)(RunShortcuts&, const QString&); } runBindings[] = {
        {&m_stepPass, [](RunShortcuts& r, const QString& t) { r.passAndNext = t; }},
        {&m_stepFail, [](RunShortcuts& r, const QString& t) { r.failAndNext = t; }},
        {&m_stepBack, [](RunShortcuts& r, const QString& t) { r.previous = t; }}};
    for (const auto& b : runBindings) {
        QLineEdit* edit = *b.field;
        auto apply = b.apply;
        connect(edit, &QLineEdit::editingFinished, this, [this, edit, apply]() {
            m_selfEdit = true;
            m_settings.updateRunShortcuts([&](RunShortcuts& r) { apply(r, edit->text().trimmed()); });
            m_selfEdit = false;
        });
    }

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
    connect(&m_settings, &SettingsStore::runShortcutsChanged, this, &SettingsView::refreshRunShortcuts);
    refreshGeneral();
    refreshTracker();
    refreshCapture();
    refreshRunShortcuts();
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
    m_kindHint->setText(hintFor(t));
    m_projectLabel->setText(t.projectLabel().toUpper());
    m_project->setPlaceholderText(t.projectPlaceholder());
    m_url->setPlaceholderText(t.defaultUrl());
    // La autenticación sólo se elige en Jira; el usuario, sólo cuando ese modo lo pide. Las columnas
    // que quedan sin campo pierden su peso para que el de al lado ocupe la fila entera.
    const bool jira = t.kind == TrackerKind::Jira;
    m_authField->setVisible(jira);
    m_projectGrid->setColumnStretch(1, jira ? 1 : 0);
    m_userField->setVisible(t.needsUser());
    m_credGrid->setColumnStretch(0, t.needsUser() ? 1 : 0);
    m_userLabel->setText(t.userLabel().toUpper());
    m_user->setPlaceholderText(t.userPlaceholder());
    m_tokenLabel->setText(t.secretLabel().toUpper());
    m_token->setPlaceholderText(t.secretPlaceholder());
    const bool secure = m_settings.secretsAreSecure();
    m_secretNote->setText(secure ? tr("🔒 %1 · se guarda en: %2").arg(t.secretLabel(), m_settings.secretBackend())
                                 : tr("⚠ %1 · se guarda %2. Instala un llavero (secret-tool / libsecret en Linux) para cifrar el dato.").arg(t.secretLabel(), m_settings.secretBackend()));
    m_secretNote->setStyleSheet(QStringLiteral("font-size:11.5px;color:%1;").arg(secure ? theme::Muted : theme::AmberSoft));
    if (m_selfEdit) return;
    m_selfEdit = true;
    refreshZephyr();
    m_kind->setCurrentText(toString(t.kind));
    m_url->setText(t.url);
    m_project->setText(t.project);
    m_jiraAuth->setCurrentIndex(std::max(0, m_jiraAuth->findData(static_cast<int>(t.jiraAuth))));
    m_user->setText(t.user);
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

void SettingsView::refreshRunShortcuts() {
    if (m_selfEdit) return;
    const RunShortcuts& r = m_settings.runShortcuts();
    m_selfEdit = true;
    m_stepPass->setText(r.passAndNext);
    m_stepFail->setText(r.failAndNext);
    m_stepBack->setText(r.previous);
    m_selfEdit = false;
}

void SettingsView::refreshZephyr() {
    const TrackerSettings& t = m_settings.tracker();
    // Zephyr es un plugin de Jira: no tiene sentido ofrecerlo con otro gestor.
    m_zephyrBlock->setVisible(m_publish != nullptr && t.kind == TrackerKind::Jira);
    m_zephyrVersion->setEnabled(t.zephyr);
    m_zephyrTestType->setEnabled(t.zephyr);
    m_zephyrTest->setEnabled(t.zephyr);
    m_zephyrNote->setText(t.zephyr
                              ? tr("Cada caso publica su ejecución sobre el issue de tipo Test que tiene enlazado; el que aún no lo tenga lo estrena a partir del caso (título, precondiciones y pasos) y su clave se guarda en el campo «Test de Zephyr».")
                              : tr("Zephyr for Jira: los ciclos y sus ejecuciones se crean en la misma instancia con estas credenciales."));
    if (m_selfEdit) return;
    const bool wasEditing = m_selfEdit;
    m_selfEdit = true;
    m_zephyr->setChecked(t.zephyr);
    m_zephyrVersion->setText(t.zephyrVersion);
    m_zephyrTestType->setText(t.zephyrTestType);
    m_selfEdit = wasEditing;
}

void SettingsView::testZephyr() {
    if (!m_publish) return;
    m_zephyrTest->setEnabled(false);
    m_zephyrTest->setText(tr("Probando…"));
    m_publish->testConnection([this](const ConnectionResult& r) {
        m_zephyrTest->setEnabled(true);
        m_zephyrTest->setText(tr("Probar Zephyr"));
        if (r.ok) emit toast(tr("Zephyr responde · %1").arg(r.displayName), theme::Green);
        else emit toast(tr("No se pudo hablar con Zephyr · %1").arg(r.error), theme::Red);
    });
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
