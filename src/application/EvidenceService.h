#pragma once

#include "core/models/TestCase.h"
#include "core/services/IScreenCapture.h"
#include "core/services/IScreenRecorder.h"

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <memory>

class QDir;

namespace qaflow {

class TestCaseStore;
class RunController;
class SettingsStore;

/// Orquesta las evidencias de la ejecución en curso (sin ejecución no se captura: la evidencia es
/// de la ejecución, no del caso):
///  - captura de pantalla (con cuenta atrás opcional), guardada en la carpeta configurada y
///    adjuntada a la ejecución, asignada al paso que se estaba ejecutando;
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
    /// Subcarpeta del proyecto dentro de la carpeta general de capturas.
    void setProjectId(const QString& id) { m_projectId = id; }
    QString captureFolder() const;
    bool canRecord() const { return m_recorder != nullptr; }

    /// Captura según los ajustes, para la ejecución en curso. Con retardo, primero cuenta atrás
    /// (`countdown`); una segunda llamada durante la cuenta atrás la cancela.
    void captureForSelectedCase();
    bool isCountingDown() const { return m_countdownLeft > 0; }
    /// Cancela la cuenta atrás en marcha (sea para la ejecución o para un parte de bug).
    void cancelCountdown();

    /// Inicia o detiene la grabación de GIF de la ejecución en curso.
    void toggleRecording();
    bool isRecording() const;
    bool isBusy() const { return m_busy; }

    /// Copia ficheros existentes a la carpeta de capturas y los adjunta. Devuelve cuántos se adjuntaron.
    int attachFiles(const QStringList& paths);
    /// Sustituye la imagen de una evidencia (editor de anotaciones). El caso emite `caseChanged`.
    bool replaceImage(const QString& caseId, int shotId, const QImage& image);
    /// Copia la imagen de una evidencia al portapapeles.
    bool copyToClipboard(const QString& path);

    // ---- Adjuntos de un parte de bug ----
    // Son del bug, no de la ejecución: viven en la subcarpeta `bugs/` y nunca pasan por el store,
    // así que quitarlos o anotarlos en el parte no toca la evidencia de la ejecución (ni al revés).

    /// Carpeta de los adjuntos de bugs, dentro de la de capturas del proyecto.
    QString bugFolder() const;
    /// Captura para el parte de bug (no hace falta ejecución en curso). Respeta la cuenta atrás;
    /// el resultado llega con `bugShotCaptured` o `failed`.
    void captureForBug();
    /// Copia ficheros a la carpeta de bugs y devuelve las evidencias creadas (con id 0: el parte
    /// numera las suyas). Sirve para copiar la última captura de la ejecución y para adjuntar.
    QList<Screenshot> copyForBug(const QStringList& paths);
    /// Sustituye la imagen de un adjunto de bug tras anotarla.
    bool replaceBugImage(const QString& path, const QImage& image);
    /// Borra adjuntos de un parte que no llegó a crearse (o que se quitaron de él). Sólo toca
    /// ficheros de la carpeta de bugs.
    void discardBugFiles(const QStringList& paths);

signals:
    void captured(const QString& filePath);
    /// Fichero de evidencia añadido (captura, grabación o adjunto), con su caso y su id.
    void shotAdded(const QString& caseId, int shotId, const QString& filePath);
    void attached(const QStringList& filePaths);
    void failed(const QString& error);
    /// Segundos que faltan para capturar (3, 2, 1); 0 al terminar o cancelar.
    void countdown(int secondsLeft);
    void recordingChanged(bool recording);
    /// Captura hecha con `captureForBug()` (id 0).
    void bugShotCaptured(const Screenshot& shot);

private:
    QString targetCaseId() const;
    int targetStep(const QString& caseId) const;
    bool ensureFolder(const QString& folder, QString* error) const;
    void grabNow();
    /// Nombre libre en `dir`: "bug_001.png" (`suffix` ".png") o "bug_001_log.txt" (`suffix` "log.txt").
    static QString freeBugName(const QDir& dir, const QString& suffix);

    std::shared_ptr<IScreenCapture> m_capture;
    std::shared_ptr<IScreenRecorder> m_recorder;
    TestCaseStore& m_cases;
    RunController& m_run;
    SettingsStore& m_settings;
    QTimer m_countdown;
    int m_countdownLeft = 0;
    bool m_busy = false;
    bool m_forBug = false;   // la captura en marcha (o en cuenta atrás) es para el parte de bug
    QString m_projectId;
};

} // namespace qaflow
