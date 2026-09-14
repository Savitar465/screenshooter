#include "GesreqClient.h"

#include "infrastructure/requirements/GesreqParser.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QRegularExpression>
#include <QUrl>

#include <iterator>
#include <utility>

namespace qaflow {

QString GesreqClient::sessionKey(const RequirementSourceSettings& s) {
    return s.baseUrl() + QLatin1Char('\n') + s.user.trimmed();
}

bool GesreqClient::hasSession(const RequirementSourceSettings& s) const {
    return !m_session.isEmpty() && m_session == sessionKey(s);
}

GesreqClient::Failure GesreqClient::checkSettings(const RequirementSourceSettings& s) {
    if (s.baseUrl().isEmpty())
        return {RequirementSourceFailure::Configuration, QCoreApplication::translate("infrastructure", "Indica la dirección de GESREQ")};
    const QUrl url(s.baseUrl());
    if (!url.isValid() || url.host().isEmpty() || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https")))
        return {RequirementSourceFailure::Configuration,
                QCoreApplication::translate("infrastructure", "La dirección de GESREQ no es válida: %1").arg(s.baseUrl())};
    if (s.user.trimmed().isEmpty() || s.password.isEmpty())
        return {RequirementSourceFailure::Configuration, QCoreApplication::translate("infrastructure", "Indica el usuario y la contraseña de GESREQ")};
    return {};
}

GesreqClient::Failure GesreqClient::transportFailure(const Response& r) {
    if (r.retryable)
        return {RequirementSourceFailure::Network, QCoreApplication::translate("infrastructure", "No se pudo conectar con GESREQ · %1").arg(r.error)};
    // Sin ser un fallo de red, lo que queda es que la dirección no lleva a GESREQ (un 404 al pedir sus
    // páginas): se arregla en los ajustes, no insistiendo.
    return {RequirementSourceFailure::Configuration,
            QCoreApplication::translate("infrastructure", "GESREQ no responde en esa dirección; revisa la URL y su ruta (/greq) · %1").arg(r.error)};
}

QString GesreqClient::decode(const Response& r) {
    // GESREQ sirve UTF-8; otro juego de caracteres sólo se respeta si el servidor lo declara.
    const QByteArray type = r.header("content-type").toLower();
    if (type.contains("iso-8859-1") || type.contains("windows-1252")) return QString::fromLatin1(r.body);
    return QString::fromUtf8(r.body);
}

// ---- Sesión ---------------------------------------------------------------------------------

void GesreqClient::ensureSession(const RequirementSourceSettings& s, SessionHandler done) {
    if (hasSession(s)) { done({}, false); return; }
    m_waiting.append({s, std::move(done)});
    if (m_loggingIn.isEmpty()) login(s);   // si ya hay uno en marcha, al terminar atiende también a éste
}

void GesreqClient::login(const RequirementSourceSettings& s) {
    m_loggingIn = sessionKey(s);
    m_session.clear();
    m_userName.clear();
    clearCookies();
    // Primero la página de entrada, que abre la sesión del servidor (JSESSIONID); el formulario se
    // envía dentro de ella, como lo haría el navegador.
    get(pageRequest(s.resolve(QString())), [this, s](const Response& entry) {
        if (!entry.ok) { finishLogin(transportFailure(entry)); return; }
        const QList<QPair<QString, QString>> form{{QStringLiteral("usuario"), s.user.trimmed()}, {QStringLiteral("clave"), s.password}};
        postForm(pageRequest(s.resolve(QString::fromLatin1(gesreq::kLoginPath))), form, [this](const Response& r) {
            if (!r.ok) { finishLogin(transportFailure(r)); return; }
            const QString html = decode(r);
            if (gesreq::isLoginPage(html)) {
                const QString reason = gesreq::loginMessage(html);
                finishLogin({RequirementSourceFailure::Credentials,
                             reason.isEmpty() ? QCoreApplication::translate("infrastructure", "GESREQ rechazó el usuario o la contraseña")
                                              : QCoreApplication::translate("infrastructure", "GESREQ rechazó el inicio de sesión: %1").arg(reason)});
                return;
            }
            if (!gesreq::isAuthenticatedPage(html)) {
                finishLogin({RequirementSourceFailure::PageChanged,
                             QCoreApplication::translate("infrastructure", "GESREQ respondió al inicio de sesión con una página que no se reconoce")});
                return;
            }
            m_userName = gesreq::userName(html);
            m_session = m_loggingIn;
            finishLogin({});
        });
    });
}

void GesreqClient::finishLogin(const Failure& failure) {
    const QString key = std::exchange(m_loggingIn, QString());
    const auto waiting = std::exchange(m_waiting, {});
    for (const auto& [settings, handler] : waiting) {
        if (sessionKey(settings) == key) handler(failure, true);
        else ensureSession(settings, handler);   // esperaba a otra cuenta: ahora se inicia la suya
    }
}

void GesreqClient::getPage(const RequirementSourceSettings& s, const QString& path, bool retried, PageHandler done) {
    ensureSession(s, [this, s, path, retried, done](const Failure& session, bool fresh) {
        if (session.failed()) { done({}, session, fresh); return; }
        get(pageRequest(s.resolve(path)), [this, s, path, retried, fresh, done](const Response& r) {
            if (!r.ok) { done({}, transportFailure(r), fresh); return; }
            const QString html = decode(r);
            if (!gesreq::isLoginPage(html)) { done(html, {}, fresh); return; }
            // Sesión caducada o cerrada desde otro sitio.
            if (m_session == sessionKey(s)) m_session.clear();
            if (fresh || retried) {
                // Recién iniciada y ya no vale: repetir no lo arreglaría, y callarlo dejaría la bandeja vacía.
                done({}, {RequirementSourceFailure::Credentials,
                          QCoreApplication::translate("infrastructure", "GESREQ no conserva la sesión: vuelve a pedir el inicio de sesión justo después de entrar")},
                     fresh);
                return;
            }
            getPage(s, path, true, done);
        });
    });
}

// ---- Lectura ---------------------------------------------------------------------------------

void GesreqClient::testConnection(const RequirementSourceSettings& s, std::function<void(const ConnectionResult&)> done) {
    // Con un inicio de sesión nuevo: lo que se prueba son las credenciales de ahora, no una sesión anterior.
    if (m_loggingIn.isEmpty()) m_session.clear();
    fetchInbox(s, [this, done](const RequirementInboxResult& r) {
        if (!r.ok) { done(ConnectionResult{false, {}, r.error}); return; }
        const QString inbox = QCoreApplication::translate("infrastructure", "%1 requerimientos en la bandeja de control de calidad").arg(r.requirements.size());
        done(ConnectionResult{true, m_userName.isEmpty() ? inbox : m_userName + QStringLiteral(" · ") + inbox, {}});
    });
}

void GesreqClient::fetchInbox(const RequirementSourceSettings& s, std::function<void(const RequirementInboxResult&)> done) {
    if (const Failure invalid = checkSettings(s); invalid.failed()) {
        RequirementInboxResult result;
        result.failure = invalid.kind;
        result.error = invalid.error;
        done(result);
        return;
    }
    getPage(s, QString::fromLatin1(gesreq::kInboxPath), false, [s, done](const QString& html, const Failure& failure, bool) {
        RequirementInboxResult result;
        result.fetchedAt = QDateTime::currentDateTime();
        if (failure.failed()) {
            result.failure = failure.kind;
            result.error = failure.error;
            done(result);
            return;
        }
        const gesreq::InboxPage page = gesreq::parseInbox(html, s);
        if (!page.ok) {
            result.failure = RequirementSourceFailure::PageChanged;
            result.error = QCoreApplication::translate("infrastructure", "La bandeja de GESREQ no tiene la estructura esperada: %1").arg(page.error);
            done(result);
            return;
        }
        result.ok = true;
        result.requirements = page.requirements;
        done(result);
    });
}

void GesreqClient::fetchSystems(const RequirementSourceSettings& s, std::function<void(const RequirementSystemsResult&)> done) {
    if (const Failure invalid = checkSettings(s); invalid.failed()) {
        RequirementSystemsResult result;
        result.failure = invalid.kind;
        result.error = invalid.error;
        done(result);
        return;
    }
    loadSystems(s, 0, QString(), std::move(done));
}

void GesreqClient::loadSystems(const RequirementSourceSettings& s, int index, const QString& firstError,
                               std::function<void(const RequirementSystemsResult&)> done) {
    getPage(s, QString::fromLatin1(gesreq::kSystemsPaths[index]), false, [this, s, index, firstError, done](const QString& html, const Failure& failure, bool) {
        RequirementSystemsResult result;
        if (failure.failed()) {
            result.failure = failure.kind;
            result.error = failure.error;
            done(result);
            return;
        }
        const gesreq::SystemsPage page = gesreq::parseSystems(html);
        if (page.ok) {
            result.ok = true;
            result.systems = page.systems;
            done(result);
            return;
        }
        // Sin el desplegable en esta página (el usuario puede no tener esa opción de menú), la siguiente.
        const QString error = firstError.isEmpty() ? page.error : firstError;
        if (index + 1 < int(std::size(gesreq::kSystemsPaths))) {
            loadSystems(s, index + 1, error, done);
            return;
        }
        result.failure = RequirementSourceFailure::PageChanged;
        result.error = QCoreApplication::translate("infrastructure", "GESREQ no ofrece el catálogo de sistemas: %1").arg(error);
        done(result);
    });
}

void GesreqClient::fetchDetail(const RequirementSourceSettings& s, const QString& id, std::function<void(const RequirementDetailResult&)> done) {
    RequirementDetailResult result;
    if (const Failure invalid = checkSettings(s); invalid.failed()) {
        result.failure = invalid.kind;
        result.error = invalid.error;
        done(result);
        return;
    }
    static const QRegularExpression number(QStringLiteral("^\\d+$"));
    if (!number.match(id.trimmed()).hasMatch()) {
        result.failure = RequirementSourceFailure::NotFound;
        result.error = QCoreApplication::translate("infrastructure", "«%1» no es un número de requerimiento de GESREQ").arg(id);
        done(result);
        return;
    }
    loadDetail(s, id.trimmed(), false, std::move(done));
}

void GesreqClient::loadDetail(const RequirementSourceSettings& s, const QString& id, bool retried,
                              std::function<void(const RequirementDetailResult&)> done) {
    getPage(s, gesreq::detailPath(id), retried, [this, s, id, retried, done](const QString& html, const Failure& failure, bool fresh) {
        RequirementDetailResult result;
        result.fetchedAt = QDateTime::currentDateTime();
        if (failure.failed()) {
            result.failure = failure.kind;
            result.error = failure.error;
            done(result);
            return;
        }
        const gesreq::DetailPage page = gesreq::parseDetail(html, id, s);
        switch (page.kind) {
            case gesreq::DetailPage::Kind::Detail:
                result.ok = true;
                result.detail = page.detail;
                break;
            case gesreq::DetailPage::Kind::Empty:
                // La cáscara sin datos es lo que GESREQ devuelve sin sesión: con una sesión reutilizada es
                // que caducó, así que se inicia otra y se repite una vez. Si llega con la sesión recién
                // iniciada, este usuario no puede ver el requerimiento.
                if (!fresh && !retried) {
                    if (m_session == sessionKey(s)) m_session.clear();
                    loadDetail(s, id, true, done);
                    return;
                }
                [[fallthrough]];
            case gesreq::DetailPage::Kind::Missing:
                result.failure = RequirementSourceFailure::NotFound;
                result.error = QCoreApplication::translate("infrastructure", "GESREQ no devuelve datos del requerimiento %1: no existe o tu usuario no puede verlo").arg(id);
                break;
            case gesreq::DetailPage::Kind::Unexpected:
                result.failure = RequirementSourceFailure::PageChanged;
                result.error = QCoreApplication::translate("infrastructure", "La ficha del requerimiento %1 no tiene la estructura esperada: %2").arg(id, page.error);
                break;
        }
        done(result);
    });
}

} // namespace qaflow
