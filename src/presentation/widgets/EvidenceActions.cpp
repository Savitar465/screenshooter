#include "EvidenceActions.h"

#include "application/EvidenceService.h"
#include "application/TestCaseStore.h"
#include "presentation/widgets/AnnotationEditor.h"
#include "presentation/widgets/ImageViewer.h"
#include "presentation/widgets/ShotCard.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

namespace qaflow::evidence {

namespace {
const Screenshot* findShot(TestCaseStore& cases, const QString& caseId, int shotId) {
    const TestCase* c = cases.find(caseId);
    if (!c) return nullptr;
    for (const auto& s : c->shots) if (s.id == shotId) return &s;
    return nullptr;
}
} // namespace

bool annotate(QWidget* parent, TestCaseStore& cases, EvidenceService& service, const QString& caseId, int shotId) {
    const Screenshot* s = findShot(cases, caseId, shotId);
    if (!s || !s->isImage() || s->isAnimation()) return false;
    const QImage base(s->path);
    if (base.isNull()) return false;
    const QImage edited = AnnotationEditor::edit(base, parent);
    if (edited.isNull()) return false;
    return service.replaceImage(caseId, shotId, edited);
}

void copyToClipboard(EvidenceService& service, TestCaseStore& cases, const QString& caseId, int shotId) {
    if (const Screenshot* s = findShot(cases, caseId, shotId)) service.copyToClipboard(s->path);
}

void showInFolder(TestCaseStore& cases, const QString& caseId, int shotId) {
    const Screenshot* s = findShot(cases, caseId, shotId);
    if (!s) return;
    const QFileInfo info(s->path);
#if defined(Q_OS_WIN)
    if (info.exists() && QProcess::startDetached(QStringLiteral("explorer"), {QStringLiteral("/select,"), QDir::toNativeSeparators(info.absoluteFilePath())})) return;
#elif defined(Q_OS_MACOS)
    if (info.exists() && QProcess::startDetached(QStringLiteral("open"), {QStringLiteral("-R"), info.absoluteFilePath()})) return;
#endif
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
}

void openViewer(QWidget* parent, TestCaseStore& cases, EvidenceService& service, const QString& caseId, int shotId) {
    const TestCase* c = cases.find(caseId);
    if (!c || c->shots.isEmpty()) return;
    int index = 0;
    for (int i = 0; i < c->shots.size(); ++i) if (c->shots[i].id == shotId) index = i;
    auto* viewer = new ImageViewer(c->shots, index, parent);
    QPointer<ImageViewer> guard(viewer);
    TestCaseStore* store = &cases;
    EvidenceService* svc = &service;
    QObject::connect(viewer, &ImageViewer::copyRequested, viewer, [svc, store, caseId](int id) { copyToClipboard(*svc, *store, caseId, id); });
    QObject::connect(viewer, &ImageViewer::openFolderRequested, viewer, [store, caseId](int id) { showInFolder(*store, caseId, id); });
    QObject::connect(viewer, &ImageViewer::annotateRequested, viewer, [guard, store, svc, caseId](int id) {
        if (annotate(guard, *store, *svc, caseId, id) && guard) guard->reload();
    });
    viewer->show();
    viewer->raise();
    viewer->activateWindow();
}

QStringList pickFiles(QWidget* parent) {
    const QString start = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    return QFileDialog::getOpenFileNames(parent, QCoreApplication::translate("EvidenceActions", "Adjuntar archivos como evidencia"), start,
                                         QCoreApplication::translate("EvidenceActions", "Evidencias (*.png *.jpg *.jpeg *.webp *.gif *.bmp *.mp4 *.webm *.mkv *.mov *.log *.txt *.json *.har *.zip);;Todos los archivos (*)"));
}

void wireCard(ShotCard* card, QWidget* parent, TestCaseStore& cases, EvidenceService& service, const QString& caseId) {
    TestCaseStore* store = &cases;
    EvidenceService* svc = &service;
    QObject::connect(card, &ShotCard::openRequested, parent, [parent, store, svc, caseId](int id) { openViewer(parent, *store, *svc, caseId, id); });
    QObject::connect(card, &ShotCard::annotateRequested, parent, [parent, store, svc, caseId](int id) { annotate(parent, *store, *svc, caseId, id); });
    QObject::connect(card, &ShotCard::copyRequested, parent, [store, svc, caseId](int id) { copyToClipboard(*svc, *store, caseId, id); });
    QObject::connect(card, &ShotCard::openFolderRequested, parent, [store, caseId](int id) { showInFolder(*store, caseId, id); });
}

} // namespace qaflow::evidence
