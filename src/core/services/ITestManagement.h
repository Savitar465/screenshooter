#pragma once

#include "core/models/RunHistory.h"
#include "core/models/Settings.h"
#include "core/models/TestCase.h"
#include "core/services/IIssueTracker.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>

namespace qaflow {

/// Una evidencia que acompaña a un caso publicado: el fichero y a qué paso pertenece.
struct PublishAttachment {
    QString path;
    int step = 0;      // 0 = del caso entero; 1..N = de ese paso
};

/// Un caso ejecutado, listo para publicar en la herramienta de gestión de pruebas. Lleva además el
/// caso tal y como está escrito (precondiciones y pasos), que es con lo que se crea su Test la
/// primera vez, cuando el caso todavía no está enlazado a ninguno.
struct PublishCase {
    QString caseId;      // TC-104, para los mensajes de la aplicación
    QString runId;       // R-0007: la ejecución que se publica, dueña del Test que se cree para ella
    QString testKey;     // Test de esta ejecución en Jira (SHOP-42), si ya se publicó antes; vacío = hay que crearlo
    QString title;
    QString preconditions;           // precondiciones del caso, para la descripción del Test
    QList<TestStep> design;          // pasos del caso (acción y resultado esperado) con los que se crea el Test
    Verdict verdict = Verdict::Superado;
    QList<RunRecordStep> steps;      // los pasos tal y como se ejecutaron
    QList<PublishAttachment> attachments;
    qint64 durationSecs = 0;
};

/// Un ciclo de plan terminado: lo que QAflow publica de una vez.
struct PublishRequest {
    /// Ciclo de Zephyr que se actualiza (el informe ya se publicó en él): se reutilizan sus
    /// ejecuciones, se fijan los veredictos de nuevo y se suben sólo las evidencias que falten.
    /// Vacío = crear un ciclo nuevo.
    QString cycleId;
    QString cycleName;       // "Regresión Sprint 14 · 12/05/2026"
    QString versionName;     // versión del proyecto; vacío = sin programar
    QString description;
    QDateTime startedAt;
    QDateTime finishedAt;
    QList<PublishCase> cases;
};

struct PublishResult {
    bool ok = false;
    QString cycleId;
    int executions = 0;      // casos publicados
    int steps = 0;           // pasos con veredicto
    int attachments = 0;     // evidencias subidas
    int testsCreated = 0;    // Tests creados en Jira a partir de casos de QAflow
    /// Clave del Test creado para cada caso (TC-104 → SHOP-77): es de la ejecución publicada, que la
    /// guarda para reutilizarlo si ese mismo informe se vuelve a publicar.
    QHash<QString, QString> createdTests;
    /// Lo que no se pudo publicar, con el motivo ("TC-103: no se pudo crear el Test · …").
    QStringList skipped;
    QString error;
    bool retryable = false;  // fallo de red o 5xx: merece la pena reintentar
};

/// Herramienta de gestión de pruebas (Zephyr for Jira). Es otra responsabilidad distinta de la de
/// `IIssueTracker`: aquélla crea defectos, ésta publica el resultado de un ciclo de pruebas.
/// Asíncrona: las llamadas devuelven por callback en el hilo principal.
class ITestManagement {
public:
    virtual ~ITestManagement() = default;
    /// Comprueba que la API responde con estos ajustes y devuelve por qué ruta lo hace.
    virtual void testConnection(const TrackerSettings& s, std::function<void(const ConnectionResult&)> done) = 0;
    /// Crea el ciclo con sus ejecuciones, veredictos por paso y evidencias. A la ejecución que
    /// todavía no tiene Test se le crea antes uno a partir del caso, y su clave vuelve en
    /// `PublishResult::createdTests` para que la ejecución la guarde.
    virtual void publish(const TrackerSettings& s, const PublishRequest& request, std::function<void(const PublishResult&)> done) = 0;

};

} // namespace qaflow
