#pragma once

#include "core/services/IDocumentReader.h"

#include <QObject>
#include <QStringList>

namespace qaflow {

/// Lector de documentos del escritorio. Word (.docx) y OpenDocument se leen dentro de QAflow; el PDF, con
/// `pdftotext` (poppler-utils), y el Word antiguo (.doc, .rtf), con LibreOffice en modo sin ventana. Si el
/// programa no está instalado se dice cuál falta, para instalarlo o copiar el texto a mano.
///
/// El documento se escribe en un directorio temporal que se borra al terminar; nada queda en disco.
class DocumentReader : public QObject, public IDocumentReader {
    Q_OBJECT
public:
    explicit DocumentReader(QObject* parent = nullptr);

    void read(const QString& fileName, const QByteArray& data, std::function<void(const DocumentText&)> done) override;

    /// Programa con el que se leen los PDF; vacío si no se encuentra.
    static QString pdfToText();
    /// LibreOffice (soffice); vacío si no se encuentra.
    static QString office();
    /// Tiempo máximo de un programa externo antes de abandonar.
    int timeoutMs = 60000;

private:
    using Extract = std::function<DocumentText(const QString& outputDir)>;
    /// Escribe el documento en un directorio temporal, lanza `program` y, al terminar, `extract` lee su
    /// salida en ese directorio.
    void runTool(const QString& program, const QStringList& arguments, const QString& fileName, const QByteArray& data,
                 Extract extract, std::function<void(const DocumentText&)> done);
};

} // namespace qaflow
