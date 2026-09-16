#pragma once

#include "core/services/IRequirementSource.h"
#include "infrastructure/http/HttpClient.h"
#include "infrastructure/requirements/GesreqParser.h"

#include <QList>
#include <QPair>

namespace qaflow {

/// Cliente de GESREQ, una aplicación Struts/JSP con las páginas generadas en el servidor: inicia sesión
/// con su formulario de login, conserva la cookie de sesión y lee la bandeja de control de calidad, la
/// ficha de cada requerimiento y el catálogo de sistemas. El marcado se interpreta en `gesreq::`
/// (GesreqParser); aquí están sólo la sesión y la recuperación de errores. Nunca pide páginas que
/// registren algo en el sistema.
///
/// GESREQ no avisa de una sesión caducada con un código HTTP: responde 200 con el formulario de login
/// (en la bandeja) o con una ficha vacía (en el detalle). El cliente lo reconoce por el contenido, inicia
/// otra sesión y repite la petición una sola vez; si aun así no hay datos, lo dice con su motivo en vez
/// de devolver una bandeja vacía o un requerimiento en blanco.
class GesreqClient : public HttpClient, public IRequirementSource {
    Q_OBJECT
public:
    explicit GesreqClient(QObject* parent = nullptr) : HttpClient(parent) {}

    void testConnection(const RequirementSourceSettings& s, std::function<void(const ConnectionResult&)> done) override;
    void fetchInbox(const RequirementSourceSettings& s, std::function<void(const RequirementInboxResult&)> done) override;
    void fetchDetail(const RequirementSourceSettings& s, const QString& id,
                     std::function<void(const RequirementDetailResult&)> done) override;
    void fetchSystems(const RequirementSourceSettings& s, std::function<void(const RequirementSystemsResult&)> done) override;

    bool canRegisterResult() const override { return true; }
    /// Las reglas del formulario del control de calidad, comprobadas sin tocar la red: el acta que
    /// GESREQ exige y la coherencia entre el resultado y el resumen de observaciones.
    QString registrationProblem(const RequirementRegistration& registration) const override;
    /// A partir de este tamaño, un fallo del servidor al guardar apunta al adjunto (el formulario lo
    /// comprueba en el navegador con su propio límite, que no viaja en la página).
    static constexpr qint64 kBigAttachment = 2 * 1024 * 1024;
    /// Registra el resultado en el formulario de control de calidad del requerimiento, con el acta
    /// adjunta. Es lo único que GESREQ escribe desde QAflow: recorre las mismas páginas que el usuario
    /// (bandeja → gestión → formulario del sistema) para que cada enlace y cada campo los ponga el
    /// servidor, y sólo entonces envía.
    void registerResult(const RequirementSourceSettings& s, const RequirementRegistration& registration,
                        std::function<void(const RequirementRegistrationResult&)> done) override;

    /// Hay una sesión iniciada para estos ajustes (la misma dirección y el mismo usuario).
    bool hasSession(const RequirementSourceSettings& s) const;

private:
    struct Failure {
        RequirementSourceFailure kind = RequirementSourceFailure::None;
        QString error;

        bool failed() const { return kind != RequirementSourceFailure::None; }
    };
    /// `fresh`: la sesión se acaba de iniciar para esta petición (no es una reutilizada que pudo caducar).
    using SessionHandler = std::function<void(const Failure& failure, bool fresh)>;
    using PageHandler = std::function<void(const QString& html, const Failure& failure, bool fresh)>;

    static QString sessionKey(const RequirementSourceSettings& s);
    static Failure checkSettings(const RequirementSourceSettings& s);
    static Failure transportFailure(const Response& r);
    static QString decode(const Response& r);

    /// La sesión de estos ajustes, iniciándola si no la hay. Las peticiones que llegan mientras se
    /// inicia esperan a ese mismo login en vez de lanzar otro.
    void ensureSession(const RequirementSourceSettings& s, SessionHandler done);
    void login(const RequirementSourceSettings& s);
    void finishLogin(const Failure& failure);
    /// GET de una página con sesión. Si la respuesta es el formulario de login, la sesión caducó: se
    /// inicia otra y se repite una vez (`retried` evita la segunda).
    void getPage(const RequirementSourceSettings& s, const QString& path, bool retried, PageHandler done);
    void loadDetail(const RequirementSourceSettings& s, const QString& id, bool retried,
                    std::function<void(const RequirementDetailResult&)> done);
    /// Envía el formulario del control con el resultado, el comentario y el acta de QAflow. No se
    /// reintenta solo: un envío repetido registraría el control dos veces.
    void sendControl(const RequirementSourceSettings& s, const RequirementRegistration& registration,
                     const gesreq::ControlForm& form, std::function<void(const RequirementRegistrationResult&)> done);
    /// Busca el catálogo en la página `index` de `gesreq::kSystemsPaths` y, si no está, en la siguiente.
    void loadSystems(const RequirementSourceSettings& s, int index, const QString& firstError,
                     std::function<void(const RequirementSystemsResult&)> done);

    QString m_session;     // clave de la sesión iniciada (dirección + usuario); vacía = sin sesión
    QString m_loggingIn;   // clave del login en curso; vacía = ninguno
    QList<QPair<RequirementSourceSettings, SessionHandler>> m_waiting;   // peticiones que esperan a ese login
    QString m_userName;    // usuario conectado según el menú de GESREQ, leído al iniciar sesión
};

} // namespace qaflow
