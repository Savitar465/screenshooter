#pragma once

#include "core/models/BugReport.h"
#include "core/models/IssueLink.h"
#include "core/services/IIssueTracker.h"

#include <QObject>
#include <memory>

namespace qaflow {

class TestCaseStore;
class RunController;
class SettingsStore;
class BugStore;

/// Construye el borrador de bug a partir de la ejecución, lo envía al gestor configurado y
/// mantiene el libro de bugs: enlaza el issue creado con su caso, encola lo que no se pudo
/// enviar y consulta estados.
class BugReportService : public QObject {
    Q_OBJECT
public:
    BugReportService(std::shared_ptr<IIssueTracker> tracker, TestCaseStore& cases, RunController& run,
                     SettingsStore& settings, BugStore& bugs, QObject* parent = nullptr);

    /// Borrador prellenado con el caso seleccionado y, si existe, el primer paso fallido de la ejecución.
    BugReport draftFromCurrentContext() const;

    struct SubmitResult {
        bool ok = false;         // creado en el gestor
        bool queued = false;     // no se pudo enviar; espera en la cola
        QString key;
        QString url;
        QString error;
        int attachmentsUploaded = 0;
    };
    /// Envía el bug. Si el fallo es de red o del servidor, lo deja en la cola de pendientes.
    void submit(const BugReport& bug, std::function<void(const SubmitResult&)> done);

    struct RetryResult {
        int sent = 0;
        int failed = 0;
        QStringList keys;
    };
    /// Reintenta todos los pendientes en orden. Se detiene al primer fallo de red.
    void retryPending(std::function<void(const RetryResult&)> done);
    void discardPending(const QString& id);

    struct RefreshResult {
        int updated = 0;
        int failed = 0;
    };
    /// Consulta en el gestor el estado de todos los issues (o sólo los no resueltos).
    void refreshStatuses(bool onlyOpen, std::function<void(const RefreshResult&)> done);

    void testConnection(std::function<void(const ConnectionResult&)> done);

    /// Metadatos del proyecto (tipos, prioridades, componentes, versiones, asignables). Se cachean
    /// por gestor+proyecto; `force` vuelve a pedirlos.
    void loadMetadata(bool force, std::function<void(const MetadataResult&)> done);
    const ProjectMetadata& metadata() const { return m_metadata; }
    bool hasMetadata() const { return !m_metadata.isEmpty(); }

signals:
    void metadataChanged();

private:
    IssueLink linkFor(const BugReport& bug, const IssueResult& r) const;
    void retryNext(QList<QString> ids, RetryResult acc, std::function<void(const RetryResult&)> done);
    void refreshNext(QStringList keys, RefreshResult acc, std::function<void(const RefreshResult&)> done);

    std::shared_ptr<IIssueTracker> m_tracker;
    TestCaseStore& m_cases;
    RunController& m_run;
    SettingsStore& m_settings;
    BugStore& m_bugs;
    ProjectMetadata m_metadata;
    QString m_metadataFor;   // "Jira|SHOP@https://…": para invalidar la caché al cambiar de proyecto
};

} // namespace qaflow
