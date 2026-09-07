#pragma once

#include "core/services/IScreenCapture.h"

#include <QObject>
#include <memory>

namespace qaflow {

class TestCaseStore;
class RunController;
class SettingsStore;

/// Orquesta una captura: pide la imagen, la guarda en la carpeta configurada
/// y la adjunta al caso seleccionado (asignada al paso en ejecución, si lo hay).
class EvidenceService : public QObject {
    Q_OBJECT
public:
    EvidenceService(std::shared_ptr<IScreenCapture> capture, TestCaseStore& cases, RunController& run,
                    SettingsStore& settings, QObject* parent = nullptr);

    void captureForSelectedCase();

signals:
    void captured(const QString& filePath);
    void failed(const QString& error);

private:
    std::shared_ptr<IScreenCapture> m_capture;
    TestCaseStore& m_cases;
    RunController& m_run;
    SettingsStore& m_settings;
    bool m_busy = false;
};

} // namespace qaflow
