#include "BugView.h"

#include "application/BugReportService.h"
#include "application/SettingsStore.h"
#include "application/TestCaseStore.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/FlowLayout.h"
#include "presentation/widgets/ShotCard.h"
#include "presentation/widgets/TextArea.h"
#include "presentation/widgets/Ui.h"

#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>

namespace qaflow {

namespace {
QWidget* field(const QString& title, QWidget* w, const QString& titleColor = QString()) {
    auto* box = new QWidget;
    auto* v = ui::vbox(box, 0, 6);
    auto* l = ui::label(title.toUpper(), "eyebrow");
    if (!titleColor.isEmpty()) l->setStyleSheet(QStringLiteral("color:%1;").arg(titleColor));
    v->addWidget(l);
    v->addWidget(w);
    return box;
}
} // namespace

BugView::BugView(TestCaseStore& cases, SettingsStore& settings, BugReportService& bugs, QWidget* parent)
    : QWidget(parent), m_cases(cases), m_settings(settings), m_bugs(bugs) {
    auto* root = ui::hbox(this, 0, 0);
    QWidget* content;
    QVBoxLayout* outer;
    auto* sa = ui::scrollArea(&content, &outer);
    outer->setContentsMargins(32, 28, 32, 28);
    auto* page = new QWidget;
    page->setMaximumWidth(820);
    auto* v = ui::vbox(page, 0, 18);
    outer->addWidget(page, 0, Qt::AlignTop);
    root->addWidget(sa, 1);

    auto* head = new QWidget;
    auto* hv = ui::vbox(head, 0, 0);
    m_eyebrow = ui::label(QString(), "eyebrow");
    m_eyebrow->setTextFormat(Qt::RichText);
    hv->addWidget(m_eyebrow);
    hv->addWidget(ui::label(QStringLiteral("Reportar bug"), "h1"));
    v->addWidget(head);

    auto* card = ui::card("card-lg");
    auto* ch = ui::hbox(card, 0, 0);
    ch->addWidget(ui::accentBar(theme::Red));
    auto* body = new QWidget;
    auto* bv = ui::vbox(body, 0, 16);
    bv->setContentsMargins(22, 22, 24, 22);

    m_title = new QLineEdit;
    m_title->setPlaceholderText(QStringLiteral("Resumen corto: qué falla y dónde"));
    m_title->setStyleSheet(QStringLiteral("font-size:14px;padding:9px 12px;"));
    connect(m_title, &QLineEdit::textChanged, this, [this]() { if (m_touched) ui::setFlag(m_title, "invalid", m_title->text().trimmed().isEmpty()); });
    bv->addWidget(field(QStringLiteral("Título"), m_title));

    auto* meta = new QWidget;
    auto* mg = new QGridLayout(meta);
    mg->setContentsMargins(0, 0, 0, 0);
    mg->setHorizontalSpacing(12);
    m_severity = new QComboBox;
    m_severity->addItems({QStringLiteral("Bloqueante"), QStringLiteral("Crítica"), QStringLiteral("Mayor"), QStringLiteral("Menor"), QStringLiteral("Trivial")});
    m_env = new QComboBox;
    m_env->addItems({QStringLiteral("Staging"), QStringLiteral("QA"), QStringLiteral("Producción")});
    m_linkedCase = new QLabel;
    m_linkedCase->setStyleSheet(QStringLiteral("background:#1c232d;border:1px solid #2a3441;border-radius:9px;padding:8px 10px;font-family:'Consolas','DejaVu Sans Mono',monospace;color:%1;").arg(theme::Muted));
    mg->addWidget(field(QStringLiteral("Severidad"), m_severity), 0, 0);
    mg->addWidget(field(QStringLiteral("Entorno"), m_env), 0, 1);
    mg->addWidget(field(QStringLiteral("Caso vinculado"), m_linkedCase), 0, 2);
    for (int i = 0; i < 3; ++i) mg->setColumnStretch(i, 1);
    bv->addWidget(meta);

    m_steps = new TextArea(5);
    m_steps->setProperty("role", QStringLiteral("mono"));
    bv->addWidget(field(QStringLiteral("Pasos para reproducir"), m_steps));

    auto* results = new QWidget;
    auto* rg = new QGridLayout(results);
    rg->setContentsMargins(0, 0, 0, 0);
    rg->setHorizontalSpacing(12);
    m_expected = new TextArea(3);
    m_actual = new TextArea(3);
    m_actual->setProperty("role", QStringLiteral("danger-soft"));
    m_actual->setPlaceholderText(QStringLiteral("Qué ocurrió realmente"));
    connect(m_actual, &TextArea::edited, this, [this]() { if (m_touched) ui::setFlag(m_actual, "invalid", m_actual->toPlainText().trimmed().isEmpty()); });
    rg->addWidget(field(QStringLiteral("Resultado esperado"), m_expected), 0, 0);
    rg->addWidget(field(QStringLiteral("Resultado actual"), m_actual, theme::RedSoft), 0, 1);
    rg->setColumnStretch(0, 1);
    rg->setColumnStretch(1, 1);
    bv->addWidget(results);

    auto* shotsBlock = new QWidget;
    auto* sv = ui::vbox(shotsBlock, 0, 8);
    auto* shHead = new QWidget;
    auto* shh = ui::hbox(shHead, 0, 8);
    m_shotsHeader = ui::label(QString(), "eyebrow");
    shh->addWidget(m_shotsHeader, 1);
    auto* capture = ui::button(QStringLiteral("+ Capturar pantalla"), "dashed");
    connect(capture, &QPushButton::clicked, this, &BugView::captureRequested);
    shh->addWidget(capture);
    sv->addWidget(shHead);
    auto* shots = new QWidget;
    m_shotsRow = new FlowLayout(shots, 0, 8, 8);
    sv->addWidget(shots);
    bv->addWidget(shotsBlock);

    ch->addWidget(body, 1);
    v->addWidget(card);

    auto* actions = new QWidget;
    auto* ah = ui::hbox(actions, 0, 10);
    ah->addStretch(1);
    auto* cancel = ui::button(QStringLiteral("Cancelar"), "ghost");
    connect(cancel, &QPushButton::clicked, this, &BugView::cancelled);
    m_submit = ui::button(QStringLiteral("Crear en Jira"), "primary");
    connect(m_submit, &QPushButton::clicked, this, &BugView::submit);
    ah->addWidget(cancel);
    ah->addWidget(m_submit);
    v->addWidget(actions);

    connect(&m_cases, &TestCaseStore::caseChanged, this, [this](const QString& id) { if (id == m_cases.selectedId()) refreshShots(); });
    connect(&m_settings, &SettingsStore::jiraChanged, this, [this]() {
        m_eyebrow->setText(QStringLiteral("NUEVO DEFECTO · DESTINO JIRA <span style=\"color:%1;font-family:monospace\">%2</span>").arg(theme::Blue, m_settings.jira().project));
    });
    emit m_settings.jiraChanged();
}

void BugView::loadDraft() {
    const BugReport d = m_bugs.draftFromCurrentContext();
    m_touched = false;
    ui::setFlag(m_title, "invalid", false);
    ui::setFlag(m_actual, "invalid", false);
    m_title->setText(d.title);
    m_severity->setCurrentText(d.severity);
    m_env->setCurrentText(d.environment);
    m_linkedCase->setText(d.linkedCaseId.isEmpty() ? QStringLiteral("—") : d.linkedCaseId);
    m_steps->setTextSilently(d.stepsToReproduce);
    m_expected->setTextSilently(d.expected);
    m_actual->setTextSilently(d.actual);
    refreshShots();
}

void BugView::refreshShots() {
    ui::clearLayout(m_shotsRow);
    const TestCase* c = m_cases.selected();
    const int n = c ? c->shots.size() : 0;
    m_shotsHeader->setText(QStringLiteral("ADJUNTOS · %1").arg(n));
    if (!c) return;
    const QString id = c->id;
    for (const auto& s : c->shots) {
        auto* card = new ShotCard(s, c->steps, ShotCard::Layout::Compact);
        connect(card, &ShotCard::removeRequested, this, [this, id](int shotId) { m_cases.removeShot(id, shotId); });
        m_shotsRow->addWidget(card);
    }
}

BugReport BugView::collect() const {
    BugReport b;
    b.title = m_title->text();
    b.severity = m_severity->currentText();
    b.environment = m_env->currentText();
    b.linkedCaseId = m_linkedCase->text() == QStringLiteral("—") ? QString() : m_linkedCase->text();
    b.stepsToReproduce = m_steps->toPlainText();
    b.expected = m_expected->toPlainText();
    b.actual = m_actual->toPlainText();
    if (const TestCase* c = m_cases.selected()) for (const auto& s : c->shots) b.attachmentPaths << s.path;
    return b;
}

void BugView::submit() {
    if (m_sending) return;
    const BugReport b = collect();
    if (!b.isValid()) {
        m_touched = true;
        ui::setFlag(m_title, "invalid", b.title.trimmed().isEmpty());
        ui::setFlag(m_actual, "invalid", b.actual.trimmed().isEmpty());
        emit toast(QStringLiteral("Completa título y resultado actual"), theme::Red);
        return;
    }
    if (!m_settings.jira().connected) {
        emit toast(QStringLiteral("Jira no está conectado · revisa Ajustes"), theme::Amber);
        return;
    }
    m_sending = true;
    m_submit->setEnabled(false);
    m_submit->setText(QStringLiteral("Creando…"));
    m_bugs.submit(b, [this](const IssueResult& r) {
        m_sending = false;
        m_submit->setEnabled(true);
        m_submit->setText(QStringLiteral("Crear en Jira"));
        if (!r.ok) { emit toast(QStringLiteral("Error al crear en Jira · %1").arg(r.error), theme::Red); return; }
        emit toast(QStringLiteral("%1 creado en Jira con %2 adjuntos").arg(r.key).arg(r.attachmentsUploaded), theme::Blue);
        emit submitted(r.key);
    });
}

} // namespace qaflow
