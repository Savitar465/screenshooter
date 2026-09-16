#pragma once

#include "application/IssuePublishService.h"   // IssueDraft
#include "core/models/Issue.h"
#include "core/models/PlanReport.h"

#include <QObject>
#include <QStringList>
#include <functional>

namespace qaflow {

class IssuePublishService;
class IssueStore;
class QualityRecordService;
class RequirementSourceService;
class RunHistoryStore;
class TestPublishService;

/// Publicar el resultado de una revisión: lo que se hace, de una vez y en orden, cuando el control de
/// calidad de un requerimiento termina.
///
/// Son tres destinos y cada uno es opcional:
///  1. **Zephyr**: los ciclos de los planes del issue, con sus casos y sus evidencias (los ya
///     publicados se actualizan, no se duplican). De aquí salen los enlaces que llevan los otros dos.
///  2. **El gestor (Jira)**: un comentario en el issue con el resultado, los enlaces de Zephyr y el
///     acta adjunta. El issue ya está en el gestor desde que se importó el requerimiento, así que
///     aquí sólo se comenta.
///  3. **GESREQ**: el registro del resultado en el requerimiento, con su acta.
///
/// Nada se manda solo: quien publica elige los pasos y ve antes qué va a cada sitio. Los pasos se
/// ejecutan en ese orden y uno que falle no impide los demás (se informa de cada uno). Un envío
/// cortado sin respuesta queda «sin confirmar», como en el resto de la aplicación.
class RevisionPublishService : public QObject {
    Q_OBJECT
public:
    RevisionPublishService(IssueStore& issues, RunHistoryStore& history, QualityRecordService& records,
                           TestPublishService* zephyr, IssuePublishService* tracker,
                           RequirementSourceService* requirements, QObject* parent = nullptr);

    enum class Destination { Zephyr, Tracker, Requirement };

    /// Un destino de la publicación, tal y como se le enseña a quien va a publicar.
    struct Step {
        Destination destination = Destination::Zephyr;
        bool available = false;       // se puede hacer ahora (configurado y con algo que mandar)
        bool done = false;            // ya se hizo en esta revisión
        QString target;               // "Zephyr · 2 ciclos", "Jira · SUMA2-2907", "GESREQ · GREQ 2026997"
        QString detail;               // qué se haría, o qué se hizo ya
        QString blocked;              // por qué no se puede; vacío si se puede
        /// Lo bloquea una **regla del destino** (GESREQ no acepta ese resultado, falta el acta…), no la
        /// configuración: cambiar el resultado o generar el acta puede desbloquearlo, así que la
        /// pantalla vuelve a preguntar (`requirementProblem`) cuando algo de eso cambia.
        bool rule = false;
    };
    /// Qué se puede publicar de la revisión del issue y cómo está cada destino.
    QList<Step> stepsFor(const QString& issueId) const;
    /// Ciclos de la revisión que se publicarían en Zephyr (los del acta), el más reciente primero.
    QList<PlanReport> cyclesFor(const QString& issueId) const;
    /// El registro que se mandaría a GESREQ con ese resultado y esa acta: lo que se enseña y lo que se
    /// envía salen de aquí, así que dicen lo mismo.
    RequirementRegistration registrationFor(const QString& issueId, QaOutcome outcome, const QString& documentPath) const;
    /// Por qué GESREQ rechazaría ese registro (falta el acta, el resultado no cuadra con las
    /// observaciones…); vacío si lo aceptaría. La pantalla lo consulta al cambiar el resultado o el
    /// acta, para avisar antes de enviar nada.
    QString requirementProblem(const QString& issueId, QaOutcome outcome, const QString& documentPath) const;

    struct Options {
        bool zephyr = true;
        bool tracker = true;
        bool requirement = true;
        QaOutcome outcome = QaOutcome::Conforme;
        QString comment;          // el resumen que va al gestor y a GESREQ
        QString documentPath;     // acta a adjuntar; vacía = sin adjunto
    };

    /// Cómo terminó un destino.
    struct Outcome {
        Destination destination = Destination::Zephyr;
        bool ok = false;
        bool uncertain = false;   // el envío se cortó: puede haber quedado hecho
        QString message;          // qué pasó, para contarlo en la pantalla
    };

    struct Result {
        bool ok = false;          // todos los pasos elegidos salieron bien
        QList<Outcome> steps;
    };

    /// Publica lo elegido, en orden. `progress` se llama al terminar cada paso (también si falla) y
    /// `done` al final con todo lo que pasó.
    void publish(const QString& issueId, const Options& options, std::function<void(const Outcome&)> progress,
                 std::function<void(const Result&)> done);

    /// Nombre para mostrar de un destino ("Zephyr", "el gestor", "GESREQ").
    static QString label(Destination destination);

private:
    struct Run;   // el estado de una publicación en curso (vive hasta que termina el último paso)

    void runZephyr(const std::shared_ptr<Run>& run);
    void runTracker(const std::shared_ptr<Run>& run);
    /// Cuelga del issue del gestor lo que salió de sus pruebas: los bugs de la revisión y los Tests de
    /// Zephyr de sus ejecuciones. Así, desde el issue del requerimiento se llega a todo.
    void linkEvidence(const std::shared_ptr<Run>& run, const QString& key);
    void runRequirement(const std::shared_ptr<Run>& run);
    /// Por qué el control ya no se puede registrar en GESREQ (esta ronda ya se registró, o el control se
    /// cerró como Conforme); vacío si todavía se puede. Registrar cambia el estado del requerimiento en
    /// el sistema, así que se hace una sola vez por ronda y sólo se repite cuando quedó observado.
    QString alreadyRegistered(const Issue& issue) const;
    void finish(const std::shared_ptr<Run>& run, const Outcome& outcome);

    IssueStore& m_issues;
    RunHistoryStore& m_history;
    QualityRecordService& m_records;
    TestPublishService* m_zephyr;
    IssuePublishService* m_tracker;
    RequirementSourceService* m_requirements;
};

} // namespace qaflow
