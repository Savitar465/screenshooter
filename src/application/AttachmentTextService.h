#pragma once

#include "core/models/Requirement.h"
#include "core/services/IDocumentReader.h"

#include <QHash>
#include <QObject>
#include <functional>
#include <memory>

namespace qaflow {

class RequirementSourceService;

/// El texto de los adjuntos de un requerimiento: los descarga de GESREQ con la sesión del usuario y saca
/// su texto para el prompt con el que se generan los casos. Sólo lee; nada se guarda en disco.
///
/// Lo ya leído se recuerda mientras dura la sesión (por dirección del adjunto): volver a abrir el
/// diálogo no descarga otra vez el mismo PDF.
class AttachmentTextService : public QObject {
    Q_OBJECT
public:
    AttachmentTextService(RequirementSourceService& source, std::shared_ptr<IDocumentReader> reader, QObject* parent = nullptr);

    void read(const RequirementAttachment& attachment, std::function<void(const DocumentText&)> done);

private:
    RequirementSourceService& m_source;
    std::shared_ptr<IDocumentReader> m_reader;
    QHash<QString, QString> m_cache;   // url → texto
};

} // namespace qaflow
