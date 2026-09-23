#include "AiCaseGenerationDialog.h"

#include "presentation/theme/Theme.h"
#include "presentation/widgets/TextArea.h"
#include "presentation/widgets/Ui.h"

#include <QApplication>
#include <QClipboard>
#include <QLabel>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTextBrowser>

namespace qaflow {

namespace {
QWidget* field(const QString& title, QWidget* w) {
    auto* box = new QWidget;
    auto* v = ui::vbox(box, 0, 6);
    v->addWidget(ui::label(title.toUpper(), "eyebrow"));
    v->addWidget(w);
    return box;
}

QString caseHtml(const TestCase& c) {
    const auto esc = [](const QString& s) { return s.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")); };
    QString html = QStringLiteral("<h3>%1</h3>").arg(esc(c.title));
    QStringList meta{QObject::tr("Prioridad: %1").arg(qaflow::label(c.priority))};
    if (!c.component.isEmpty()) meta << QObject::tr("Componente: %1").arg(esc(c.component));
    if (!c.tags.isEmpty()) meta << QObject::tr("Etiquetas: %1").arg(esc(c.tags.join(QStringLiteral(", "))));
    html += QStringLiteral("<p>%1</p>").arg(meta.join(QStringLiteral(" · ")));
    if (!c.preconditions.isEmpty()) html += QStringLiteral("<p><b>%1</b><br>%2</p>").arg(QObject::tr("Precondiciones"), esc(c.preconditions));
    html += QStringLiteral("<table cellspacing='0' cellpadding='4' border='1' width='100%'><tr><th>#</th><th>%1</th><th>%2</th><th>%3</th></tr>")
                .arg(QObject::tr("Acción"), QObject::tr("Datos"), QObject::tr("Resultado esperado"));
    for (int i = 0; i < c.steps.size(); ++i) {
        const TestStep& s = c.steps[i];
        const QString expected = s.expected.isEmpty() ? QStringLiteral("<i style='color:%1'>%2</i>").arg(theme::AmberSoft, QObject::tr("falta"))
                                                      : esc(s.expected);
        html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td></tr>").arg(i + 1).arg(esc(s.action), esc(s.data), expected);
    }
    return html + QStringLiteral("</table>");
}
} // namespace

AiCaseGenerationDialog::AiCaseGenerationDialog(const ai::GenerationRequest& request, const QString& target,
                                               const QList<RequirementAttachment>& attachments, AttachmentLoader loader, QWidget* parent)
    : QDialog(parent), m_request(request), m_attachments(attachments), m_loader(std::move(loader)) {
    setObjectName(QStringLiteral("aiCaseGenerationDialog"));
    setWindowTitle(tr("Generar casos con IA"));
    setWindowIcon(ui::appIcon());
    setMinimumSize(760, 560);
    resize(960, 720);

    auto* v = ui::vbox(this, 18, 10);
    v->addWidget(ui::label(tr("Generar casos con IA"), "h2"));
    m_target = target;
    m_intro = ui::label(tr("Copia el prompt, pégalo en ChatGPT u otra IA y trae aquí su respuesta. Los casos que elijas se añaden %1 "
                           "en Borrador y con la etiqueta «%2».")
                            .arg(target, ai::generatedTag()),
                        "muted-sm");
    m_intro->setWordWrap(true);
    v->addWidget(m_intro);

    m_tabs = new QTabWidget;
    m_tabs->setObjectName(QStringLiteral("aiTabs"));
    v->addWidget(m_tabs, 1);

    // ---- 1 · El prompt: lo que sale del equipo, revisado antes de copiarlo.
    auto* promptPage = new QWidget;
    auto* pv = ui::vbox(promptPage, 12, 10);
    m_source = new TextArea(14);
    m_source->setObjectName(QStringLiteral("aiSource"));
    m_source->setTextSilently(request.source);
    // Es el texto principal de la pestaña: crece con el diálogo en vez de quedarse en sus 14 líneas.
    m_source->setMinimumHeight(m_source->height());
    m_source->setMaximumHeight(QWIDGETSIZE_MAX);
    m_source->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    pv->addWidget(field(tr("Texto del requerimiento"), m_source), 1);
    auto* hint = ui::label(tr("Es lo que compartirás con la IA: quita lo que no deba salir y añade lo que falte."), "muted-sm");
    hint->setWordWrap(true);
    pv->addWidget(hint);

    // Los adjuntos suelen ser la especificación de verdad: se añaden al texto a petición, para ver qué sale.
    if (!m_attachments.isEmpty()) {
        m_attachmentList = new QListWidget;
        m_attachmentList->setObjectName(QStringLiteral("aiAttachments"));
        m_attachmentList->setMaximumHeight(96);
        for (const auto& a : m_attachments) {
            auto* item = new QListWidgetItem(m_attachmentList);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Checked);
            item->setToolTip(a.label);
        }
        for (int i = 0; i < m_attachments.size(); ++i) setAttachmentState(i, QString());
        auto* attachRow = new QWidget;
        auto* ah = ui::hbox(attachRow, 0, 8);
        m_attachmentStatus = ui::label(m_loader ? tr("Marca los que sean parte de la especificación y añádelos al texto.")
                                                : tr("Sin conexión con GESREQ: abre los adjuntos y copia su texto a mano."),
                                       "muted-sm");
        m_attachmentStatus->setObjectName(QStringLiteral("aiAttachmentStatus"));
        m_attachmentStatus->setWordWrap(true);
        ah->addWidget(m_attachmentStatus, 1);
        m_addAttachments = ui::button(tr("Añadir al texto"), "outline");
        m_addAttachments->setObjectName(QStringLiteral("aiAddAttachments"));
        m_addAttachments->setEnabled(bool(m_loader));
        connect(m_addAttachments, &QPushButton::clicked, this, &AiCaseGenerationDialog::addAttachments);
        ah->addWidget(m_addAttachments, 0, Qt::AlignTop);
        auto* box = new QWidget;
        auto* bv = ui::vbox(box, 0, 6);
        bv->addWidget(m_attachmentList);
        bv->addWidget(attachRow);
        pv->addWidget(field(tr("Adjuntos del requerimiento"), box));
    }

    m_instructions = new TextArea(3);
    m_instructions->setObjectName(QStringLiteral("aiInstructions"));
    m_instructions->setPlaceholderText(tr("Opcional: «céntrate en las validaciones del formulario», «máximo 10 casos»…"));
    m_instructions->setTextSilently(request.instructions);
    pv->addWidget(field(tr("Indicaciones para la IA"), m_instructions));

    auto* copyRow = new QWidget;
    auto* ch = ui::hbox(copyRow, 0, 8);
    m_sourceSize = ui::label(QString(), "muted-sm");
    m_sourceSize->setObjectName(QStringLiteral("aiSourceSize"));
    ch->addWidget(m_sourceSize, 1);
    m_copied = ui::label(QString(), "muted-sm");
    m_copied->setObjectName(QStringLiteral("aiCopied"));
    ch->addWidget(m_copied);
    m_copy = ui::button(tr("Copiar prompt"), "primary");
    m_copy->setObjectName(QStringLiteral("aiCopyPrompt"));
    connect(m_copy, &QPushButton::clicked, this, &AiCaseGenerationDialog::copyPrompt);
    ch->addWidget(m_copy);
    m_generate = ui::button(QString(), "primary");
    m_generate->setObjectName(QStringLiteral("aiGenerate"));
    m_generate->setVisible(false);
    connect(m_generate, &QPushButton::clicked, this, &AiCaseGenerationDialog::generate);
    ch->addWidget(m_generate);
    m_destination = ui::label(QString(), "muted-sm");
    m_destination->setObjectName(QStringLiteral("aiDestination"));
    m_destination->setWordWrap(true);
    m_destination->setVisible(false);
    pv->addWidget(m_destination);
    pv->addWidget(copyRow);
    const auto updateSize = [this]() {
        const qsizetype size = prompt().size();
        const bool tooLong = size > kLongPrompt;
        m_sourceSize->setText(tooLong ? tr("%1 caracteres en el prompt: puede no caber en el chat de la IA; quita lo que no haga falta")
                                            .arg(size)
                                      : tr("%1 caracteres en el prompt").arg(size));
        m_sourceSize->setStyleSheet(tooLong ? QStringLiteral("color:%1;").arg(theme::AmberSoft) : QString());
        m_copied->clear();
    };
    connect(m_source, &QPlainTextEdit::textChanged, this, updateSize);
    connect(m_instructions, &QPlainTextEdit::textChanged, this, updateSize);
    updateSize();
    m_tabs->addTab(promptPage, tr("1 · Prompt"));

    // ---- 2 · La respuesta: se interpreta y se revisa caso a caso.
    auto* responsePage = new QWidget;
    auto* rv = ui::vbox(responsePage, 12, 10);
    m_response = new TextArea(7);
    m_response->setObjectName(QStringLiteral("aiResponse"));
    m_response->setPlaceholderText(tr("Pega aquí la respuesta completa de la IA (el JSON, aunque venga con texto alrededor)"));
    rv->addWidget(field(tr("Respuesta de la IA"), m_response));

    auto* readRow = new QWidget;
    auto* rh = ui::hbox(readRow, 0, 8);
    m_status = ui::label(QString(), "muted-sm");
    m_status->setObjectName(QStringLiteral("aiStatus"));
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rh->addWidget(m_status, 1);
    auto* read = ui::button(tr("Interpretar"), "outline");
    read->setObjectName(QStringLiteral("aiInterpret"));
    connect(read, &QPushButton::clicked, this, &AiCaseGenerationDialog::interpret);
    rh->addWidget(read, 0, Qt::AlignTop);
    rv->addWidget(readRow);

    auto* split = new QSplitter(Qt::Horizontal);
    m_list = new QListWidget;
    m_list->setObjectName(QStringLiteral("aiCases"));
    m_list->setSpacing(2);
    connect(m_list, &QListWidget::currentRowChanged, this, &AiCaseGenerationDialog::showCase);
    connect(m_list, &QListWidget::itemChanged, this, &AiCaseGenerationDialog::refreshAccept);
    m_preview = new QTextBrowser;
    m_preview->setObjectName(QStringLiteral("aiPreview"));
    m_preview->setOpenLinks(false);
    split->addWidget(m_list);
    split->addWidget(m_preview);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);
    rv->addWidget(split, 1);
    m_tabs->addTab(responsePage, tr("2 · Respuesta"));

    auto* buttons = new QWidget;
    auto* h = ui::hbox(buttons, 0, 8);
    h->addStretch(1);
    auto* cancel = ui::button(tr("Cancelar"), "outline");
    m_accept = ui::button(tr("Añadir casos"), "primary");
    m_accept->setObjectName(QStringLiteral("aiAccept"));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_accept, &QPushButton::clicked, this, &QDialog::accept);
    h->addWidget(cancel);
    h->addWidget(m_accept);
    v->addWidget(buttons);
    refreshAccept();
}

void AiCaseGenerationDialog::setAttachmentState(int row, const QString& state) {
    if (!m_attachmentList || row < 0 || row >= m_attachmentList->count()) return;
    const RequirementAttachment& a = m_attachments[row];
    const QString name = a.fileName.isEmpty() ? a.label : a.fileName;
    m_attachmentList->item(row)->setText(state.isEmpty() ? name : name + QStringLiteral(" · ") + state);
}

void AiCaseGenerationDialog::addAttachments() {
    if (!m_loader || !m_attachmentList) return;
    QList<int> pending;
    for (int i = 0; i < m_attachmentList->count(); ++i)
        if (m_attachmentList->item(i)->checkState() == Qt::Checked) pending << i;
    if (pending.isEmpty()) {
        m_attachmentStatus->setText(tr("Marca al menos un adjunto"));
        return;
    }
    m_addAttachments->setEnabled(false);
    m_attachmentList->setEnabled(false);
    readNextAttachment(pending);
}

void AiCaseGenerationDialog::readNextAttachment(QList<int> pending) {
    if (pending.isEmpty()) {
        m_addAttachments->setEnabled(true);
        m_attachmentList->setEnabled(true);
        return;
    }
    const int row = pending.takeFirst();
    setAttachmentState(row, tr("leyendo…"));
    m_attachmentStatus->setText(tr("Descargando y leyendo %1…").arg(m_attachments[row].fileName));
    QPointer<AiCaseGenerationDialog> self(this);
    m_loader(m_attachments[row], [self, row, pending](const DocumentText& text) {
        if (!self) return;
        if (text.ok) {
            const RequirementAttachment& a = self->m_attachments[row];
            QString source = self->m_source->toPlainText().trimmed();
            if (!source.isEmpty()) source += QStringLiteral("\n\n");
            source += QStringLiteral("=== Adjunto: %1 ===\n%2").arg(a.fileName.isEmpty() ? a.label : a.fileName, text.text);
            self->m_source->setPlainText(source);
            // Ya está en el texto: se desmarca para no añadirlo dos veces.
            self->m_attachmentList->item(row)->setCheckState(Qt::Unchecked);
            self->setAttachmentState(row, tr("añadido (%1 caracteres)").arg(text.text.size()));
            self->m_attachmentStatus->setText(tr("Revisa el texto añadido: quita índices, firmas o anexos que no sirvan para probar."));
        } else {
            self->setAttachmentState(row, tr("no se pudo leer"));
            self->m_attachmentList->item(row)->setToolTip(text.error);
            self->m_attachmentStatus->setText(text.error);
        }
        self->readNextAttachment(pending);
    });
}

void AiCaseGenerationDialog::setGenerator(const QString& destination, Generator generator) {
    m_generator = std::move(generator);
    const bool on = bool(m_generator);
    m_generate->setVisible(on);
    m_destination->setVisible(on);
    m_generate->setText(tr("Generar con IA"));
    m_destination->setText(tr("«Generar con IA» envía este prompt a %1 con tu clave. Revisa antes el texto: es todo lo que sale del equipo.")
                               .arg(destination));
    if (on)
        m_intro->setText(tr("Genera los casos con la IA configurada, o copia el prompt para usar otra. Los casos que elijas se añaden %1 "
                            "en Borrador y con la etiqueta «%2».")
                             .arg(m_target, ai::generatedTag()));
    // Con IA configurada, generar es lo principal; copiar queda para usar otra.
    ui::setRole(m_copy, on ? "outline" : "primary");
}

void AiCaseGenerationDialog::generate() {
    if (!m_generator || m_generating) return;
    m_generating = true;
    m_generate->setEnabled(false);
    m_generate->setText(tr("Generando…"));
    m_copied->setStyleSheet(QString());
    m_copied->setText(tr("Esperando la respuesta de la IA; puede tardar un par de minutos"));
    QPointer<AiCaseGenerationDialog> self(this);
    m_generator(prompt(), [self](const AiCompletion& r) {
        if (!self) return;
        self->m_generating = false;
        self->m_generate->setEnabled(true);
        self->m_generate->setText(tr("Generar con IA"));
        if (!r.ok) {
            self->m_copied->setStyleSheet(QStringLiteral("color:%1;").arg(theme::RedSoft));
            self->m_copied->setText(r.error);
            return;
        }
        self->m_copied->clear();
        self->m_response->setPlainText(r.text);
        self->m_tabs->setCurrentIndex(1);
        self->interpret();
        if (r.truncated) {
            // Lo que llegó se aprovecha, pero hay que saber que falta el final.
            self->m_status->setStyleSheet(QStringLiteral("color:%1;").arg(theme::AmberSoft));
            self->m_status->setText(tr("La respuesta llegó cortada por el tope de tokens (súbelo en Ajustes o pide menos casos) · %1")
                                        .arg(self->m_status->text()));
        }
    });
}

QString AiCaseGenerationDialog::prompt() const {
    ai::GenerationRequest request = m_request;
    request.source = m_source->toPlainText();
    request.instructions = m_instructions->toPlainText();
    return ai::buildPrompt(request);
}

void AiCaseGenerationDialog::copyPrompt() {
    QApplication::clipboard()->setText(prompt());
    m_copied->setText(tr("Copiado: pégalo en la IA y trae su respuesta"));
    m_tabs->setCurrentIndex(1);
    m_response->setFocus();
}

void AiCaseGenerationDialog::interpret() {
    m_parsed = ai::parseResponse(m_response->toPlainText());
    const QSignalBlocker block(m_list);
    m_list->clear();
    m_preview->clear();
    if (!m_parsed.ok) {
        m_status->setStyleSheet(QStringLiteral("color:%1;").arg(theme::RedSoft));
        m_status->setText(m_parsed.error);
        refreshAccept();
        return;
    }
    for (const auto& c : m_parsed.cases) {
        auto* item = new QListWidgetItem(tr("%1 · %2 · %3 paso(s)").arg(c.title, qaflow::label(c.priority)).arg(c.steps.size()), m_list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        item->setToolTip(c.title);
    }
    QString status = tr("%1 caso(s) leídos").arg(m_parsed.cases.size());
    if (!m_parsed.warnings.isEmpty()) status += tr(" · revisa: %1").arg(m_parsed.warnings.join(QStringLiteral(" · ")));
    m_status->setStyleSheet(m_parsed.warnings.isEmpty() ? QString() : QStringLiteral("color:%1;").arg(theme::AmberSoft));
    m_status->setText(status);
    m_list->setCurrentRow(0);
    showCase(0);
    refreshAccept();
}

QList<TestCase> AiCaseGenerationDialog::chosenCases() const {
    QList<TestCase> out;
    for (int i = 0; i < m_list->count() && i < m_parsed.cases.size(); ++i)
        if (m_list->item(i)->checkState() == Qt::Checked) out << m_parsed.cases[i];
    return out;
}

void AiCaseGenerationDialog::showCase(int row) {
    if (row < 0 || row >= m_parsed.cases.size()) { m_preview->clear(); return; }
    m_preview->setHtml(caseHtml(m_parsed.cases[row]));
}

void AiCaseGenerationDialog::refreshAccept() {
    const qsizetype n = chosenCases().size();
    m_accept->setEnabled(n > 0);
    m_accept->setText(n > 0 ? tr("Añadir %1 caso(s)").arg(n) : tr("Añadir casos"));
}

} // namespace qaflow
