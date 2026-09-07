#pragma once

#include <QObject>
#include <QString>

namespace qaflow {

class TestCaseStore;

/// Importación y exportación de casos a ficheros (JSON, CSV, Markdown) para compartirlos.
/// La serialización vive en core (formats::); aquí sólo se lee/escribe el fichero y se
/// fusiona el resultado en el store.
class CaseTransferService : public QObject {
    Q_OBJECT
public:
    enum class Format { Json, Csv, Markdown };

    explicit CaseTransferService(TestCaseStore& cases, QObject* parent = nullptr);

    struct Result {
        bool ok = false;
        QString message;   // "12 casos exportados a …" / "3 añadidos · 2 actualizados" / error
        int added = 0;
        int updated = 0;
    };

    /// Exporta todos los casos (o sólo `ids`, si no está vacío). Las capturas no se incluyen.
    Result exportTo(const QString& path, Format format, const QStringList& ids = {}) const;
    /// Importa JSON o CSV según la extensión. Los casos con id existente se actualizan; el resto se añaden.
    Result importFrom(const QString& path);

    static Format formatForPath(const QString& path);
    static QString extension(Format f);

private:
    TestCaseStore& m_cases;
};

} // namespace qaflow
