#pragma once

#include "core/services/IScreenCapture.h"
#include "core/services/IScreenRecorder.h"

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <memory>

namespace qaflow {

class TestCaseStore;
class RunController;
class SettingsStore;

/// Orquesta las evidencias del caso seleccionado:
///  - captura de pantalla (con cuenta atrás opcional), guardada en la carpeta configurada y
///    adjuntada al caso (asignada al paso en ejecución, si lo hay);
///  - grabación de GIF (iniciar / detener), que se adjunta igual que una captura;
///  - ficheros existentes (logs, vídeos…) copiados a la carpeta de capturas;
///  - copia al portapapeles y sustitución de una imagen tras anotarla.
/// Es el único sitio que escribe o borra ficheros de evidencias: el store sólo conoce rutas.
class EvidenceService : public QObject {
    Q_OBJECT
public:
    EvidenceService(std::shared_ptr<IScreenCapture> capture, TestCaseStore& cases, RunController& run,
                    SettingsStore& settings, QObject* parent = nullptr);

    /// Grabador de pantalla (opcional: sin él `canRecord()` es false).
    void setRecorder(std::shared_ptr<IScreenRecorder> recorder);
    bool canRecord() const { return m_recorder != nullptr; }

    /// Captura según los ajustes. Con retardo, primero cuenta atrás (`countdown`); una segunda
    /// llamada durante la cuenta atrás la cancela.
    void captureForSelectedCase();
    bool isCountingDown() const { return m_countdownLeft > 0; }

    /// Inicia o detiene la grabación de GIF del caso seleccionado.
    void toggleRecording();
    bool isRecording() const;

    /// Copia ficheros existentes a la carpeta de capturas y los adjunta. Devuelve cuántos se adjuntaron.
    int attachFiles(const QStringList& paths);
    /// Sustituye la imagen de una evidencia (editor de anotaciones). El caso emite `caseChanged`.
    bool replaceImage(const QString& caseId, int shotId, const QImage& image);
    /// Copia la imagen de una evidencia al portapapeles.
    bool copyToClipboard(const QString& path);

signals:
    void captured(const QString& filePath);
    /// Fichero de evidencia añadido (captura, grabación o adjunto), con su caso y su id.
    void shotAdded(const QString& caseId, int shotId, const QString& filePath);
    void attached(const QStringList& filePaths);
    void failed(const QString& error);
    /// Segundos que faltan para capturar (3, 2, 1); 0 al terminar o cancelar.
    void countdown(int secondsLeft);
    void recordingChanged(bool recording);

private:
    QString targetCaseId() const;
    int targetStep(const QString& caseId) const;
    bool ensureFolder(QString* error) const;
    void grabNow();

    std::shared_ptr<IScreenCapture> m_capture;
    std::shared_ptr<IScreenRecorder> m_recorder;
    TestCaseStore& m_cases;
    RunController& m_run;
    SettingsStore& m_settings;
    QTimer m_countdown;
    int m_countdownLeft = 0;
    bool m_busy = false;
};

} // namespace qaflow
