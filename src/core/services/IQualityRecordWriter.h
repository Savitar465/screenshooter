#pragma once

#include "core/models/QualityRecord.h"

#include <QString>

namespace qaflow {

struct QualityRecordWriteResult {
    bool ok = false;
    QString error;
};

/// Quien sabe convertir el acta de control de calidad en un documento en disco. La implementación
/// (Word/OOXML) vive en infrastructure; la aplicación sólo pide «escribe esto aquí».
class IQualityRecordWriter {
public:
    virtual ~IQualityRecordWriter() = default;
    /// Escribe el acta en `path`, sobrescribiéndolo si existe.
    virtual QualityRecordWriteResult write(const QualityRecord& record, const QString& path) = 0;
};

} // namespace qaflow
