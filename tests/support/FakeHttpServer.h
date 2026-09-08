#pragma once

// Servidor HTTP mínimo en localhost para probar los clientes REST sin red: guarda cada petición
// recibida (método, ruta, cabeceras, cuerpo) y responde con lo que devuelva el manejador.

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QTcpServer>
#include <QTcpSocket>

#include <functional>

namespace qaflow::testing {

struct HttpRequest {
    QByteArray method;
    QByteArray path;       // con query string
    QHash<QByteArray, QByteArray> headers;   // claves en minúsculas
    QByteArray body;

    QByteArray header(const char* name) const { return headers.value(QByteArray(name).toLower()); }
};

struct HttpResponse {
    int status = 200;
    QByteArray body;
    QByteArray contentType = "application/json";
    /// Cabeceras adicionales (las de Seraph en Jira Server, por ejemplo).
    QHash<QByteArray, QByteArray> extraHeaders;

    static HttpResponse json(int status, const QByteArray& body) { return HttpResponse{status, body, "application/json", {}}; }
};

class FakeHttpServer : public QObject {
public:
    using Handler = std::function<HttpResponse(const HttpRequest&)>;

    explicit FakeHttpServer(QObject* parent = nullptr) : QObject(parent) {
        connect(&m_server, &QTcpServer::newConnection, this, &FakeHttpServer::onConnection);
        m_server.listen(QHostAddress::LocalHost, 0);
    }

    QString baseUrl() const { return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()); }
    /// Rutas que responde el servidor (se comparan con el path sin query string). Sin manejador → 404.
    void route(const QByteArray& method, const QByteArray& path, Handler h) { m_routes[method + " " + path] = std::move(h); }
    /// Manejador por defecto para cualquier petición sin ruta registrada.
    void fallback(Handler h) { m_fallback = std::move(h); }

    QList<HttpRequest> requests;
    void close() { m_server.close(); }

private:
    void onConnection() {
        while (QTcpSocket* sock = m_server.nextPendingConnection()) {
            auto* buffer = new QByteArray;
            connect(sock, &QTcpSocket::readyRead, this, [this, sock, buffer]() {
                buffer->append(sock->readAll());
                const int headerEnd = buffer->indexOf("\r\n\r\n");
                if (headerEnd < 0) return;
                const QByteArray head = buffer->left(headerEnd);
                const QList<QByteArray> lines = head.split('\n');
                HttpRequest req;
                const QList<QByteArray> first = lines.first().trimmed().split(' ');
                req.method = first.value(0);
                req.path = first.value(1);
                for (int i = 1; i < lines.size(); ++i) {
                    const int colon = lines[i].indexOf(':');
                    if (colon > 0) req.headers[lines[i].left(colon).trimmed().toLower()] = lines[i].mid(colon + 1).trimmed();
                }
                const int length = req.header("content-length").toInt();
                if (buffer->size() < headerEnd + 4 + length) return;   // cuerpo incompleto
                req.body = buffer->mid(headerEnd + 4, length);
                buffer->clear();   // antes de desconectar: `disconnected` libera el buffer
                requests.append(req);
                const HttpResponse res = dispatch(req);
                QByteArray out = "HTTP/1.1 " + QByteArray::number(res.status) + " " + (res.status < 400 ? "OK" : "Error") + "\r\n";
                out += "Content-Type: " + res.contentType + "\r\nContent-Length: " + QByteArray::number(res.body.size()) + "\r\nConnection: close\r\n";
                for (auto it = res.extraHeaders.cbegin(); it != res.extraHeaders.cend(); ++it) out += it.key() + ": " + it.value() + "\r\n";
                out += "\r\n";
                out += res.body;
                sock->write(out);
                sock->flush();
                sock->disconnectFromHost();
            });
            connect(sock, &QTcpSocket::disconnected, sock, [sock, buffer]() { delete buffer; sock->deleteLater(); });
        }
    }

    HttpResponse dispatch(const HttpRequest& req) {
        const QByteArray path = req.path.left(req.path.indexOf('?') < 0 ? req.path.size() : req.path.indexOf('?'));
        if (const auto it = m_routes.constFind(req.method + " " + path); it != m_routes.cend()) return it.value()(req);
        if (m_fallback) return m_fallback(req);
        return HttpResponse::json(404, R"({"message":"Not Found"})");
    }

    QTcpServer m_server;
    QHash<QByteArray, Handler> m_routes;
    Handler m_fallback;
};

} // namespace qaflow::testing
