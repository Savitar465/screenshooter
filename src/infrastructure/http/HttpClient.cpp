#include "HttpClient.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QRandomGenerator>
#include <QNetworkCookieJar>
#include <QNetworkReply>

namespace qaflow {

namespace {
constexpr int kTimeoutMs = 15000;

/// "POST /rest/zapi/latest/cycle": dice en el mensaje de error qué petición fue la que falló.
QString endpointOf(QNetworkReply* reply) {
    QByteArray verb;
    switch (reply->operation()) {
        case QNetworkAccessManager::GetOperation: verb = "GET"; break;
        case QNetworkAccessManager::PostOperation: verb = "POST"; break;
        case QNetworkAccessManager::PutOperation: verb = "PUT"; break;
        case QNetworkAccessManager::DeleteOperation: verb = "DELETE"; break;
        default: verb = reply->request().attribute(QNetworkRequest::CustomVerbAttribute).toByteArray(); break;
    }
    return QStringLiteral("%1 %2").arg(QString::fromLatin1(verb), reply->url().path());
}

bool isNetworkFailure(QNetworkReply::NetworkError e) {
    switch (e) {
        case QNetworkReply::ConnectionRefusedError:
        case QNetworkReply::RemoteHostClosedError:
        case QNetworkReply::HostNotFoundError:
        case QNetworkReply::TimeoutError:
        case QNetworkReply::OperationCanceledError:
        case QNetworkReply::SslHandshakeFailedError:
        case QNetworkReply::TemporaryNetworkFailureError:
        case QNetworkReply::NetworkSessionFailedError:
        case QNetworkReply::BackgroundRequestNotAllowedError:
        case QNetworkReply::TooManyRedirectsError:
        case QNetworkReply::InsecureRedirectError:
        case QNetworkReply::ProxyConnectionRefusedError:
        case QNetworkReply::ProxyConnectionClosedError:
        case QNetworkReply::ProxyNotFoundError:
        case QNetworkReply::ProxyTimeoutError:
        case QNetworkReply::UnknownNetworkError:
        case QNetworkReply::UnknownProxyError:
        case QNetworkReply::ServiceUnavailableError:
        case QNetworkReply::InternalServerError:
        case QNetworkReply::UnknownServerError:
            return true;
        default:
            return false;
    }
}
} // namespace

QNetworkRequest HttpClient::jsonRequest(const QString& url) {
    QNetworkRequest req{QUrl(url)};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    // Ni Jira ni los servicios de sus plugins prometen JSON en todas sus respuestas: ZAPI contesta
    // en texto plano a la subida de evidencias y el propio Jira devuelve HTML en varios errores.
    // Con un Accept que sólo admita JSON el servidor no encuentra representación que servir y
    // responde 406 en vez de hacer el trabajo, así que se pide JSON por delante y se acepta el resto.
    req.setRawHeader("Accept", "application/json, text/plain, */*");
    req.setTransferTimeout(kTimeoutMs);
    return req;
}

QString HttpClient::messageFrom(const QJsonDocument& json) {
    if (!json.isObject()) return {};
    const QJsonObject o = json.object();
    QStringList msgs;
    for (const auto& v : o[QStringLiteral("errorMessages")].toArray()) msgs << v.toString();                 // Jira
    const QJsonObject errs = o[QStringLiteral("errors")].toObject();                                             // Jira (por campo)
    for (auto it = errs.begin(); it != errs.end(); ++it) msgs << it.key() + QStringLiteral(": ") + it.value().toString();
    if (o[QStringLiteral("message")].isString()) msgs << o[QStringLiteral("message")].toString();              // GitHub, GitLab, Azure
    if (o[QStringLiteral("message")].isArray()) for (const auto& v : o[QStringLiteral("message")].toArray()) msgs << v.toString();
    if (o[QStringLiteral("error")].isString()) msgs << o[QStringLiteral("error")].toString();                  // GitLab
    if (o[QStringLiteral("errorDesc")].isString()) msgs << o[QStringLiteral("errorDesc")].toString();          // ZAPI
    // ZAPI no usa ninguna de esas formas: rechaza los datos inválidos con un 406 y un objeto plano
    // campo→motivo ({"date": "Formato de fecha no válido…"}). Sin leerlo, un fallo de validación
    // llegaba al usuario como el "Error transferring…" genérico de Qt, que no dice nada.
    if (msgs.isEmpty()) {
        static const QStringList ruido{QStringLiteral("errorDescHtml"), QStringLiteral("errorId"),
                                       QStringLiteral("status-code"), QStringLiteral("stack-trace")};
        for (auto it = o.begin(); it != o.end(); ++it)
            if (it.value().isString() && !ruido.contains(it.key())) msgs << it.value().toString();
    }
    return msgs.join(QStringLiteral("; "));
}

void HttpClient::finish(QNetworkReply* reply, Handler done) {
    connect(reply, &QNetworkReply::finished, this, [reply, done = std::move(done)]() {
        reply->deleteLater();
        Response r;
        r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        r.body = reply->readAll();
        r.json = QJsonDocument::fromJson(r.body);
        for (const auto& [name, value] : reply->rawHeaderPairs()) r.headers.insert(name.toLower(), value);
        r.ok = reply->error() == QNetworkReply::NoError;
        if (!r.ok) {
            r.retryable = isNetworkFailure(reply->error()) || r.status >= 500;
            const QString msg = messageFrom(r.json);
            const QString base = msg.isEmpty() ? reply->errorString() : msg;
            r.error = r.status ? QStringLiteral("HTTP %1 · %2").arg(r.status).arg(base) : base;
            r.error += QStringLiteral(" (%1)").arg(endpointOf(reply));
        }
        done(r);
    });
}

void HttpClient::get(const QNetworkRequest& req, Handler done) { finish(m_nam.get(req), std::move(done)); }

void HttpClient::postJson(const QNetworkRequest& req, const QJsonDocument& body, Handler done) {
    finish(m_nam.post(req, body.toJson(QJsonDocument::Compact)), std::move(done));
}

void HttpClient::sendCustom(const QByteArray& verb, const QNetworkRequest& req, const QByteArray& body, Handler done) {
    finish(m_nam.sendCustomRequest(req, verb, body), std::move(done));
}

void HttpClient::postMultipart(const QNetworkRequest& req, QHttpMultiPart* multi, Handler done) {
    QNetworkReply* reply = m_nam.post(req, multi);
    multi->setParent(reply);
    finish(reply, std::move(done));
}

void HttpClient::postFile(const QNetworkRequest& req, const QString& path, Handler done) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        Response r;
        r.error = QCoreApplication::translate("infrastructure", "No se pudo leer %1").arg(path);
        done(r);
        return;
    }
    QNetworkRequest bin = req;
    bin.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
    finish(m_nam.post(bin, f.readAll()), std::move(done));
}

QHttpMultiPart* HttpClient::multipartFile(const QString& path, const QByteArray& fieldName) {
    auto* file = new QFile(path);
    if (!file->open(QIODevice::ReadOnly)) { delete file; return nullptr; }
    auto* multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentTypeHeader, QMimeDatabase().mimeTypeForFile(path).name());
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"%1\"; filename=\"%2\"").arg(QString::fromLatin1(fieldName), QFileInfo(path).fileName()));
    part.setBodyDevice(file);
    file->setParent(multi);
    multi->append(part);
    return multi;
}

QHttpMultiPart* HttpClient::multipartForm(const QList<QPair<QString, QString>>& fields, const QString& filePath,
                                          const QByteArray& fileField) {
    auto* multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    for (const auto& [name, value] : fields) {
        QHttpPart part;
        // Sin juego de caracteres declarado, el servidor lee las tildes con el suyo por omisión.
        part.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/plain; charset=utf-8"));
        part.setHeader(QNetworkRequest::ContentDispositionHeader, QStringLiteral("form-data; name=\"%1\"").arg(name));
        part.setBody(value.toUtf8());
        multi->append(part);
    }
    if (filePath.isEmpty()) return multi;
    auto* file = new QFile(filePath);
    if (!file->open(QIODevice::ReadOnly)) { delete file; delete multi; return nullptr; }
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentTypeHeader, QMimeDatabase().mimeTypeForFile(filePath).name());
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"%1\"; filename=\"%2\"").arg(QString::fromLatin1(fileField), QFileInfo(filePath).fileName()));
    part.setBodyDevice(file);
    file->setParent(multi);
    multi->append(part);
    return multi;
}

HttpClient::FormData HttpClient::formData(const QList<QPair<QString, QString>>& fields, const QString& filePath,
                                          const QByteArray& fileField, int filePosition) {
    FormData out;
    // Un delimitador como el de un navegador: sin comillas y con caracteres que no aparecen en los datos.
    QByteArray boundary = "----QAflowFormBoundary";
    for (int i = 0; i < 16; ++i) boundary += QByteArray::number(QRandomGenerator::global()->bounded(16), 16);
    out.contentType = "multipart/form-data; boundary=" + boundary;

    QByteArray file;
    if (!filePath.isEmpty()) {
        QFile source(filePath);
        if (!source.open(QIODevice::ReadOnly)) return out;
        const QString name = QFileInfo(filePath).fileName();
        file = "--" + boundary + "\r\n";
        file += "Content-Disposition: form-data; name=\"" + fileField + "\"; filename=\"" + name.toUtf8() + "\"\r\n";
        file += "Content-Type: " + QMimeDatabase().mimeTypeForFile(filePath).name().toUtf8() + "\r\n\r\n";
        file += source.readAll();
        file += "\r\n";
    }

    const int at = file.isEmpty() ? -1 : (filePosition < 0 || filePosition > fields.size() ? int(fields.size()) : filePosition);
    for (int i = 0; i < fields.size(); ++i) {
        if (i == at) out.body += file;
        // Como el navegador: sólo `Content-Disposition`, y el valor en los bytes de la página (UTF-8).
        out.body += "--" + boundary + "\r\n";
        out.body += "Content-Disposition: form-data; name=\"" + fields[i].first.toUtf8() + "\"\r\n\r\n";
        out.body += fields[i].second.toUtf8();
        out.body += "\r\n";
    }
    if (at == fields.size()) out.body += file;
    out.body += "--" + boundary + "--\r\n";
    out.ok = true;
    return out;
}

void HttpClient::postFormData(const QNetworkRequest& req, const FormData& data, Handler done) {
    QNetworkRequest request = req;
    request.setHeader(QNetworkRequest::ContentTypeHeader, QString::fromLatin1(data.contentType));
    request.setHeader(QNetworkRequest::ContentLengthHeader, data.body.size());
    finish(m_nam.post(request, data.body), std::move(done));
}

QNetworkRequest HttpClient::pageRequest(const QString& url) {
    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("Accept", "text/html,application/xhtml+xml,*/*;q=0.8");
    req.setTransferTimeout(kTimeoutMs);
    return req;
}

void HttpClient::postForm(const QNetworkRequest& req, const QList<QPair<QString, QString>>& fields, Handler done) {
    // Cada nombre y valor, codificado entero: QUrlQuery deja el '+' tal cual y el servidor lo leería
    // como un espacio (una contraseña con '+' no entraría nunca).
    QByteArray body;
    for (const auto& [name, value] : fields) {
        if (!body.isEmpty()) body += '&';
        body += QUrl::toPercentEncoding(name) + '=' + QUrl::toPercentEncoding(value);
    }
    QNetworkRequest form = req;
    form.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    finish(m_nam.post(form, body), std::move(done));
}

void HttpClient::clearCookies() {
    // El gestor borra el almacén anterior, que es suyo.
    m_nam.setCookieJar(new QNetworkCookieJar(&m_nam));
}

QStringList HttpClient::existingFiles(const QStringList& paths) {
    QStringList out;
    for (const auto& p : paths) if (!p.isEmpty() && QFileInfo::exists(p)) out << p;
    return out;
}

} // namespace qaflow
