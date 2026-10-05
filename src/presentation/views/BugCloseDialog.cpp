#include "BugCloseDialog.h"

#include "presentation/widgets/FlowLayout.h"
#include "presentation/widgets/TextArea.h"
#include "presentation/widgets/Thumbnail.h"
#include "presentation/widgets/Ui.h"

#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QStandardPaths>
#include <QUrl>

namespace qaflow {

namespace {
QStringList localFiles(const QMimeData* mime) {
    QStringList out;
    if (!mime || !mime->hasUrls()) return out;
    for (const QUrl& url : mime->urls())
        if (url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile()) out << url.toLocalFile();
    return out;
}
} // namespace

BugCloseDialog::BugCloseDialog(const QString& key, bool withNote, QWidget* parent) : QDialog(parent), m_withNote(withNote) {
    setObjectName(QStringLiteral("bugCloseDialog"));
    setWindowTitle(tr("Cerrar %1").arg(key));
    setModal(true);
    setMinimumWidth(withNote ? 520 : 360);
    setAcceptDrops(withNote);

    auto* v = ui::vbox(this, 18, 12);
    auto* intro = ui::label(tr("¿Cerrar %1 en el gestor? Se da por corregido.").arg(key));
    intro->setWordWrap(true);
    v->addWidget(intro);

    if (withNote) {
        v->addWidget(ui::label(tr("COMENTARIO"), "eyebrow"));
        m_comment = new TextArea(4);
        m_comment->setObjectName(QStringLiteral("bugCloseComment"));
        m_comment->setPlaceholderText(tr("Cómo se comprobó la corrección (opcional)"));
        m_comment->enableMarkupEditor(tr("Comentario de cierre de %1").arg(key));
        v->addWidget(m_comment);

        auto* head = new QWidget;
        auto* hh = ui::hbox(head, 0, 8);
        m_attachmentsHeader = ui::label(QString(), "eyebrow");
        hh->addWidget(m_attachmentsHeader, 1);
        auto* attach = ui::button(tr("+ Adjuntar archivo…"), "dashed");
        attach->setObjectName(QStringLiteral("bugCloseAttach"));
        attach->setToolTip(tr("Capturas, vídeos o GIF que prueban la corrección; también se pueden soltar aquí"));
        connect(attach, &QPushButton::clicked, this, [this]() {
            const QString start = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
            addAttachments(QFileDialog::getOpenFileNames(this, tr("Adjuntar al cierre"), start,
                                                         tr("Imágenes y vídeos (*.png *.jpg *.jpeg *.webp *.gif *.bmp *.mp4 *.webm *.mkv *.mov);;"
                                                            "Todos los archivos (*)")));
        });
        hh->addWidget(attach);
        v->addWidget(head);
        auto* row = new QWidget;
        m_attachmentsRow = new FlowLayout(row, 0, 8, 8);
        v->addWidget(row);
        refreshAttachments();
    }

    auto* actions = new QWidget;
    auto* ah = ui::hbox(actions, 0, 10);
    ah->addStretch(1);
    auto* cancel = ui::button(tr("Cancelar"), "ghost");
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    ah->addWidget(cancel);
    auto* accept = ui::button(tr("Cerrar %1").arg(key), "primary");
    accept->setObjectName(QStringLiteral("bugDetailCloseAccept"));
    accept->setDefault(true);
    connect(accept, &QPushButton::clicked, this, &QDialog::accept);
    ah->addWidget(accept);
    v->addWidget(actions);
}

BugReportService::CloseNote BugCloseDialog::note() const {
    BugReportService::CloseNote n;
    if (!m_withNote) return n;
    n.comment = m_comment->toPlainText().trimmed();
    n.attachments = m_attachments;
    return n;
}

void BugCloseDialog::addAttachments(const QStringList& paths) {
    if (!m_withNote) return;
    for (const QString& p : paths) {
        const QString path = QFileInfo(p).absoluteFilePath();
        if (QFileInfo(path).isFile() && !m_attachments.contains(path)) m_attachments << path;
    }
    refreshAttachments();
}

void BugCloseDialog::refreshAttachments() {
    ui::clearLayout(m_attachmentsRow);
    m_attachmentsHeader->setText(tr("ADJUNTOS · %1").arg(m_attachments.size()));
    for (int i = 0; i < m_attachments.size(); ++i) {
        const QString path = m_attachments[i];
        auto* card = ui::card("card-flat");
        card->setFixedWidth(140);
        auto* cv = ui::vbox(card, 0, 0);
        auto* thumb = new Thumbnail(path, 0, i + 1);
        thumb->setWidthHint(138);
        thumb->setToolTip(tr("Abrir con la aplicación del sistema"));
        // Se abre con el visor del sistema: los adjuntos son los ficheros originales y no se anotan aquí.
        connect(thumb, &Thumbnail::clicked, this, [path]() { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
        cv->addWidget(thumb);
        auto* bottom = new QWidget;
        auto* bh = ui::hbox(bottom, 0, 4);
        bh->setContentsMargins(8, 5, 4, 5);
        auto* name = ui::label(QFileInfo(path).fileName(), "mono-muted");
        name->setToolTip(path);
        name->setStyleSheet(QStringLiteral("font-size:11px;font-weight:400;"));
        bh->addWidget(name, 1);
        auto* remove = ui::button(QStringLiteral("×"), "icon");
        remove->setToolTip(tr("Quitar"));
        connect(remove, &QPushButton::clicked, this, [this, path]() {
            m_attachments.removeAll(path);
            refreshAttachments();
        });
        bh->addWidget(remove);
        cv->addWidget(bottom);
        m_attachmentsRow->addWidget(card);
    }
}

void BugCloseDialog::dragEnterEvent(QDragEnterEvent* e) {
    if (!localFiles(e->mimeData()).isEmpty()) e->acceptProposedAction();
}

void BugCloseDialog::dropEvent(QDropEvent* e) {
    const QStringList files = localFiles(e->mimeData());
    if (files.isEmpty()) return;
    addAttachments(files);
    e->acceptProposedAction();
}

} // namespace qaflow
