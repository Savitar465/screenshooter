// AppImageInstaller y NsisInstaller (infrastructure/update/UpdateInstallers.h): qué paquete acepta cada uno,
// cómo reemplaza el AppImage y qué órdenes deja preparadas para después de cerrar QAflow.

#include "infrastructure/update/UpdateInstallers.h"

#include <QFile>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QtTest>

using namespace qaflow;

namespace {
void write(const QString& path, const QByteArray& content) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(content);
}

QByteArray read(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
} // namespace

class UpdateInstallersTest : public QObject {
    Q_OBJECT
private slots:
    void eachInstallerPicksItsOwnPackage() {
        AppImageInstaller appImage(QStringLiteral("/tmp/QAflow.AppImage"));
        const QString arch = QSysInfo::buildCpuArchitecture();
        QVERIFY(appImage.accepts(QStringLiteral("QAflow-1.6.0-%1.AppImage").arg(arch)));
        QVERIFY(!appImage.accepts(QStringLiteral("qaflow-1.6.0-Linux-%1.tar.gz").arg(arch)));
        QVERIFY(!appImage.accepts(QStringLiteral("QAflow-1.6.0-not%1-arch.AppImage.zsync").arg(arch)));

        NsisInstaller nsis(QStringLiteral("C:/Program Files/QAflow"));
        QVERIFY(nsis.accepts(QStringLiteral("qaflow-1.6.0-win64.exe")));
        QVERIFY(!nsis.accepts(QStringLiteral("qaflow-1.6.0-win64.zip")));
        QVERIFY(!nsis.accepts(QStringLiteral("qaflow_1.6.0_amd64.deb")));
    }

    void theAppImageIsReplacedInPlaceKeepingItExecutable() {
#ifdef Q_OS_WIN
        QSKIP("AppImage es de Linux: en Windows no hay permiso de ejecución que conservar");
#endif
        QTemporaryDir dir;
        const QString target = dir.filePath(QStringLiteral("QAflow-1.5.3-x86_64.AppImage"));
        const QString package = dir.filePath(QStringLiteral("descarga.AppImage"));
        write(target, "versión vieja");
        QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        write(package, "versión nueva");
        QFile::setPermissions(package, QFile::ReadOwner | QFile::WriteOwner);   // sin permiso de ejecución

        AppImageInstaller installer(target);
        QString error;
        QVERIFY2(installer.install(package, false, &error), qPrintable(error));
        QCOMPARE(read(target), QByteArray("versión nueva"));
        QVERIFY(QFileInfo(target).isExecutable());
        // Sólo queda el AppImage: la copia intermedia no se queda por ahí.
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).size(), 2);
    }

    void aReadOnlyFolderIsReportedAndNothingChanges() {
#ifdef Q_OS_WIN
        QSKIP("AppImage es de Linux: en Windows no hay permiso de ejecución que conservar");
#endif
        QTemporaryDir dir;
        const QString target = dir.filePath(QStringLiteral("QAflow.AppImage"));
        write(target, "versión vieja");
        QTemporaryDir downloads;
        const QString package = downloads.filePath(QStringLiteral("nueva.AppImage"));
        write(package, "versión nueva");
        QFile::setPermissions(dir.path(), QFile::ReadOwner | QFile::ExeOwner);
        AppImageInstaller installer(target);
        QString error;
        const bool ok = installer.install(package, false, &error);
        QFile::setPermissions(dir.path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        if (QFileInfo(dir.path()).isWritable() && ok) QSKIP("Se ejecuta como root: los permisos no limitan");
        QVERIFY(!ok);
        QVERIFY(!error.isEmpty());
        QCOMPARE(read(target), QByteArray("versión vieja"));
    }

    void theRelaunchWaitsForQAflowAndPassesPathsAsArguments() {
        const QString path = QStringLiteral("/home/ana/Mis apps/QAflow $(rm).AppImage");
        const QStringList command = AppImageInstaller::relaunchCommand(4242, path);
        QCOMPARE(command.first(), QStringLiteral("/bin/sh"));
        QVERIFY(command.contains(QStringLiteral("4242")));
        QCOMPARE(command.last(), path);           // como argumento: el intérprete nunca la interpreta
        QVERIFY(!command[2].contains(path));
    }

    void theWindowsScriptInstallsSilentlyInTheSameFolder() {
        const QString script = NsisInstaller::script(77, QStringLiteral("C:/Users/O'Brien/AppData/Local/QAflow/cache/updates/1.6.0/qaflow-1.6.0-win64.exe"),
                                                     QStringLiteral("C:/Program Files/QAflow"), true);
        QVERIFY(script.contains(QStringLiteral("Wait-Process -Id 77")));
        QVERIFY(script.contains(QStringLiteral("'C:\\Users\\O''Brien\\AppData")));            // comilla doblada
        QVERIFY(script.contains(QStringLiteral("'/S /D=C:\\Program Files\\QAflow'")));        // /D= sin comillas propias
        QVERIFY(script.contains(QStringLiteral("$true")));
        QVERIFY(script.contains(QStringLiteral("'C:\\Program Files\\QAflow\\bin\\qaflow.exe'")));
        QVERIFY(NsisInstaller::script(77, QStringLiteral("a.exe"), QStringLiteral("C:/QAflow"), false).contains(QStringLiteral("$false")));
    }
};

QTEST_GUILESS_MAIN(UpdateInstallersTest)
#include "test_update_installers.moc"
