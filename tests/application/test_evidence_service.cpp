// EvidenceService (application/EvidenceService.h) con una captura y un grabador falsos sobre la
// AppFixture. La evidencia es de la ejecución, así que el fixture arranca una: cubre la captura
// inmediata y con cuenta atrás (y su cancelación), adjuntar ficheros existentes, la grabación de
// GIF, la sustitución de la imagen tras anotar, la copia al portapapeles y que sin ejecución en
// curso no se captura nada.

#include "support/AppFixture.h"
#include "support/FakeScreenRecorder.h"

#include "application/EvidenceService.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QtTest>

using namespace qaflow;
using qaflow::testing::AppFixture;
using qaflow::testing::FakeScreenRecorder;

namespace {
class FakeScreenCapture : public IScreenCapture {
public:
    int calls = 0;
    CaptureMode lastMode = CaptureMode::FullScreen;
    void capture(CaptureMode mode, Callback done) override {
        ++calls;
        lastMode = mode;
        QImage img(8, 6, QImage::Format_ARGB32);
        img.fill(Qt::green);
        done(CaptureResult{true, img, {}});
    }
};

struct Fixture {
    AppFixture app;
    QTemporaryDir folder;
    std::shared_ptr<FakeScreenCapture> capture = std::make_shared<FakeScreenCapture>();
    std::shared_ptr<FakeScreenRecorder> recorder = std::make_shared<FakeScreenRecorder>();
    EvidenceService evidence{capture, app.store, app.run, app.settings};

    Fixture() {
        app.settings.updateCapture([&](CaptureSettings& c) { c.folder = folder.path(); c.delaySecs = 0; });
        // Sin ejecución no hay dónde guardar la evidencia: se ejecuta el caso seleccionado.
        app.run.start(app.store.selectedId());
    }
    const TestCase& selected() const { return *app.store.selected(); }
    QString writeFile(const QString& name, const QByteArray& content) {
        const QString path = folder.filePath(name);
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write(content);
        return path;
    }
};
} // namespace

class EvidenceServiceTest : public QObject {
    Q_OBJECT
private slots:
    void projectsShareTheCaptureSettingWithoutOverwritingEvidence() {
        Fixture a, b;
        b.app.settings.updateCapture([&](CaptureSettings& c) { c.folder = a.folder.path(); });
        a.evidence.setProjectId(QStringLiteral("project-a"));
        b.evidence.setProjectId(QStringLiteral("project-b"));
        a.evidence.captureForSelectedCase();
        const auto first = a.selected().shots.last();
        QFile original(first.path);
        QVERIFY(original.open(QIODevice::ReadOnly));
        const auto bytes = original.readAll(); original.close();
        b.evidence.captureForSelectedCase();
        const auto second = b.selected().shots.last();
        QCOMPARE(first.fileName, second.fileName);
        QVERIFY(first.path != second.path);
        QVERIFY(QFile::exists(second.path));
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), bytes);
        const QString log = a.writeFile(QStringLiteral("log.txt"), QByteArray("log"));
        QCOMPARE(a.evidence.attachFiles({log}), 1);
        QCOMPARE(b.evidence.attachFiles({log}), 1);
        QVERIFY(a.selected().shots.last().path != b.selected().shots.last().path);
    }

    // ---- Captura -----------------------------------------------------------------------

    // Sin ejecución en curso no se captura: la evidencia es de la ejecución.
    void capturingWithoutARunIsRefused() {
        Fixture f;
        f.app.run.abandon();
        QSignalSpy failed(&f.evidence, &EvidenceService::failed);
        QSignalSpy added(&f.evidence, &EvidenceService::shotAdded);
        f.evidence.captureForSelectedCase();
        QCOMPARE(added.count(), 0);
        QCOMPARE(failed.count(), 1);
        QVERIFY(f.selected().shots.isEmpty());
        // Y lo mismo con un fichero que se arrastra o se elige a mano.
        const QString log = f.writeFile(QStringLiteral("servidor.log"), "boom");
        QCOMPARE(f.evidence.attachFiles({log}), 0);
        QVERIFY(f.selected().shots.isEmpty());
    }

    // Lo capturado durante la ejecución es suyo: al archivarla queda sellado con su id.
    void evidenceIsSealedWithTheRunWhenItIsArchived() {
        Fixture f;
        f.evidence.captureForSelectedCase();
        QVERIFY(f.selected().shots.first().runId.isEmpty());   // la ejecución sigue en curso
        while (f.app.run.isRunning()) f.app.run.mark(StepResult::Pass);
        f.app.run.finish();
        const QString runId = f.app.history.runsForCase(f.selected().id).first().id;
        QVERIFY(!runId.isEmpty());
        QCOMPARE(f.selected().shots.first().runId, runId);
        QCOMPARE(f.selected().shotsOfRun(runId).size(), 1);
        QVERIFY(f.selected().shotsOfRun(QString()).isEmpty());   // ya no hay evidencia suelta
    }

    void captureSavesFileAndAttachesToSelectedCase() {
        Fixture f;
        QSignalSpy captured(&f.evidence, &EvidenceService::captured);
        QSignalSpy added(&f.evidence, &EvidenceService::shotAdded);
        f.evidence.captureForSelectedCase();
        QCOMPARE(captured.count(), 1);
        QCOMPARE(added.count(), 1);
        QCOMPARE(f.selected().shots.size(), 1);
        const Screenshot& s = f.selected().shots.first();
        QVERIFY(s.fileName.startsWith(QStringLiteral("cap_")));
        QVERIFY(s.fileName.endsWith(QStringLiteral(".png")));
        QVERIFY(s.isImage());
        QVERIFY(QFile::exists(s.path));
        QCOMPARE(static_cast<int>(f.capture->lastMode), static_cast<int>(CaptureMode::ActiveWindow));   // modo de Ajustes
        QCOMPARE(added.first().at(0).toString(), f.selected().id);
        QCOMPARE(added.first().at(1).toInt(), s.id);
    }

    void captureUsesConfiguredFormat() {
        Fixture f;
        f.app.settings.updateCapture([](CaptureSettings& c) { c.format = QStringLiteral("JPG"); c.mode = CaptureMode::Region; });
        f.evidence.captureForSelectedCase();
        QCOMPARE(f.selected().shots.first().extension(), QStringLiteral("jpg"));
        QCOMPARE(static_cast<int>(f.capture->lastMode), static_cast<int>(CaptureMode::Region));
    }

    void delayCountsDownBeforeCapturing() {
        Fixture f;
        f.app.settings.updateCapture([](CaptureSettings& c) { c.delaySecs = 2; });
        QSignalSpy countdown(&f.evidence, &EvidenceService::countdown);
        QSignalSpy captured(&f.evidence, &EvidenceService::captured);
        f.evidence.captureForSelectedCase();
        QVERIFY(f.evidence.isCountingDown());
        QCOMPARE(f.capture->calls, 0);
        QCOMPARE(countdown.count(), 1);
        QCOMPARE(countdown.first().at(0).toInt(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(captured.count(), 1, 5000);
        QVERIFY(!f.evidence.isCountingDown());
        QCOMPARE(countdown.count(), 3);   // 2, 1, 0
        QCOMPARE(countdown.last().at(0).toInt(), 0);
        QCOMPARE(f.capture->calls, 1);
    }

    void secondCallDuringCountdownCancelsIt() {
        Fixture f;
        f.app.settings.updateCapture([](CaptureSettings& c) { c.delaySecs = 5; });
        QSignalSpy failed(&f.evidence, &EvidenceService::failed);
        f.evidence.captureForSelectedCase();
        QVERIFY(f.evidence.isCountingDown());
        f.evidence.captureForSelectedCase();
        QVERIFY(!f.evidence.isCountingDown());
        QCOMPARE(failed.count(), 1);
        QTest::qWait(1200);
        QCOMPARE(f.capture->calls, 0);
        QVERIFY(f.selected().shots.isEmpty());
    }

    void copyToClipboardSettingCopiesTheCapture() {
        Fixture f;
        f.app.settings.updateCapture([](CaptureSettings& c) { c.copyToClipboard = true; });
        QGuiApplication::clipboard()->clear();
        f.evidence.captureForSelectedCase();
        const QImage img = QGuiApplication::clipboard()->image();
        QVERIFY(!img.isNull());
        QCOMPARE(img.size(), QSize(8, 6));
    }

    // ---- Adjuntos ----------------------------------------------------------------------

    void attachFilesCopiesIntoCaptureFolderAndKeepsOriginal() {
        Fixture f;
        QTemporaryDir elsewhere;
        const QString log = elsewhere.filePath(QStringLiteral("servidor.log"));
        { QFile file(log); file.open(QIODevice::WriteOnly); file.write("ERROR 500\n"); }
        QSignalSpy attached(&f.evidence, &EvidenceService::attached);
        QCOMPARE(f.evidence.attachFiles({log}), 1);
        QCOMPARE(attached.count(), 1);
        QCOMPARE(f.selected().shots.size(), 1);
        const Screenshot& s = f.selected().shots.first();
        QVERIFY(s.fileName.startsWith(QStringLiteral("adj_")));
        QVERIFY(s.fileName.endsWith(QStringLiteral("_servidor.log")));
        QVERIFY(!s.isImage());
        QCOMPARE(s.extension(), QStringLiteral("log"));
        QVERIFY(s.path.startsWith(f.folder.path()));
        QVERIFY(QFile::exists(s.path));
        QVERIFY(QFile::exists(log));   // el original no se toca
        QFile copy(s.path);
        copy.open(QIODevice::ReadOnly);
        QCOMPARE(copy.readAll(), QByteArray("ERROR 500\n"));
    }

    void attachMissingFileFailsButOthersSucceed() {
        Fixture f;
        const QString ok = f.writeFile(QStringLiteral("video.mp4"), "mp4");
        QSignalSpy failed(&f.evidence, &EvidenceService::failed);
        QCOMPARE(f.evidence.attachFiles({QStringLiteral("/no/existe.txt"), ok}), 1);
        QCOMPARE(failed.count(), 1);
        QCOMPARE(f.selected().shots.size(), 1);
    }

    void attachedFileIsAssignedToTheRunningStep() {
        Fixture f;
        const QString id = f.app.store.selectedId();
        f.app.run.start(id);
        f.app.run.mark(StepResult::Pass);   // ahora en el paso 2
        const QString ok = f.writeFile(QStringLiteral("trace.har"), "{}");
        f.evidence.attachFiles({ok});
        QCOMPARE(f.selected().shots.first().step, 2);
    }

    void releasedFilesAreDeletedFromDisk() {
        Fixture f;
        f.evidence.captureForSelectedCase();
        const QString path = f.selected().shots.first().path;
        f.app.store.removeShot(f.selected().id, f.selected().shots.first().id);
        QVERIFY(QFile::exists(path));   // aún se puede deshacer
        f.app.store.commitUndo();
        QVERIFY(!QFile::exists(path));
    }

    // ---- Grabación ---------------------------------------------------------------------

    void recordingIsUnavailableWithoutRecorder() {
        Fixture f;
        QVERIFY(!f.evidence.canRecord());
        QSignalSpy failed(&f.evidence, &EvidenceService::failed);
        f.evidence.toggleRecording();
        QCOMPARE(failed.count(), 1);
    }

    void toggleRecordingStartsAndStopsAndAttachesTheGif() {
        Fixture f;
        f.evidence.setRecorder(f.recorder);
        f.app.settings.updateCapture([](CaptureSettings& c) { c.mode = CaptureMode::ActiveWindow; c.gifFps = 12; c.gifMaxSecs = 20; });
        QSignalSpy changed(&f.evidence, &EvidenceService::recordingChanged);
        QSignalSpy captured(&f.evidence, &EvidenceService::captured);
        f.evidence.toggleRecording();
        QVERIFY(f.evidence.isRecording());
        QCOMPARE(changed.count(), 1);
        QVERIFY(changed.first().at(0).toBool());
        QCOMPARE(static_cast<int>(f.recorder->lastOptions.mode), static_cast<int>(CaptureMode::Region));   // «Ventana activa» graba una región
        QCOMPARE(f.recorder->lastOptions.fps, 12);
        QCOMPARE(f.recorder->lastOptions.maxSecs, 20);
        QVERIFY(f.recorder->lastOptions.outputPath.endsWith(QStringLiteral(".gif")));
        // Mientras graba no se captura.
        f.evidence.captureForSelectedCase();
        QCOMPARE(f.capture->calls, 0);

        f.evidence.toggleRecording();
        QVERIFY(!f.evidence.isRecording());
        QCOMPARE(changed.count(), 2);
        QCOMPARE(captured.count(), 1);
        QCOMPARE(f.selected().shots.size(), 1);
        const Screenshot& s = f.selected().shots.first();
        QVERIFY(s.fileName.startsWith(QStringLiteral("rec_")));
        QVERIFY(s.isAnimation());
        QVERIFY(QFile::exists(s.path));
    }

    void recorderStartFailureIsReported() {
        Fixture f;
        f.recorder->failOnStart = true;
        f.evidence.setRecorder(f.recorder);
        QSignalSpy failed(&f.evidence, &EvidenceService::failed);
        QSignalSpy changed(&f.evidence, &EvidenceService::recordingChanged);
        f.evidence.toggleRecording();
        QCOMPARE(failed.count(), 1);
        QVERIFY(!f.evidence.isRecording());
        QVERIFY(f.selected().shots.isEmpty());
        QVERIFY(!f.evidence.isRecording());
        // Una captura vuelve a ser posible.
        f.evidence.captureForSelectedCase();
        QCOMPARE(f.capture->calls, 1);
    }

    // ---- Anotaciones y portapapeles ------------------------------------------------------

    void replaceImageOverwritesTheFileAndNotifiesTheCase() {
        Fixture f;
        f.evidence.captureForSelectedCase();
        const QString id = f.selected().id;
        const Screenshot s = f.selected().shots.first();
        QSignalSpy changed(&f.app.store, &TestCaseStore::caseChanged);
        QImage red(8, 6, QImage::Format_ARGB32);
        red.fill(Qt::red);
        QVERIFY(f.evidence.replaceImage(id, s.id, red));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(changed.first().at(0).toString(), id);
        QCOMPARE(QImage(s.path).pixelColor(1, 1), QColor(Qt::red));
        QVERIFY(!f.evidence.replaceImage(id, 999, red));
        QVERIFY(!f.evidence.replaceImage(id, s.id, QImage()));
    }

    void copyToClipboardCopiesAnImageFile() {
        Fixture f;
        f.evidence.captureForSelectedCase();
        QGuiApplication::clipboard()->clear();
        QVERIFY(f.evidence.copyToClipboard(f.selected().shots.first().path));
        QCOMPARE(QGuiApplication::clipboard()->image().size(), QSize(8, 6));
        QSignalSpy failed(&f.evidence, &EvidenceService::failed);
        QVERIFY(!f.evidence.copyToClipboard(QStringLiteral("/no/existe.png")));
        QCOMPARE(failed.count(), 1);
    }
};

QTEST_MAIN(EvidenceServiceTest)
#include "test_evidence_service.moc"
