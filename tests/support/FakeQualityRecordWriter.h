#pragma once

// Escritor de actas que no escribe nada: guarda lo último que se le pidió y puede fallar a voluntad,
// para probar la capa de aplicación sin tocar el disco.

#include "core/services/IQualityRecordWriter.h"

#include <QString>

namespace qaflow::testing {

class FakeQualityRecordWriter : public IQualityRecordWriter {
public:
    QualityRecordWriteResult write(const QualityRecord& record, const QString& path) override {
        ++calls;
        lastRecord = record;
        lastPath = path;
        if (fail) return {false, QStringLiteral("no se pudo escribir")};
        return {true, {}};
    }

    int calls = 0;
    bool fail = false;
    QualityRecord lastRecord;
    QString lastPath;
};

} // namespace qaflow::testing
