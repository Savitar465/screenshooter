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

    /// Borrador prellenado con el caso seleccionado y un paso de la ejecución: el que se pida en
    /// `stepIndex` (0-based) o, con -1, el fallo o bloqueo que haya visto la ejecución en curso.
    /// Un paso bloqueado trae la severidad en "Bloqueante".
    BugReport draftFromCurrentContext(int stepIndex = -1) const;

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

    struct ImportResult {
        bool ok = false;
        int imported = 0;    // bugs que el libro no conocía y ahora tiene
        int updated = 0;     // bugs ya conocidos, con el título y el estado de hoy
        int total = 0;       // cuántos tiene el gestor en total, quepan en esta página o no
        int nextStart = -1;  // desde dónde pedir la página siguiente; -1 = no queda nada
        QString error;

        bool hasMore() const { return nextStart >= 0; }
    };
    /// Bugs que trae cada página (y cada llamada al gestor).
    static constexpr int kImportPage = 50;
    /// ¿Se pueden traer del gestor los bugs que creó QAflow? Depende del gestor configurado.
    bool canImportFromTracker() const;
    /// Trae del gestor una página de los bugs que QAflow creó en este proyecto y la pasa al libro:
    /// los que ya están se actualizan y los que no (reportados desde otro equipo) se añaden. Nada se
    /// borra: un bug que el gestor ya no devuelva se queda como está. `startAt` 0 empieza por el más
    /// reciente; el `nextStart` del resultado es lo que hay que pasar para seguir.
    void importFromTracker(int startAt, std::function<void(const ImportResult&)> done);

    /// ¿Sabe el gestor configurado cerrar bugs desde QAflow? Sólo Jira, por ahora.
    bool canCloseBugs() const;
    struct CloseResult {
        QStringList closed;      // claves que quedaron cerradas (o ya lo estaban)
        QStringList failed;      // «CLAVE: motivo» de las que no se pudieron cerrar
        QStringList uncertain;   // claves cuyo cierre se cortó sin respuesta: pueden haberse cerrado
    };
    /// Cierra esos bugs en el gestor, uno tras otro, con la transición que ofrezca su flujo, y deja en
    /// el libro el estado con el que quedaron. Uno que falle no para a los demás. Nada se cierra solo:
    /// esto lo pide quien da los bugs por corregidos.
    void closeBugs(const QStringList& keys, std::function<void(const CloseResult&)> done);

    void testConnection(std::function<void(const ConnectionResult&)> done);

    /// Metadatos del proyecto (tipos, prioridades, componentes, versiones, asignables). Se cachean
    /// por gestor+proyecto; `force` vuelve a pedirlos.
    void loadMetadata(bool force, std::function<void(const MetadataResult&)> done);
    const ProjectMetadata& metadata() const { return m_metadata; }
    bool hasMetadata() const { return !m_metadata.isEmpty(); }

    /// Personas para el campo "Asignado a". Con Jira se pregunta al servidor, que conoce a todo el
    /// mundo y no sólo a los primeros del proyecto; con el resto de gestores se filtra en local la
    /// lista que trajo `loadMetadata()`. `query` es lo que se lleva escrito.
    void searchAssignees(const QString& query, std::function<void(const AssigneeSearch&)> done);
    /// ¿Las personas se buscan en el servidor del gestor o se filtran las ya cargadas?
    bool searchesAssigneesOnServer() const;

    /// ¿Se puede elegir el proyecto del gestor configurado de una lista? Sólo con Jira.
    bool canListProjects() const;
    /// Proyectos que ve el usuario en el gestor configurado, para elegir el del proyecto de QAflow.
    void fetchProjects(std::function<void(const TrackerProjectList&)> done);

signals:
    void metadataChanged();

private:
    IssueLink linkFor(const BugReport& bug, const IssueResult& r) const;
    void retryNext(QList<QString> ids, RetryResult acc, std::function<void(const RetryResult&)> done);
    void refreshNext(QStringList keys, RefreshResult acc, std::function<void(const RefreshResult&)> done);
    void closeNext(QStringList keys, CloseResult acc, std::function<void(const CloseResult&)> done);

    std::shared_ptr<IIssueTracker> m_tracker;
    TestCaseStore& m_cases;
    RunController& m_run;
    SettingsStore& m_settings;
    BugStore& m_bugs;
    ProjectMetadata m_metadata;
    QString m_metadataFor;   // "Jira|SHOP@https://…": para invalidar la caché al cambiar de proyecto
};

} // namespace qaflow
