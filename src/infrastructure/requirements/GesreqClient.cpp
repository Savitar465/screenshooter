#include "GesreqClient.h"

#include "infrastructure/requirements/GesreqParser.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>

#include <algorithm>
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

// ---- Registro del resultado --------------------------------------------------------------------

namespace {
/// Nombre que le da el formulario de GESREQ a cada clasificación del resumen del acta.
QString observationField(const QString& classification) {
    static const QHash<QString, QString> fields{
        {QStringLiteral("A"), QStringLiteral("tot_obs_func")},  {QStringLiteral("B"), QStringLiteral("tot_obs_datos")},
        {QStringLiteral("C"), QStringLiteral("tot_obs_forma")}, {QStringLiteral("D"), QStringLiteral("tot_obs_rec")},
        {QStringLiteral("E"), QStringLiteral("tot_obs_vul")}};
    return fields.value(classification.trimmed().toUpper());
}

/// Observaciones que GESREQ cuenta para decidir si el control puede ser «OK»: todas menos las
/// recomendaciones, que no impiden dar por bueno el requerimiento.
int blockingObservations(const QList<ObservationCount>& observations) {
    int total = 0;
    for (const auto& o : observations)
        if (o.type.trimmed().toUpper() != QLatin1String("D")) total += std::max(0, o.observations);
    return total;
}

/// Extensiones que admite el adjunto, las mismas que comprueba la ventana antes de enviar.
bool allowedAttachment(const QString& path) {
    static const QStringList extensions{QStringLiteral("gif"), QStringLiteral("jpg"),  QStringLiteral("doc"), QStringLiteral("docx"),
                                        QStringLiteral("pdf"), QStringLiteral("xls"), QStringLiteral("xlsx"), QStringLiteral("vsd")};
    return extensions.contains(QFileInfo(path).suffix().toLower());
}
} // namespace

QString GesreqClient::registrationProblem(const RequirementRegistration& registration) const {
    const QString outcome = registration.result.trimmed();
    const bool conforme = outcome.compare(QStringLiteral("Conforme"), Qt::CaseInsensitive) == 0;
    if (!conforme && outcome.compare(QStringLiteral("Observado"), Qt::CaseInsensitive) != 0)
        return QCoreApplication::translate("infrastructure",
                                           "GESREQ sólo admite «Conforme» u «Observado» como resultado del control, y la revisión está en «%1»")
                .arg(registration.result);
    if (registration.attachmentPath.trimmed().isEmpty())
        return QCoreApplication::translate("infrastructure",
                                           "GESREQ exige adjuntar el acta del control: genérala antes de registrar el resultado");
    if (!QFileInfo::exists(registration.attachmentPath))
        return QCoreApplication::translate("infrastructure", "No se encuentra el acta %1").arg(registration.attachmentPath);
    if (!allowedAttachment(registration.attachmentPath))
        return QCoreApplication::translate("infrastructure",
                                           "GESREQ no admite adjuntos «%1»: el acta tiene que ser .doc, .docx, .pdf, .xls, .xlsx, .vsd, .jpg o .gif")
                .arg(QFileInfo(registration.attachmentPath).suffix());
    const int blocking = blockingObservations(registration.observations);
    if (conforme && blocking > 0)
        return QCoreApplication::translate("infrastructure",
                                           "GESREQ no acepta un control «OK» con %n observación(es) que no sean recomendaciones: registra esta ronda como observada",
                                           nullptr, blocking);
    if (!conforme && blocking == 0)
        return QCoreApplication::translate("infrastructure",
                                           "GESREQ no acepta un control «OBSERVADO» sin ninguna observación de funcionamiento, datos, forma o vulnerabilidades");
    return {};
}

void GesreqClient::registerResult(const RequirementSourceSettings& s, const RequirementRegistration& registration,
                                  std::function<void(const RequirementRegistrationResult&)> done) {
    auto fail = [done](RequirementSourceFailure kind, const QString& error, bool uncertain = false) {
        RequirementRegistrationResult result;
        result.failure = kind;
        result.error = error;
        result.uncertain = uncertain;
        done(result);
    };
    if (const Failure invalid = checkSettings(s); invalid.failed()) { fail(invalid.kind, invalid.error); return; }

    const QString id = registration.requirementId.trimmed();
    static const QRegularExpression number(QStringLiteral("^\\d+$"));
    if (!number.match(id).hasMatch()) {
        fail(RequirementSourceFailure::NotFound,
             QCoreApplication::translate("infrastructure", "«%1» no es un número de requerimiento de GESREQ").arg(registration.requirementId));
        return;
    }
    // Las reglas del formulario: se comprueban aquí para no enviar algo que GESREQ va a rechazar y que
    // dejaría el resultado a medias entre los dos sistemas. La pantalla las consulta antes (con
    // `registrationProblem`) para avisar sin llegar a enviar nada.
    if (const QString problem = registrationProblem(registration); !problem.isEmpty()) {
        fail(RequirementSourceFailure::Configuration, problem);
        return;
    }

    // La bandeja da el enlace del registro con el estado con el que figura el requerimiento: sin ese
    // estado la pantalla de gestión se abre sin el botón del control.
    getPage(s, QString::fromLatin1(gesreq::kInboxPath), false, [this, s, registration, id, fail, done](const QString& inbox, const Failure& failure, bool) {
        if (failure.failed()) { fail(failure.kind, failure.error); return; }
        const QString gestion = gesreq::registrationPath(inbox, id);
        if (gestion.isEmpty()) {
            fail(RequirementSourceFailure::NotFound,
                 QCoreApplication::translate("infrastructure", "El requerimiento %1 no está en tu bandeja de control de calidad o ya no admite registro").arg(id));
            return;
        }
        getPage(s, gestion, false, [this, s, registration, id, fail, done](const QString& page, const Failure& failure, bool) {
            if (failure.failed()) { fail(failure.kind, failure.error); return; }
            const QList<gesreq::ControlItem> items = gesreq::parseControlItems(page);
            if (items.isEmpty()) {
                fail(RequirementSourceFailure::PageChanged,
                     QCoreApplication::translate("infrastructure", "La pantalla de gestión del requerimiento %1 no ofrece el formulario del control de calidad").arg(id));
                return;
            }
            const QString wanted = registration.systemCode.simplified();
            auto matches = [&wanted](const gesreq::ControlItem& item) { return item.systemCode.compare(wanted, Qt::CaseInsensitive) == 0; };
            const auto found = wanted.isEmpty() ? items.cbegin() : std::find_if(items.cbegin(), items.cend(), matches);
            // Un requerimiento puede tocar varios sistemas y cada uno lleva su propio control: registrar
            // el de otro sería dar por revisado lo que no se ha probado.
            if (found == items.cend() || (wanted.isEmpty() && items.size() > 1)) {
                QStringList codes;
                for (const auto& item : items) codes << item.systemCode;
                fail(RequirementSourceFailure::NotFound,
                     QCoreApplication::translate("infrastructure", "El requerimiento %1 no tiene un control de calidad del sistema «%2» (tiene: %3)")
                             .arg(id, wanted.isEmpty() ? QCoreApplication::translate("infrastructure", "sin indicar") : wanted, codes.join(QStringLiteral(", "))));
                return;
            }
            getPage(s, found->formPath, false, [this, s, registration, fail, done](const QString& html, const Failure& failure, bool) {
                if (failure.failed()) { fail(failure.kind, failure.error); return; }
                const gesreq::ControlForm form = gesreq::parseControlForm(html);
                if (!form.ok) {
                    fail(RequirementSourceFailure::PageChanged,
                         QCoreApplication::translate("infrastructure", "El formulario del control de calidad no tiene la estructura esperada: %1").arg(form.error));
                    return;
                }
                sendControl(s, registration, form, done);
            });
        });
    });
}

void GesreqClient::sendControl(const RequirementSourceSettings& s, const RequirementRegistration& registration,
                               const gesreq::ControlForm& form, std::function<void(const RequirementRegistrationResult&)> done) {
    // El campo de observaciones del formulario es el de una columna de la base de datos: un texto más
    // largo que su hermano («maximo(this,2000)») haría fallar el guardado en el servidor.
    QString comment = registration.comment;
    if (comment.size() > gesreq::kControlCommentMax) comment = comment.left(gesreq::kControlCommentMax - 1) + QChar(0x2026);
    QHash<QString, QString> ours{
        {QStringLiteral("resultado_control"), registration.result.compare(QStringLiteral("Conforme"), Qt::CaseInsensitive) == 0
                                                      ? QStringLiteral("OK") : QStringLiteral("OBSERVADO")},
        {QStringLiteral("obs_controlfuncional"), comment}};
    for (const auto& count : registration.observations)
        if (const QString field = observationField(count.type); !field.isEmpty())
            ours.insert(field, QString::number(std::max(0, count.observations)));

    // Lo que trae el formulario viaja tal cual (incluidas las correcciones que puso el sistema); sólo
    // se cambia lo que decide QAflow.
    QList<QPair<QString, QString>> fields = form.fields;
    QStringList applied;
    for (auto& [name, value] : fields) {
        if (!ours.contains(name)) continue;
        value = ours.value(name);
        applied << name;
    }
    for (auto it = ours.cbegin(); it != ours.cend(); ++it)
        if (!applied.contains(it.key())) fields << qMakePair(it.key(), it.value());

    // El cuerpo se escribe como el del navegador (delimitador sin comillas, campos sin Content-Type y
    // el acta en el sitio que ocupa en el formulario): el multipart que compone Qt hacía que el
    // servidor de GESREQ respondiera un 500 sin llegar a leer ningún campo.
    const FormData body = formData(fields, registration.attachmentPath, gesreq::kControlFileField, form.filePosition);
    if (!body.ok) {
        RequirementRegistrationResult result;
        result.failure = RequirementSourceFailure::Configuration;
        result.error = QCoreApplication::translate("infrastructure", "No se pudo leer el acta %1").arg(registration.attachmentPath);
        done(result);
        return;
    }
    QNetworkRequest request = pageRequest(s.resolve(QString::fromLatin1(gesreq::kControlSavePath)));
    // Como lo envía la propia ventana: por AJAX y esperando el estado en JSON.
    request.setRawHeader("X-Requested-With", "XMLHttpRequest");
    request.setRawHeader("Accept", "application/json, text/javascript, */*; q=0.01");
    const qint64 attachmentSize = QFileInfo(registration.attachmentPath).size();
    postFormData(request, body, [this, s, attachmentSize, done](const Response& r) {
        RequirementRegistrationResult result;
        if (!r.ok) {
            // Un error del servidor no es «no hubo respuesta»: GESREQ contestó y su página suele decir
            // qué reventó (el adjunto, un dato que no cabe…). Eso es lo que hay que enseñar.
            if (r.status >= 500) {
                const QString reason = gesreq::serverErrorReason(decode(r));
                result.failure = RequirementSourceFailure::Network;
                result.error = reason.isEmpty()
                        ? QCoreApplication::translate("infrastructure", "GESREQ falló al guardar el control (HTTP %1)").arg(r.status)
                        : QCoreApplication::translate("infrastructure", "GESREQ falló al guardar el control (HTTP %1): %2").arg(r.status).arg(reason);
                if (attachmentSize > kBigAttachment)
                    result.error += QCoreApplication::translate("infrastructure", " · el acta pesa %1 MB, prueba a generarla sin capturas")
                                            .arg(double(attachmentSize) / (1024 * 1024), 0, 'f', 1);
                // Pudo guardarlo antes de fallar: hay que mirarlo en el sistema antes de repetirlo.
                result.uncertain = true;
                done(result);
                return;
            }
            const Failure failure = transportFailure(r);
            result.failure = failure.kind;
            result.error = failure.error;
            // Se cortó con el envío en marcha: GESREQ pudo haberlo guardado antes de perderse la respuesta.
            result.uncertain = r.retryable;
            done(result);
            return;
        }
        const QString body = decode(r);
        if (gesreq::isLoginPage(body)) {
            if (m_session == sessionKey(s)) m_session.clear();
            result.failure = RequirementSourceFailure::Credentials;
            result.error = QCoreApplication::translate("infrastructure", "GESREQ pidió iniciar sesión en vez de registrar el control; vuelve a intentarlo");
            done(result);
            return;
        }
        // `Anb.form.ajax` acepta las dos formas: el estado suelto o {state, data:{message…}}.
        QString state;
        QString message;
        if (r.json.isObject()) {
            const QJsonObject object = r.json.object();
            state = object.value(QStringLiteral("state")).toString();
            const QJsonValue data = object.value(QStringLiteral("data"));
            if (data.isObject()) {
                message = data.toObject().value(QStringLiteral("message")).toString();
                // El sistema dice con qué estado queda el requerimiento: el issue se actualiza con él
                // sin volver a consultar la bandeja.
                result.state = data.toObject().value(QStringLiteral("estado")).toString().simplified();
            } else {
                message = data.toString();
            }
        }
        if (state.isEmpty()) {
            state = body.trimmed();
            if (state.size() >= 2 && state.startsWith(QLatin1Char('"')) && state.endsWith(QLatin1Char('"'))) state = state.mid(1, state.size() - 2);
        }
        if (gesreq::isSavedState(state)) {
            result.ok = true;
            done(result);
            return;
        }
        result.failure = RequirementSourceFailure::Rejected;
        const QString reason = message.isEmpty() ? state : message;
        result.error = reason.isEmpty()
                ? QCoreApplication::translate("infrastructure", "GESREQ no confirmó el registro del control")
                : QCoreApplication::translate("infrastructure", "GESREQ no registró el control: %1").arg(reason.left(300));
        done(result);
    });
}

} // namespace qaflow
