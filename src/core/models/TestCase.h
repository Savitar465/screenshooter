#pragma once

#include <QDateTime>
#include <QList>
#include <QString>

namespace qaflow {

enum class Priority { Alta, Media, Baja };
enum class CaseStatus { Listo, Borrador, Obsoleto };
enum class RunOutcome { None, Passed, Failed, Blocked };

QString toString(Priority p);
QString toString(CaseStatus s);
Priority priorityFromString(const QString& s);
CaseStatus statusFromString(const QString& s);

struct TestStep {
    QString action;
    QString expected;

    bool isComplete() const { return !action.trimmed().isEmpty() && !expected.trimmed().isEmpty(); }
};

struct Screenshot {
    int id = 0;
    int step = 0;          // 0 = sin asignar, 1..N = paso
    QString fileName;      // cap_004.png
    QString path;          // ruta absoluta en disco
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

    int unassignedShots() const;
    bool readyToBeMarkedListo() const;
};

} // namespace qaflow
