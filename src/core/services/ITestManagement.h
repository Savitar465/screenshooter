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
    QString testKey;     // Test que representa el caso en Jira (SHOP-42); vacío = todavía hay que crearlo
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
    QString cycleName;       // "Regresión Sprint 14 · 12/05/2026"
    QString versionName;     // versión del proyecto; vacío = sin programar
    QString description;
    QDateTime startedAt;
    QDateTime finishedAt;
    QList<PublishCase> cases;
};

/// Resultado de crear el Test de un caso, se pida desde el editor del caso o al publicar un ciclo.
struct CreateTestResult {
    bool ok = false;
    QString key;             // clave del Test creado (SHOP-77)
    QStringList skipped;     // pasos que no entraron en el Test, con su motivo
    QString error;
    bool retryable = false;  // fallo de red o 5xx: merece la pena reintentar
};

struct PublishResult {
    bool ok = false;
    QString cycleId;
    int executions = 0;      // casos publicados
    int steps = 0;           // pasos con veredicto
    int attachments = 0;     // evidencias subidas
    int testsCreated = 0;    // Tests creados en Jira a partir de casos de QAflow
    /// Clave del Test creado para cada caso (TC-104 → SHOP-77), para enlazarla al caso y reutilizar
    /// ese mismo Test en los ciclos siguientes.
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
    /// Crea el ciclo con sus ejecuciones, veredictos por paso y evidencias. Al caso que todavía no
    /// está enlazado a un Test se le crea antes a partir de él, y su clave vuelve en
    /// `PublishResult::createdTests` para que el caso la guarde y la reutilice.
    virtual void publish(const TrackerSettings& s, const PublishRequest& request, std::function<void(const PublishResult&)> done) = 0;
    /// Crea el Test de un caso suelto (su título, precondiciones y pasos), sin ciclo de por medio:
    /// es lo que pide el editor del caso.
    virtual void createTest(const TrackerSettings& s, const PublishCase& c, std::function<void(const CreateTestResult&)> done) = 0;
};

} // namespace qaflow
