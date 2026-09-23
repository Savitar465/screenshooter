#pragma once

#include <QByteArray>
#include <QString>
#include <functional>

namespace qaflow {

/// El texto de un documento, o por qué no se pudo sacar.
struct DocumentText {
    bool ok = false;
    QString text;
    QString error;
};

/// Saca el texto plano de un documento (PDF, Word, OpenDocument, texto…) para dárselo a una IA. El
/// formato se reconoce por la extensión de `fileName` y, si no la tiene, por el contenido. Asíncrona:
/// algunos formatos se leen con programas externos; el callback llega en el hilo principal.
class IDocumentReader {
public:
    virtual ~IDocumentReader() = default;
    virtual void read(const QString& fileName, const QByteArray& data, std::function<void(const DocumentText&)> done) = 0;
};

} // namespace qaflow
