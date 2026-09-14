#pragma once

#include <QHash>
#include <QJsonDocument>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QObject>
#include <QPair>
#include <functional>

class QHttpMultiPart;
class QNetworkReply;

namespace qaflow {

/// Base de los clientes REST: peticiones JSON y multipart con callbacks, extracción de mensajes
/// de error y detección de fallos reintentables. No sabe de gestores de incidencias ni de Zephyr.
class HttpClient : public QObject {
    Q_OBJECT
public:
    explicit HttpClient(QObject* parent = nullptr) : QObject(parent) {}

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
    /// POST de formulario (application/x-www-form-urlencoded), como el que envía un navegador.
    void postForm(const QNetworkRequest& req, const QList<QPair<QString, QString>>& fields, Handler done);
    /// Olvida las cookies recibidas: la siguiente petición sale sin sesión.
    void clearCookies();

    static QNetworkRequest jsonRequest(const QString& url);
    /// Petición de una página web (HTML), con el mismo tiempo máximo que las de la API.
    static QNetworkRequest pageRequest(const QString& url);
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
