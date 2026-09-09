#include "JiraAuth.h"

namespace qaflow {

QByteArray jiraAuthorization(const TrackerSettings& s) {
    if (s.jiraAuth == JiraAuth::ServerToken) return "Bearer " + s.token.toUtf8();
    // correo + API token (Cloud) o usuario + contraseña (Server): el mismo Basic auth
    return "Basic " + (s.user.trimmed() + QLatin1Char(':') + s.token).toUtf8().toBase64();
}

} // namespace qaflow
