#include "DocumentReader.h"

#include "infrastructure/documents/DocumentFormats.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>
#include <memory>

namespace qaflow {

namespace {

QString tr(const char* text) { return QCoreApplication::translate("infrastructure", text); }

DocumentText failure(const QString& error) { return DocumentText{false, {}, error}; }

/// El texto leído, o un error si el documento no tiene texto (un PDF escaneado, por ejemplo).
DocumentText fromText(const QString& text, const QString& fileName) {
    const QString clean = documents::tidy(text);
    if (clean.isEmpty()) return failure(tr("«%1» no tiene texto que leer (¿es una imagen escaneada?)").arg(fileName));
    return DocumentText{true, clean, {}};
}

QString readFile(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? documents::plainText(f.readAll()) : QString();
}

/// Nombre seguro para escribir el documento en el directorio temporal: sin rutas ni caracteres raros,
/// y con la extensión que el programa externo necesita para reconocerlo.
QString safeName(const QString& fileName, const QString& fallbackSuffix) {
    QString base = QFileInfo(fileName).fileName();
    base.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
    if (base.isEmpty() || base.startsWith(QLatin1Char('.'))) base.prepend(QStringLiteral("documento"));
    if (QFileInfo(base).suffix().isEmpty()) base += QLatin1Char('.') + fallbackSuffix;
    return base;
}

} // namespace

DocumentReader::DocumentReader(QObject* parent) : QObject(parent) {}

QString DocumentReader::pdfToText() {
    QString found = QStandardPaths::findExecutable(QStringLiteral("pdftotext"));
#ifdef Q_OS_WIN
    if (found.isEmpty())
        found = QStandardPaths::findExecutable(QStringLiteral("pdftotext"),
                                               {QCoreApplication::applicationDirPath(), QStringLiteral("C:/Program Files/poppler/Library/bin"),
                                                QStringLiteral("C:/Program Files/poppler/bin")});
#endif
    return found;
}

QString DocumentReader::office() {
    for (const auto* name : {"soffice", "libreoffice"})
        if (const QString found = QStandardPaths::findExecutable(QString::fromLatin1(name)); !found.isEmpty()) return found;
    return QStandardPaths::findExecutable(QStringLiteral("soffice"),
                                          {QStringLiteral("C:/Program Files/LibreOffice/program"),
                                           QStringLiteral("C:/Program Files (x86)/LibreOffice/program"),
                                           QStringLiteral("/Applications/LibreOffice.app/Contents/MacOS")});
}

void DocumentReader::read(const QString& fileName, const QByteArray& data, std::function<void(const DocumentText&)> done) {
    if (data.isEmpty()) { done(failure(tr("«%1» está vacío").arg(fileName))); return; }
    switch (documents::formatOf(fileName, data)) {
        case documents::Format::Docx: {
            QString error;
            const auto xml = documents::zipEntry(data, QStringLiteral("word/document.xml"), &error);
            done(xml ? fromText(documents::wordText(*xml), fileName) : failure(tr("No se pudo leer «%1»: %2").arg(fileName, error)));
            return;
        }
        case documents::Format::Odt: {
            QString error;
            const auto xml = documents::zipEntry(data, QStringLiteral("content.xml"), &error);
            done(xml ? fromText(documents::odtText(*xml), fileName) : failure(tr("No se pudo leer «%1»: %2").arg(fileName, error)));
            return;
        }
        case documents::Format::Text:
            done(fromText(documents::plainText(data), fileName));
            return;
        case documents::Format::Html:
            done(fromText(documents::htmlText(data), fileName));
            return;
        case documents::Format::Pdf: {
            const QString program = pdfToText();
            if (program.isEmpty()) {
                done(failure(tr("Para leer PDF hace falta pdftotext (paquete poppler-utils); instálalo o copia el texto de «%1» a mano")
                                 .arg(fileName)));
                return;
            }
            // -layout conserva las columnas de las tablas, que en una especificación suelen importar.
            runTool(program, {QStringLiteral("-layout"), QStringLiteral("-enc"), QStringLiteral("UTF-8"), QStringLiteral("{in}"),
                              QStringLiteral("{out}/texto.txt")},
                    safeName(fileName, QStringLiteral("pdf")), data,
                    [fileName](const QString& out) { return fromText(readFile(out + QStringLiteral("/texto.txt")), fileName); },
                    std::move(done));
            return;
        }
        case documents::Format::LegacyOffice: {
            const QString program = office();
            if (program.isEmpty()) {
                done(failure(tr("Para leer «%1» hace falta LibreOffice; instálalo o copia su texto a mano").arg(fileName)));
                return;
            }
            const QString name = safeName(fileName, QStringLiteral("doc"));
            // Un perfil propio en el directorio temporal: con LibreOffice abierto, el perfil del usuario
            // está bloqueado y la conversión no haría nada.
            runTool(program, {QStringLiteral("-env:UserInstallation={outUrl}/perfil"), QStringLiteral("--headless"),
                              QStringLiteral("--convert-to"), QStringLiteral("txt:Text (encoded):UTF8"), QStringLiteral("--outdir"),
                              QStringLiteral("{out}"), QStringLiteral("{in}")},
                    name, data,
                    [fileName, name](const QString& out) {
                        return fromText(readFile(out + QLatin1Char('/') + QFileInfo(name).completeBaseName() + QStringLiteral(".txt")), fileName);
                    },
                    std::move(done));
            return;
        }
        case documents::Format::Unknown:
            break;
    }
    done(failure(tr("QAflow no sabe leer el texto de «%1»: copia a mano lo que haga falta").arg(fileName)));
}

void DocumentReader::runTool(const QString& program, const QStringList& arguments, const QString& fileName,
                             const QByteArray& data, Extract extract, std::function<void(const DocumentText&)> done) {
    auto dir = std::make_shared<QTemporaryDir>();
    if (!dir->isValid()) { done(failure(tr("No se pudo crear un directorio temporal para leer el documento"))); return; }
    const QString input = dir->filePath(fileName);
    {
        QFile f(input);
        if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) {
            done(failure(tr("No se pudo escribir el documento en el directorio temporal")));
            return;
        }
    }
    const QString out = QDir::fromNativeSeparators(dir->path());
    QStringList args;
    const QString outUrl = QUrl::fromLocalFile(out).toString();
    for (QString a : arguments)
        args << a.replace(QStringLiteral("{in}"), input).replace(QStringLiteral("{outUrl}"), outUrl).replace(QStringLiteral("{out}"), out);

    auto* process = new QProcess(this);
    auto finished = std::make_shared<bool>(false);
    const auto finish = [process, finished, done](const DocumentText& result) {
        if (*finished) return;
        *finished = true;
        process->deleteLater();
        done(result);
    };
    connect(process, &QProcess::finished, this, [process, dir, out, extract, finish, program](int code, QProcess::ExitStatus status) {
        if (status != QProcess::NormalExit || code != 0) {
            const QString detail = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
            finish(failure(tr("%1 no pudo leer el documento%2").arg(QFileInfo(program).baseName(),
                                                                   detail.isEmpty() ? QString() : QStringLiteral(" · ") + detail)));
            return;
        }
        finish(extract(out));
    });
    connect(process, &QProcess::errorOccurred, this, [program, finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finish(failure(tr("No se pudo ejecutar %1").arg(program)));
    });
    QTimer::singleShot(timeoutMs, process, [process, finish]() {
        finish(failure(tr("La lectura del documento tardó demasiado y se canceló")));
        process->kill();
    });
    process->start(program, args);
}

} // namespace qaflow
