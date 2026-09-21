#include "JiraClient.h"

#include "infrastructure/tracker/JiraAuth.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>

#include <algorithm>

namespace qaflow {

QNetworkRequest JiraClient::request(const TrackerSettings& s, const QString& path) const {
    QNetworkRequest req = jsonRequest(s.baseUrl() + path);
    req.setRawHeader("Authorization", jiraAuthorization(s));
    return req;
}

QString JiraClient::missingCredentials(const TrackerSettings& s) {
    if (s.url.trimmed().isEmpty())
        return QCoreApplication::translate("infrastructure", "Indica la URL de Jira");
    if (s.needsUser() && s.user.trimmed().isEmpty())
        return QCoreApplication::translate("infrastructure", "Indica %1 y %2").arg(s.userLabel().toLower(), s.secretLabel().toLower());
    if (s.token.trimmed().isEmpty())
        return QCoreApplication::translate("infrastructure", "Indica %1").arg(s.secretLabel().toLower());
    return {};
}

QString JiraClient::errorFor(const TrackerSettings& s, const Response& r) {
    if (r.status != 401 && r.status != 403) return r.error;
    // Jira Server (Seraph) explica en cabeceras por qué rechaza el login: tras varios intentos fallidos
    // bloquea al usuario y exige resolver un CAPTCHA en el navegador antes de volver a aceptar la API.
    const QByteArray denied = r.header("X-Authentication-Denied-Reason");
    const QByteArray reason = r.header("X-Seraph-LoginReason");
    if (denied.contains("CAPTCHA") || reason.contains("AUTHENTICATION_DENIED"))
        return QCoreApplication::translate("infrastructure",
                                           "Jira ha bloqueado el acceso tras varios intentos fallidos: entra en %1 desde el navegador, "
                                           "resuelve el CAPTCHA y vuelve a probar.").arg(s.baseUrl());
    if (r.status == 401) {
        switch (s.jiraAuth) {
            case JiraAuth::ServerBasic:
                return QCoreApplication::translate("infrastructure", "Usuario o contraseña incorrectos.");
            case JiraAuth::ServerToken:
                return QCoreApplication::translate("infrastructure",
                                                   "Token personal no válido. Jira Server sólo admite tokens desde la versión 8.14; "
                                                   "en versiones anteriores usa usuario y contraseña.");
            case JiraAuth::CloudToken:
                return QCoreApplication::translate("infrastructure", "Correo o API token incorrectos.");
        }
    }
    return QCoreApplication::translate("infrastructure", "Sin permisos en Jira para esta operación (%1).").arg(r.error);
}

QString JiraClient::assignableSearchPath(const TrackerSettings& s, const QString& query, int maxResults) {
    QString path = QStringLiteral("/rest/api/2/user/assignable/search?project=%1&maxResults=%2")
                       .arg(s.project.trimmed()).arg(maxResults);
    // Jira Cloud sustituyó `username` por `query`; Jira Server / Data Center sigue con `username`.
    const QString text = query.trimmed();
    if (!text.isEmpty())
        path += (s.usesAccountId() ? QStringLiteral("&query=") : QStringLiteral("&username="))
                + QString::fromUtf8(QUrl::toPercentEncoding(text));
    return path;
}

QList<Assignee> JiraClient::assigneesFrom(const QJsonArray& users, bool accountIds) {
    QList<Assignee> out;
    for (const auto& v : users) {
        const QJsonObject u = v.toObject();
        // Jira Server permite desactivar cuentas sin borrarlas: no se puede asignar a ellas.
        if (u.contains(QStringLiteral("active")) && !u[QStringLiteral("active")].toBool()) continue;
        Assignee a;
        a.id = accountIds ? u[QStringLiteral("accountId")].toString() : u[QStringLiteral("name")].toString();
        a.name = u[QStringLiteral("displayName")].toString();
        if (a.name.isEmpty()) a.name = a.id;
        if (!a.id.isEmpty()) out.append(a);
    }
    return out;
}

void JiraClient::searchAssignees(const TrackerSettings& s, const QString& query, std::function<void(const AssigneeSearch&)> done) {
    if (const QString missing = missingCredentials(s); !missing.isEmpty()) { done(AssigneeSearch{false, {}, missing}); return; }
    if (s.project.trimmed().isEmpty()) {
        done(AssigneeSearch{false, {}, QCoreApplication::translate("infrastructure", "Indica la clave del proyecto")});
        return;
    }
    const bool accountIds = s.usesAccountId();
    get(request(s, assignableSearchPath(s, query, 50)), [s, accountIds, done](const Response& r) {
        if (!r.ok) { done(AssigneeSearch{false, {}, errorFor(s, r)}); return; }
        done(AssigneeSearch{true, assigneesFrom(r.json.array(), accountIds), {}});
    });
}

void JiraClient::testConnection(const TrackerSettings& s, std::function<void(const ConnectionResult&)> done) {
    if (const QString missing = missingCredentials(s); !missing.isEmpty()) {
        done(ConnectionResult{false, {}, missing});
        return;
    }
    get(request(s, QStringLiteral("/rest/api/2/myself")), [s, done](const Response& r) {
        if (!r.ok) { done(ConnectionResult{false, {}, errorFor(s, r)}); return; }
        done(ConnectionResult{true, r.json.object()[QStringLiteral("displayName")].toString(), {}});
    });
}

void JiraClient::createIssue(const TrackerSettings& s, const BugReport& bug, std::function<void(const IssueResult&)> done) {
    QJsonObject fields{
        {"project", QJsonObject{{"key", s.project.trimmed()}}},
        {"issuetype", QJsonObject{{"name", bug.issueType.isEmpty() ? QStringLiteral("Bug") : bug.issueType}}},
        {"summary", bug.title.trimmed()},
        {"description", bug.jiraDescription()},
    };
    QJsonArray labels{QStringLiteral("qaflow")};
    if (!bug.linkedCaseId.isEmpty()) labels.append(bug.linkedCaseId);
    for (const auto& l : bug.labels) if (!l.trimmed().isEmpty()) labels.append(l.trimmed().replace(QLatin1Char(' '), QLatin1Char('-')));
    fields["labels"] = labels;
    if (!bug.priority.isEmpty()) fields["priority"] = QJsonObject{{"name", bug.priority}};
    if (!bug.assigneeId.isEmpty())
        fields["assignee"] = s.usesAccountId() ? QJsonObject{{"accountId", bug.assigneeId}} : QJsonObject{{"name", bug.assigneeId}};
    if (!bug.components.isEmpty()) {
        QJsonArray comps;
        for (const auto& c : bug.components) if (!c.trimmed().isEmpty()) comps.append(QJsonObject{{"name", c.trimmed()}});
        if (!comps.isEmpty()) fields["components"] = comps;
    }
    if (!bug.affectsVersions.isEmpty()) {
        QJsonArray versions;
        for (const auto& v : bug.affectsVersions) if (!v.trimmed().isEmpty()) versions.append(QJsonObject{{"name", v.trimmed()}});
        if (!versions.isEmpty()) fields["versions"] = versions;
    }

    postJson(request(s, QStringLiteral("/rest/api/2/issue")), QJsonDocument(QJsonObject{{"fields", fields}}), [this, s, bug, done](const Response& r) {
        if (!r.ok) { IssueResult f; f.error = errorFor(s, r); f.retryable = r.retryable; done(f); return; }
        IssueResult res;
        res.ok = true;
        res.key = r.json.object()[QStringLiteral("key")].toString();
        res.url = s.issueUrl(res.key);
        uploadAttachments(s, res, existingFiles(bug.attachmentPaths), done);
    });
}

void JiraClient::uploadAttachments(const TrackerSettings& s, IssueResult result, QStringList pending, std::function<void(const IssueResult&)> done) {
    if (pending.isEmpty()) { done(result); return; }
    const QString path = pending.takeFirst();
    QHttpMultiPart* multi = multipartFile(path);
    if (!multi) { uploadAttachments(s, result, pending, done); return; }
    QNetworkRequest req = request(s, QStringLiteral("/rest/api/2/issue/%1/attachments").arg(result.key));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QVariant());   // lo fija el multipart
    req.setRawHeader("X-Atlassian-Token", "no-check");
    postMultipart(req, multi, [this, s, result, pending, done](const Response& r) mutable {
        if (r.ok) ++result.attachmentsUploaded;
        uploadAttachments(s, result, pending, done);
    });
}

void JiraClient::fetchStatus(const TrackerSettings& s, const QString& key, std::function<void(const IssueStatus&)> done) {
    get(request(s, QStringLiteral("/rest/api/2/issue/%1?fields=status").arg(key)), [s, done](const Response& r) {
        if (!r.ok) { done(IssueStatus{false, {}, false, errorFor(s, r)}); return; }
        const QJsonObject status = r.json.object()[QStringLiteral("fields")].toObject()[QStringLiteral("status")].toObject();
        IssueStatus st;
        st.ok = true;
        st.status = status[QStringLiteral("name")].toString();
        st.resolved = status[QStringLiteral("statusCategory")].toObject()[QStringLiteral("key")].toString() == QStringLiteral("done");
        done(st);
    });
}

void JiraClient::fetchMetadata(const TrackerSettings& s, std::function<void(const MetadataResult&)> done) {
    const QString key = s.project.trimmed();
    // Tres peticiones encadenadas: proyecto (tipos, componentes, versiones), prioridades y asignables.
    get(request(s, QStringLiteral("/rest/api/2/project/%1").arg(key)), [this, s, key, done](const Response& r) {
        if (!r.ok) { done(MetadataResult{false, {}, errorFor(s, r)}); return; }
        ProjectMetadata meta;
        const QJsonObject p = r.json.object();
        for (const auto& v : p[QStringLiteral("issueTypes")].toArray()) {
            const QJsonObject t = v.toObject();
            if (!t[QStringLiteral("subtask")].toBool()) meta.issueTypes << t[QStringLiteral("name")].toString();
        }
        for (const auto& v : p[QStringLiteral("components")].toArray()) meta.components << v.toObject()[QStringLiteral("name")].toString();
        for (const auto& v : p[QStringLiteral("versions")].toArray()) {
            const QJsonObject ver = v.toObject();
            if (!ver[QStringLiteral("archived")].toBool()) meta.versions << ver[QStringLiteral("name")].toString();
        }
        get(request(s, QStringLiteral("/rest/api/2/priority")), [this, s, key, meta, done](const Response& r2) mutable {
            if (r2.ok) for (const auto& v : r2.json.array()) meta.priorities << v.toObject()[QStringLiteral("name")].toString();
            const bool accountIds = s.usesAccountId();
            // Primeros asignables del proyecto: el formulario arranca con ellos y luego busca en el servidor.
            get(request(s, assignableSearchPath(s, QString(), 100)), [meta, accountIds, done](const Response& r3) mutable {
                if (r3.ok) meta.assignees = assigneesFrom(r3.json.array(), accountIds);
                done(MetadataResult{true, meta, {}});
            });
        });
    });
}

namespace {
/// Campos del issue de QAflow en Jira. Las etiquetas no admiten espacios.
QJsonObject issueFields(const TrackerSettings& s, const TrackerIssueDraft& draft) {
    QJsonArray labels;
    for (const auto& l : draft.labels)
        if (!l.trimmed().isEmpty()) labels.append(QString(l.trimmed()).replace(QLatin1Char(' '), QLatin1Char('-')));
    QJsonObject fields{
        {"project", QJsonObject{{"key", s.project.trimmed()}}},
        {"issuetype", QJsonObject{{"name", draft.issueType.trimmed().isEmpty() ? QStringLiteral("Tarea") : draft.issueType.trimmed()}}},
        {"summary", draft.summary.trimmed()},
        {"description", draft.description},
    };
    if (!labels.isEmpty()) fields["labels"] = labels;
    return fields;
}
} // namespace

void JiraClient::publishIssue(const TrackerSettings& s, const TrackerIssueDraft& draft, std::function<void(const IssueResult&)> done) {
    if (const QString missing = missingCredentials(s); !missing.isEmpty()) { IssueResult f; f.error = missing; done(f); return; }
    if (s.project.trimmed().isEmpty()) {
        IssueResult f;
        f.error = QCoreApplication::translate("infrastructure", "Indica la clave del proyecto");
        done(f);
        return;
    }
    postJson(request(s, QStringLiteral("/rest/api/2/issue")), QJsonDocument(QJsonObject{{"fields", issueFields(s, draft)}}), [this, s, done](const Response& r) {
        if (!r.ok) { IssueResult f; f.error = errorFor(s, r); f.retryable = r.retryable; done(f); return; }
        IssueResult res;
        res.ok = true;
        res.key = r.json.object()[QStringLiteral("key")].toString();
        res.url = s.issueUrl(res.key);
        // Quien crea el issue del requerimiento es quien lo prueba: queda a su nombre. Se asigna aparte
        // (y no en los campos de la creación) porque el campo puede no estar en la pantalla de alta del
        // proyecto, y entonces Jira rechazaría el issue entero.
        assignToMyself(s, res, done);
    });
}

void JiraClient::withMyself(const TrackerSettings& s, std::function<void(const QString&, const QString&)> done) {
    const QString scope = s.baseUrl() + QLatin1Char('|') + s.user.trimmed();
    if (!m_myself.isEmpty() && m_myselfFor == scope) { done(m_myself, {}); return; }
    get(request(s, QStringLiteral("/rest/api/2/myself")), [this, s, scope, done](const Response& r) {
        if (!r.ok) { done({}, errorFor(s, r)); return; }
        const QJsonObject me = r.json.object();
        const QString id = s.usesAccountId() ? me[QStringLiteral("accountId")].toString() : me[QStringLiteral("name")].toString();
        if (id.isEmpty()) { done({}, QCoreApplication::translate("infrastructure", "Jira no dice quién es el usuario de la conexión")); return; }
        m_myself = id;
        m_myselfFor = scope;
        done(id, {});
    });
}

void JiraClient::assignToMyself(const TrackerSettings& s, IssueResult result, std::function<void(const IssueResult&)> done) {
    withMyself(s, [this, s, result, done](const QString& id, const QString& error) mutable {
        if (id.isEmpty()) {
            result.warning = QCoreApplication::translate("infrastructure", "%1 no se pudo asignar a tu usuario · %2").arg(result.key, error);
            done(result);
            return;
        }
        const QJsonObject body = s.usesAccountId() ? QJsonObject{{"accountId", id}} : QJsonObject{{"name", id}};
        sendCustom("PUT", request(s, QStringLiteral("/rest/api/2/issue/%1/assignee").arg(result.key)),
                   QJsonDocument(body).toJson(QJsonDocument::Compact), [s, result, done](const Response& r) mutable {
                       if (!r.ok)
                           result.warning = QCoreApplication::translate("infrastructure", "%1 no se pudo asignar a tu usuario · %2")
                                                .arg(result.key, errorFor(s, r));
                       done(result);
                   });
    });
}

QString JiraClient::closingTransition(const QJsonArray& transitions, QString* resolution) {
    // Los nombres con los que se suele llamar al cierre, en el orden en que se prefieren: un flujo
    // puede ofrecer varias salidas a «hecho» («Resolver» y «Cerrar») y la definitiva es la de cerrar.
    static const QStringList preferred{QStringLiteral("cerrar"), QStringLiteral("close"), QStringLiteral("finaliz"),
                                       QStringLiteral("done"), QStringLiteral("hecho"), QStringLiteral("resol"), QStringLiteral("resolv")};
    int bestRank = -1;
    QJsonObject best;
    for (const auto& v : transitions) {
        const QJsonObject t = v.toObject();
        const QJsonObject to = t[QStringLiteral("to")].toObject();
        if (to[QStringLiteral("statusCategory")].toObject()[QStringLiteral("key")].toString() != QStringLiteral("done")) continue;
        const QString name = t[QStringLiteral("name")].toString() + QLatin1Char(' ') + to[QStringLiteral("name")].toString();
        int rank = 0;   // cualquier transición a «hecho» vale; las que se llaman como un cierre, más
        for (int i = 0; i < preferred.size(); ++i)
            if (name.contains(preferred[i], Qt::CaseInsensitive)) { rank = int(preferred.size()) - i; break; }
        if (rank > bestRank) { bestRank = rank; best = t; }
    }
    if (best.isEmpty()) return {};
    if (resolution) {
        resolution->clear();
        const QJsonObject field = best[QStringLiteral("fields")].toObject()[QStringLiteral("resolution")].toObject();
        const QJsonArray allowed = field[QStringLiteral("allowedValues")].toArray();
        // La resolución se manda sólo si la transición la pide; entre las que admite, la de «hecho».
        for (const auto& wanted : {QStringLiteral("Done"), QStringLiteral("Hecho"), QStringLiteral("Fixed"), QStringLiteral("Resuelta"),
                                   QStringLiteral("Resuelto"), QStringLiteral("Finalizado"), QStringLiteral("Listo")}) {
            for (const auto& a : allowed)
                if (a.toObject()[QStringLiteral("name")].toString().compare(wanted, Qt::CaseInsensitive) == 0) {
                    *resolution = a.toObject()[QStringLiteral("name")].toString();
                    break;
                }
            if (!resolution->isEmpty()) break;
        }
        if (resolution->isEmpty() && !allowed.isEmpty()) *resolution = allowed.first().toObject()[QStringLiteral("name")].toString();
    }
    return best[QStringLiteral("id")].toString();
}

void JiraClient::closeIssue(const TrackerSettings& s, const QString& key, std::function<void(const IssueResult&)> done) {
    if (const QString missing = missingCredentials(s); !missing.isEmpty()) { IssueResult f; f.error = missing; done(f); return; }
    const QString issue = key.trimmed();
    if (issue.isEmpty()) {
        IssueResult f;
        f.error = QCoreApplication::translate("infrastructure", "Indica el issue del gestor que se cierra");
        done(f);
        return;
    }
    // Un issue ya cerrado no se toca: cerrar dos veces tiene que ser inofensivo.
    fetchStatus(s, issue, [this, s, issue, done](const IssueStatus& status) {
        IssueResult res;
        res.key = issue;
        res.url = s.issueUrl(issue);
        if (status.ok && status.resolved) { res.ok = true; done(res); return; }
        get(request(s, QStringLiteral("/rest/api/2/issue/%1/transitions?expand=transitions.fields").arg(issue)),
            [this, s, issue, res, done](const Response& r) mutable {
                if (!r.ok) { res.error = errorFor(s, r); res.retryable = r.retryable; done(res); return; }
                QString resolution;
                const QString transition = closingTransition(r.json.object()[QStringLiteral("transitions")].toArray(), &resolution);
                if (transition.isEmpty()) {
                    res.error = QCoreApplication::translate("infrastructure",
                                                            "El flujo de %1 no ofrece desde su estado actual ninguna transición que lo cierre: "
                                                            "ciérralo en Jira").arg(issue);
                    done(res);
                    return;
                }
                QJsonObject body{{"transition", QJsonObject{{"id", transition}}}};
                if (!resolution.isEmpty()) body["fields"] = QJsonObject{{"resolution", QJsonObject{{"name", resolution}}}};
                postJson(request(s, QStringLiteral("/rest/api/2/issue/%1/transitions").arg(issue)), QJsonDocument(body),
                         [s, res, done](const Response& r2) mutable {
                             if (!r2.ok) { res.error = errorFor(s, r2); res.retryable = r2.retryable; done(res); return; }
                             res.ok = true;
                             done(res);
                         });
            });
    });
}

void JiraClient::fetchIssue(const TrackerSettings& s, const QString& key, std::function<void(const TrackerIssueInfo&)> done) {
    if (const QString missing = missingCredentials(s); !missing.isEmpty()) { TrackerIssueInfo f; f.error = missing; done(f); return; }
    get(request(s, QStringLiteral("/rest/api/2/issue/%1?fields=summary,status,issuetype").arg(key.trimmed())), [s, key, done](const Response& r) {
        if (!r.ok) {
            TrackerIssueInfo f;
            // Un 404 aquí es «esa clave no existe», que es lo que hay que decir al vincular.
            f.error = r.status == 404 ? QCoreApplication::translate("infrastructure", "%1 no existe en %2").arg(key.trimmed(), s.baseUrl()) : errorFor(s, r);
            f.retryable = r.retryable;
            done(f);
            return;
        }
        const QJsonObject fields = r.json.object()[QStringLiteral("fields")].toObject();
        const QJsonObject status = fields[QStringLiteral("status")].toObject();
        TrackerIssueInfo info;
        info.ok = true;
        info.key = r.json.object()[QStringLiteral("key")].toString();
        if (info.key.isEmpty()) info.key = key.trimmed();
        info.url = s.issueUrl(info.key);
        info.title = fields[QStringLiteral("summary")].toString();
        info.issueType = fields[QStringLiteral("issuetype")].toObject()[QStringLiteral("name")].toString();
        info.status = status[QStringLiteral("name")].toString();
        info.resolved = status[QStringLiteral("statusCategory")].toObject()[QStringLiteral("key")].toString() == QStringLiteral("done");
        done(info);
    });
}

void JiraClient::searchProjectBugs(const TrackerSettings& s, int startAt, int max, std::function<void(const TrackerIssueList&)> done) {
    TrackerIssueList fail;
    if (const QString missing = missingCredentials(s); !missing.isEmpty()) { fail.error = missing; done(fail); return; }
    const QString project = s.project.trimmed();
    if (project.isEmpty()) {
        fail.error = QCoreApplication::translate("infrastructure", "Los ajustes no dicen en qué proyecto de Jira buscar");
        done(fail);
        return;
    }
    // Los bugs que crea QAflow llevan siempre su etiqueta: eso es lo que los distingue de los que
    // abre cualquier otro en el mismo proyecto. Y de los suyos, los que se siguen aquí: errores y mejoras.
    QStringList quoted;
    for (const auto& t : BugReport::jiraIssueTypes()) quoted << QLatin1Char('"') + t + QLatin1Char('"');
    const QString jql = QStringLiteral("project = \"%1\" AND labels = qaflow AND issuetype in (%2) ORDER BY created DESC")
                            .arg(project, quoted.join(QStringLiteral(", ")));
    const QString path = QStringLiteral("/rest/api/2/search?jql=%1&startAt=%2&maxResults=%3&fields=summary,status,created,labels,issuetype")
                             .arg(QString::fromUtf8(QUrl::toPercentEncoding(jql)), QString::number(std::max(0, startAt)),
                                  QString::number(std::clamp(max, 1, 100)));
    get(request(s, path), [s, startAt, done](const Response& r) {
        if (!r.ok) {
            TrackerIssueList f;
            f.error = errorFor(s, r);
            // Un 400 aquí suele ser que esta instancia no llama así a sus tipos de incidencia: el
            // mensaje de Jira no se entiende si no se sabe de dónde salen esos nombres.
            if (r.status == 400)
                f.error = QCoreApplication::translate("infrastructure", "Jira rechazó la búsqueda de incidencias de tipo %1 · %2")
                              .arg(BugReport::jiraIssueTypes().join(QStringLiteral(" o ")), f.error);
            done(f);
            return;
        }
        TrackerIssueList out;
        out.ok = true;
        out.total = r.json.object()[QStringLiteral("total")].toInt();
        for (const auto& v : r.json.object()[QStringLiteral("issues")].toArray()) {
            const QJsonObject o = v.toObject();
            const QJsonObject fields = o[QStringLiteral("fields")].toObject();
            const QJsonObject status = fields[QStringLiteral("status")].toObject();
            TrackerIssueInfo info;
            info.ok = true;
            info.key = o[QStringLiteral("key")].toString();
            if (info.key.isEmpty()) continue;
            info.url = s.issueUrl(info.key);
            info.title = fields[QStringLiteral("summary")].toString();
            info.issueType = fields[QStringLiteral("issuetype")].toObject()[QStringLiteral("name")].toString();
            info.status = status[QStringLiteral("name")].toString();
            info.resolved = status[QStringLiteral("statusCategory")].toObject()[QStringLiteral("key")].toString() == QStringLiteral("done");
            // "2026-09-17T10:04:11.000+0200": Qt lee el ISO con offset si se le quitan los milisegundos.
            info.createdAt = QDateTime::fromString(fields[QStringLiteral("created")].toString(), Qt::ISODateWithMs);
            for (const auto& l : fields[QStringLiteral("labels")].toArray()) info.labels << l.toString();
            out.issues << info;
        }
        // Jira dice cuántos hay en total; mientras queden por detrás de esta página, se puede seguir.
        const int seen = std::max(0, startAt) + static_cast<int>(out.issues.size());
        out.nextStart = !out.issues.isEmpty() && seen < out.total ? seen : -1;
        done(out);
    });
}

void JiraClient::updateIssue(const TrackerSettings& s, const QString& key, const TrackerIssueDraft& draft, std::function<void(const IssueResult&)> done) {
    if (const QString missing = missingCredentials(s); !missing.isEmpty()) { IssueResult f; f.error = missing; done(f); return; }
    // Sólo el texto que salió de QAflow: el tipo, las etiquetas y todo lo demás se quedan como estén en Jira.
    const QJsonObject body{{"fields", QJsonObject{{"summary", draft.summary.trimmed()}, {"description", draft.description}}}};
    sendCustom("PUT", request(s, QStringLiteral("/rest/api/2/issue/%1").arg(key.trimmed())), QJsonDocument(body).toJson(QJsonDocument::Compact),
               [s, key, done](const Response& r) {
                   if (!r.ok) { IssueResult f; f.error = errorFor(s, r); f.retryable = r.retryable; done(f); return; }
                   IssueResult res;
                   res.ok = true;
                   res.key = key.trimmed();
                   res.url = s.issueUrl(res.key);
                   done(res);
               });
}

void JiraClient::commentIssue(const TrackerSettings& s, const QString& key, const QString& body, const QStringList& attachments,
                              std::function<void(const IssueResult&)> done) {
    if (const QString missing = missingCredentials(s); !missing.isEmpty()) { IssueResult f; f.error = missing; done(f); return; }
    const QString issue = key.trimmed();
    if (issue.isEmpty()) {
        IssueResult f;
        f.error = QCoreApplication::translate("infrastructure", "Indica el issue del gestor que se comenta");
        done(f);
        return;
    }
    postJson(request(s, QStringLiteral("/rest/api/2/issue/%1/comment").arg(issue)), QJsonDocument(QJsonObject{{"body", body}}),
             [this, s, issue, attachments, done](const Response& r) {
                 if (!r.ok) { IssueResult f; f.error = errorFor(s, r); f.retryable = r.retryable; done(f); return; }
                 IssueResult res;
                 res.ok = true;
                 res.key = issue;
                 res.url = s.issueUrl(issue);
                 // El comentario ya está; los adjuntos que fallen no lo deshacen, se cuentan aparte.
                 uploadAttachments(s, res, existingFiles(attachments), done);
             });
}

void JiraClient::withLinkType(const TrackerSettings& s, std::function<void(const QString&, const QString&)> done) {
    if (!m_linkType.isEmpty() && m_linkTypeFor == s.baseUrl()) { done(m_linkType, {}); return; }
    get(request(s, QStringLiteral("/rest/api/2/issueLinkType")), [this, s, done](const Response& r) {
        if (!r.ok) { done({}, errorFor(s, r)); return; }
        const QJsonArray types = r.json.object()[QStringLiteral("issueLinkTypes")].toArray();
        QString chosen;
        for (const auto& v : types) {
            const QJsonObject type = v.toObject();
            const QString name = type[QStringLiteral("name")].toString();
            const QString inward = type[QStringLiteral("inward")].toString();
            // «Relates» es el de serie; una instancia traducida lo llama de otra forma, así que se
            // reconoce por la raíz de la palabra y, si no hay nada parecido, vale el primero.
            if (name.compare(QStringLiteral("Relates"), Qt::CaseInsensitive) == 0) { chosen = name; break; }
            const bool relates = name.contains(QStringLiteral("relat"), Qt::CaseInsensitive) ||
                                 inward.contains(QStringLiteral("relacion"), Qt::CaseInsensitive) ||
                                 inward.contains(QStringLiteral("relat"), Qt::CaseInsensitive);
            if (chosen.isEmpty() || relates) chosen = name;
            if (relates) break;
        }
        if (chosen.isEmpty()) {
            done({}, QCoreApplication::translate("infrastructure", "Jira no ofrece ningún tipo de enlace entre issues"));
            return;
        }
        m_linkType = chosen;
        m_linkTypeFor = s.baseUrl();
        done(chosen, {});
    });
}

void JiraClient::linkIssues(const TrackerSettings& s, const QString& from, const QString& to,
                            std::function<void(const IssueResult&)> done) {
    if (const QString missing = missingCredentials(s); !missing.isEmpty()) { IssueResult f; f.error = missing; done(f); return; }
    const QString source = from.trimmed();
    const QString target = to.trimmed();
    if (source.isEmpty() || target.isEmpty() || source.compare(target, Qt::CaseInsensitive) == 0) {
        IssueResult f;
        f.error = QCoreApplication::translate("infrastructure", "Hacen falta dos issues distintos para enlazarlos");
        done(f);
        return;
    }
    withLinkType(s, [this, s, source, target, done](const QString& type, const QString& error) {
        if (type.isEmpty()) { IssueResult f; f.error = error; done(f); return; }
        const QJsonObject body{
            {"type", QJsonObject{{"name", type}}},
            {"inwardIssue", QJsonObject{{"key", source}}},
            {"outwardIssue", QJsonObject{{"key", target}}},
        };
        // Jira no duplica un enlace que ya existe, así que republicar no llena el issue de repetidos.
        postJson(request(s, QStringLiteral("/rest/api/2/issueLink")), QJsonDocument(body), [s, source, target, done](const Response& r) {
            IssueResult res;
            if (!r.ok) {
                res.error = errorFor(s, r);
                res.retryable = r.retryable;
                done(res);
                return;
            }
            res.ok = true;
            res.key = source;
            res.url = s.issueUrl(source);
            done(res);
        });
    });
}

void JiraClient::fetchProjects(const TrackerSettings& s, std::function<void(const TrackerProjectList&)> done) {
    if (const QString missing = missingCredentials(s); !missing.isEmpty()) { done(TrackerProjectList{false, {}, missing}); return; }
    // `/project` devuelve de una vez los proyectos que puede ver el usuario, en Jira Server 7 y 8 y en
    // Cloud (donde convive con la versión paginada `/project/search`).
    get(request(s, QStringLiteral("/rest/api/2/project")), [s, done](const Response& r) {
        if (!r.ok) { done(TrackerProjectList{false, {}, errorFor(s, r)}); return; }
        TrackerProjectList list;
        list.ok = true;
        for (const auto& v : r.json.array()) {
            const QJsonObject p = v.toObject();
            TrackerProject project{p[QStringLiteral("key")].toString(), p[QStringLiteral("name")].toString()};
            if (!project.key.isEmpty()) list.projects << project;
        }
        std::sort(list.projects.begin(), list.projects.end(),
                  [](const TrackerProject& a, const TrackerProject& b) { return a.name.localeAwareCompare(b.name) < 0; });
        done(list);
    });
}

} // namespace qaflow
