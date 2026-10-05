#pragma once

#include "application/BugReportService.h"

#include <QDialog>
#include <QStringList>

class QLabel;
class QPushButton;

namespace qaflow {

class FlowLayout;
class TextArea;

/// Cierre de un bug en el gestor, con la nota que lo acompaña: un comentario (cómo se comprobó la
/// corrección) y la evidencia —capturas, vídeos o GIF— que se sube al issue antes de cerrarlo.
///
/// Es sólo el formulario: quien lo abre pide el cierre con `note()` al aceptar. Los adjuntos se
/// suben desde donde están, sin copiarlos ni tocarlos. También se pueden soltar sobre la ventana.
class BugCloseDialog : public QDialog {
    Q_OBJECT
public:
    /// `withNote` false (un gestor que no admite comentarios) deja sólo la confirmación.
    BugCloseDialog(const QString& key, bool withNote, QWidget* parent = nullptr);

    BugReportService::CloseNote note() const;
    /// Añade ficheros a los adjuntos (los repetidos y los que no existen se ignoran).
    void addAttachments(const QStringList& paths);

protected:
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    void refreshAttachments();

    bool m_withNote;
    TextArea* m_comment = nullptr;
    QLabel* m_attachmentsHeader = nullptr;
    FlowLayout* m_attachmentsRow = nullptr;
    QStringList m_attachments;
};

} // namespace qaflow
