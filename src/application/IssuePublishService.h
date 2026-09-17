#pragma once

#include "application/IssueStore.h"
#include "application/SettingsStore.h"
#include "core/services/IIssueTracker.h"

#include <QObject>
#include <QStringList>
#include <functional>
#include <memory>

namespace qaflow {

/// Lo que se va a publicar del issue: se enseña y se puede corregir antes de enviarlo.
struct IssueDraft {
    QString summary;
    QString description;
    QString issueType;
    QStringList labels;
};

/// Publicación del issue en el gestor (Jira): crear su representación, vincular una que ya existe,
/// actualizarla cuando QAflow tiene algo distinto de lo enviado y consultar su estado.
///
/// La creación es siempre explícita y nada se sobrescribe solo: `needsUpdate()` compara lo que hay ahora
/// con lo último que salió de QAflow, y sólo «Actualizar» reescribe el título y la descripción en el
/// gestor. Si un envío se corta sin respuesta, el issue queda marcado como «sin confirmar» (puede haberse
/// creado igualmente) y quien reintente tiene que decir que ya lo comprobó.
class IssuePublishService : public QObject {
    Q_OBJECT
public:
    IssuePublishService(std::shared_ptr<IIssueTracker> tracker, IssueStore& issues, SettingsStore& settings, QObject* parent = nullptr);

    /// El gestor configurado sabe publicar issues y tiene lo que necesita (URL, proyecto y credenciales).
    bool canPublish() const;
    /// Dónde se publicaría ("Jira · SHOP"); vacío si falta configurarlo.
    QString destination() const;
    /// Borrador a partir del issue: título, descripción con lo importado de GESREQ y las notas de QA, y tipo.
    IssueDraft draftFor(const Issue& issue) const;
    /// Lo que hay en QAflow ya no es lo último publicado: el issue está pendiente de actualizar en el gestor.
    bool needsUpdate(const Issue& issue) const;

    struct Result {
        bool ok = false;
        QString key;
        QString url;
        QString error;
        bool retryable = false;
        /// El envío se cortó sin respuesta: hay que comprobar en el gestor si se creó antes de reintentar.
        bool uncertain = false;
    };
    void publish(const QString& issueId, const IssueDraft& draft, std::function<void(const Result&)> done);
    void update(const QString& issueId, const IssueDraft& draft, std::function<void(const Result&)> done);
    /// Vincula un issue que ya existe en el gestor (comprobando que está) en vez de crear otro.
    void link(const QString& issueId, const QString& key, std::function<void(const Result&)> done);
    /// Olvida la representación en el gestor. No borra nada en él.
    void unlink(const QString& issueId);
    void refreshStatus(const QString& issueId, std::function<void(const Result&)> done);

    /// ¿Se puede dejar el resultado de la revisión en el gestor? Hace falta que el issue esté publicado
    /// (o vinculado) y que el gestor sepa comentar.
    bool canPublishResult(const Issue& issue) const;
    /// Comenta en el issue del gestor el resultado de la revisión y le adjunta el acta, si se pasa.
    /// Lo guarda en la ronda `revision` (0 = la última); un envío cortado la deja «sin confirmar», como
    /// la publicación.
    void publishResult(const QString& issueId, const QString& comment, const QString& documentPath,
                       std::function<void(const Result&)> done, int revision = 0);

    /// ¿Se pueden colgar del issue del gestor los bugs y los Tests de sus pruebas? Hace falta que el
    /// gestor sepa enlazar issues (sólo Jira) y que el issue esté publicado.
    bool canLinkIssues(const Issue& issue) const;
    /// Enlaza esos issues del gestor (bugs, Tests de Zephyr…) al issue publicado, uno a uno y sin
    /// parar en el primero que falle: devuelve cuántos quedaron enlazados y qué no se pudo.
    struct LinkResult {
        int linked = 0;
        QStringList failed;   // "SHOP-143 · motivo"
    };
    void linkToIssue(const QString& issueId, const QStringList& keys, std::function<void(const LinkResult&)> done);

    /// Tipos de incidencia del proyecto de destino, para elegir con cuál se crea. Se piden una vez por
    /// gestor y proyecto; si no se pueden leer, la lista llega vacía y el tipo se escribe a mano.
    void fetchIssueTypes(std::function<void(const QStringList&)> done);
    /// Tipo con el que se crea si no se elige otro: el del último envío, «Tarea»/«Task» si el proyecto lo
    /// tiene, o el primero de la lista.
    static QString defaultIssueType(const QStringList& types);

private:
    QString metadataKey() const;
    void linkNext(const QString& key, QStringList pending, LinkResult acc, std::function<void(const LinkResult&)> done);

    std::shared_ptr<IIssueTracker> m_tracker;
    IssueStore& m_issues;
    SettingsStore& m_settings;
    QStringList m_issueTypes;
    QString m_issueTypesFor;   // "Jira|SHOP@https://…": para no reutilizarlos en otro destino
};

} // namespace qaflow
