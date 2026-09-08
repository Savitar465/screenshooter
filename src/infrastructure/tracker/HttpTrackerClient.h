#pragma once

#include "core/services/IIssueTracker.h"

#include <QHash>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QObject>
#include <functional>

class QHttpMultiPart;
class QNetworkReply;

namespace qaflow {

/// Base de los clientes REST de gestores de incidencias: peticiones JSON y multipart con
/// callbacks, extracción de mensajes de error y detección de fallos reintentables.
class HttpTrackerClient : public QObject, public IIssueTracker {
    Q_OBJECT
public:
    explicit HttpTrackerClient(QObject* parent = nullptr) : QObject(parent) {}

protected:
    struct Response {
        bool ok = false;
        int status = 0;          // código HTTP (0 si no hubo respuesta)
        QJsonDocument json;      // cuerpo parseado si era JSON
        QByteArray body;
        QString error;           // mensaje legible
        bool retryable = false;  // fallo de red o 5xx
        QHash<QByteArray, QByteArray> headers;   // cabeceras de la respuesta, con la clave en minúsculas

        QByteArray header(const char* name) const { return headers.value(QByteArray(name).toLower()); }
    };
    using Handler = std::function<void(const Response&)>;

    void get(const QNetworkRequest& req, Handler done);
    void postJson(const QNetworkRequest& req, const QJsonDocument& body, Handler done);
    /// PATCH / PUT con cuerpo arbitrario (el Content-Type va en la petición).
    void sendCustom(const QByteArray& verb, const QNetworkRequest& req, const QByteArray& body, Handler done);
    void postMultipart(const QNetworkRequest& req, QHttpMultiPart* multi, Handler done);
    /// Sube un fichero como POST binario (Content-Type application/octet-stream).
    void postFile(const QNetworkRequest& req, const QString& path, Handler done);

    static QNetworkRequest jsonRequest(const QString& url);
    /// Crea la parte multipart "file" de un fichero local (nullptr si no se puede abrir).
    static QHttpMultiPart* multipartFile(const QString& path, const QByteArray& fieldName = "file");
    /// Rutas que existen en disco, en el mismo orden.
    static QStringList existingFiles(const QStringList& paths);

private:
    void finish(QNetworkReply* reply, Handler done);
    static QString messageFrom(const QJsonDocument& json);

    QNetworkAccessManager m_nam;
};

} // namespace qaflow
