#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace qaflow {

enum class Priority { Alta, Media, Baja };
enum class CaseStatus { Listo, Borrador, Obsoleto };
enum class RunOutcome { None, Passed, Failed, Blocked };

/// Valores canónicos (se persisten y viajan en JSON/CSV): "Alta", "Listo"… No traducir.
QString toString(Priority p);
QString toString(CaseStatus s);
Priority priorityFromString(const QString& s);
CaseStatus statusFromString(const QString& s);
/// Texto para mostrar en el idioma de la interfaz.
QString label(Priority p);
QString label(CaseStatus s);
QString label(RunOutcome o);

struct TestStep {
    QString action;
    QString expected;

    bool isComplete() const { return !action.trimmed().isEmpty() && !expected.trimmed().isEmpty(); }
};

/// Evidencia de una ejecución: una captura (cap_004.png), una grabación (rec_002.gif) o un fichero
/// adjuntado desde el disco (adj_005_servidor.log). Todo vive en la carpeta de capturas.
///
/// Se captura ejecutando el caso, así que pertenece a una ejecución concreta y no al caso: `runId`
/// dice a cuál (R-0007). Mientras la ejecución está en curso todavía no tiene id —lo asigna el
/// historial al archivarla—, y por eso `runId` está vacío hasta ese momento.
struct Screenshot {
    int id = 0;
    int step = 0;          // 0 = sin asignar, 1..N = paso
    QString fileName;      // cap_004.png
    QString path;          // ruta absoluta en disco
    QString runId;         // ejecución a la que pertenece; vacío = la que está en curso

    /// PNG, JPG, WebP, GIF, BMP: se muestra como miniatura y se puede anotar (salvo GIF).
    bool isImage() const;
    /// GIF animado (grabación): se ve como imagen pero no se anota.
    bool isAnimation() const;
    /// Extensión en minúsculas, sin punto ("png", "log", "mp4").
    QString extension() const;
};

struct LastRun {
    RunOutcome outcome = RunOutcome::None;
    QDateTime at;

    /// "Pasó · hace 2 d", "Falló · ayer", "Bloqueado · hace 3 h", "Sin ejecutar"
    QString label(const QDateTime& now = QDateTime::currentDateTime()) const;
};

struct TestCase {
    QString id;                 // TC-104
    QString title;
    QString suite;
    Priority priority = Priority::Media;
    CaseStatus status = CaseStatus::Borrador;
    LastRun lastRun;
    QString preconditions;
    QList<TestStep> steps;
    QList<Screenshot> shots;
    /// Evidencias de todas sus ejecuciones, cada una con la suya en `Screenshot::runId`. El caso las
    /// guarda —son ficheros con un ciclo de vida (anotar, borrar, deshacer)—, pero se enseñan y se
    /// publican por ejecución, nunca como un álbum del caso.
    QStringList tags;           // etiquetas libres: "regresión", "smoke"…
    QString component;          // módulo o componente del producto
    QString jiraKey;            // historia o épica enlazada: SHOP-12
    /// Issue de tipo Test que representa el caso en Zephyr (SHOP-42). El caso es reutilizable —se
    /// ejecuta muchas veces y en varios planes—, así que su Test es siempre el mismo: se enlaza a
    /// mano o lo estrena QAflow al publicar el primer ciclo, y desde ahí se reutiliza.
    QString testKey;

    /// Evidencias de una ejecución concreta; con `runId` vacío, las de la ejecución en curso.
    QList<Screenshot> shotsOfRun(const QString& runId) const;
    /// Las de la ejecución más reciente: la que está en curso si la hay, si no la última archivada.
    QList<Screenshot> latestEvidence() const;
    /// Id de la ejecución de `latestEvidence()` (vacío si son las de la ejecución en curso).
    QString latestEvidenceRunId() const;
    /// Sin paso asignado, dentro de la ejecución en curso.
    int unassignedShots() const;
    bool readyToBeMarkedListo() const;
    /// Texto en el que buscan los filtros (id, título, suite, etiquetas, componente, historia, Test).
    QString searchText() const;
};

/// "a, b ,c" → ["a", "b", "c"] sin vacíos ni duplicados.
QStringList parseTags(const QString& text);

} // namespace qaflow
