#pragma once

#include "core/models/AiCaseDraft.h"
#include "core/models/Requirement.h"
#include "core/services/IAiClient.h"
#include "core/services/IDocumentReader.h"

#include <QDialog>
#include <functional>

class QLabel;
class QListWidget;
class QPushButton;
class QTabWidget;
class QTextBrowser;

namespace qaflow {

class TextArea;

/// Genera casos de prueba para un plan con una IA externa, sin conectarse a ella: QAflow arma el prompt
/// con el texto del requerimiento, el usuario lo lleva a ChatGPT (u otra) y pega aquí la respuesta.
///
/// Nada sale del equipo sin que el usuario lo vea: el texto del requerimiento se revisa y se edita antes
/// de copiar el prompt. Y nada entra en el proyecto sin revisarlo: la respuesta se lee en una lista de
/// casos con su vista previa, se eligen los que valen y sólo entonces se añaden, en Borrador y con la
/// etiqueta `ai::generatedTag()`, para terminar de pulirlos en la pantalla de casos.
class AiCaseGenerationDialog : public QDialog {
    Q_OBJECT
public:
    /// Lee el texto de un adjunto del requerimiento (lo descarga y lo extrae); el resultado llega por callback.
    using AttachmentLoader = std::function<void(const RequirementAttachment&, std::function<void(const DocumentText&)>)>;

    /// `request` trae el requerimiento y el texto propuesto; `target` dice adónde irán los casos
    /// ("al plan PL-0003 (GREQ 2025175)"); `attachments`, los adjuntos del requerimiento, que se pueden
    /// añadir al texto con `loader` (vacío si no hay conexión con GESREQ: entonces se copian a mano).
    AiCaseGenerationDialog(const ai::GenerationRequest& request, const QString& target,
                           const QList<RequirementAttachment>& attachments, AttachmentLoader loader, QWidget* parent = nullptr);

    /// Envía el prompt a la IA configurada y devuelve su respuesta.
    using Generator = std::function<void(const QString& prompt, std::function<void(const AiCompletion&)>)>;
    /// Con una IA configurada (`destination`: "Anthropic (Claude) · claude-sonnet-5"), el diálogo ofrece
    /// generar directamente además de copiar el prompt. La respuesta pasa por la misma revisión.
    void setGenerator(const QString& destination, Generator generator);
    /// Envía el prompt tal como está ahora al generador.
    void generate();

    /// Por encima de este tamaño el prompt suele no caber en el chat de una IA: se avisa.
    static constexpr qsizetype kLongPrompt = 100000;

    /// El prompt con el texto y las indicaciones tal como están ahora en el diálogo.
    QString prompt() const;
    /// Interpreta la respuesta pegada y rellena la lista de casos.
    void interpret();
    /// Los casos marcados, en el orden de la respuesta.
    QList<TestCase> chosenCases() const;

    /// Descarga los adjuntos marcados, uno tras otro, y añade su texto al del requerimiento.
    void addAttachments();

private:
    void readNextAttachment(QList<int> pending);
    void setAttachmentState(int row, const QString& state);
    void copyPrompt();
    void showCase(int row);
    void refreshAccept();

    ai::GenerationRequest m_request;
    ai::ParseResult m_parsed;
    QList<RequirementAttachment> m_attachments;
    AttachmentLoader m_loader;
    QListWidget* m_attachmentList = nullptr;
    QPushButton* m_addAttachments = nullptr;
    QLabel* m_attachmentStatus = nullptr;
    QTabWidget* m_tabs;
    TextArea* m_source;
    TextArea* m_instructions;
    QLabel* m_sourceSize;
    QLabel* m_copied;
    TextArea* m_response;
    QLabel* m_status;
    QListWidget* m_list;
    QTextBrowser* m_preview;
    QPushButton* m_accept;
    QPushButton* m_copy;
    QPushButton* m_generate;
    QLabel* m_destination;
    QLabel* m_intro;
    QString m_target;
    Generator m_generator;
    bool m_generating = false;
};

} // namespace qaflow
