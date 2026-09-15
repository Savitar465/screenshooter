#pragma once

#include "core/services/IQualityRecordWriter.h"

namespace qaflow {

/// El acta de control de calidad escrita como documento de Word con la maqueta del formulario R-213
/// («REVISIÓN CONTROL DE CALIDAD DE SOFTWARE»): cabecera con el membrete, «Generales», «Resumen
/// Observaciones», «Detalles de la revisión» y «Resultados».
///
/// No usa plantillas: arma el documento con `docx::write()`, así que lo único que hace falta para
/// cambiar el formulario es cambiar estas tablas.
class QualityRecordDocx : public IQualityRecordWriter {
public:
    QualityRecordWriteResult write(const QualityRecord& record, const QString& path) override;
};

} // namespace qaflow
