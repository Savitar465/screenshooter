#pragma once

#include "core/models/TestCase.h"
#include "core/models/TestRun.h"
#include "core/services/ITestCaseRepository.h"

#include <QObject>
#include <QString>
#include <QTimer>
#include <memory>

namespace qaflow {

/// Fuente de verdad de los casos de prueba. Toda mutación pasa por aquí y emite señales.
class TestCaseStore : public QObject {
    Q_OBJECT
public:
    explicit TestCaseStore(std::shared_ptr<ITestCaseRepository> repo, QObject* parent = nullptr);

    ~TestCaseStore() override;

    void load();
    /// Escribe en disco inmediatamente (normalmente se usa el guardado diferido).
    bool save();

    const QList<TestCase>& cases() const { return m_cases; }
    const TestCase* find(const QString& id) const;
    TestCase* find(const QString& id);
    QStringList suites() const;

    // Selección actual (caso abierto en el editor)
    QString selectedId() const { return m_selectedId; }
    const TestCase* selected() const { return find(m_selectedId); }
    void select(const QString& id);

    // Mutaciones de caso
    QString createCase();
    void updateCase(const QString& id, const std::function<void(TestCase&)>& mutate);
    void addStep(const QString& id);
    void removeStep(const QString& id, int index);
    void updateStep(const QString& id, int index, const std::function<void(TestStep&)>& mutate);
    void recordOutcome(const QString& id, RunOutcome outcome);

    // Evidencias
    void addShot(const QString& id, const Screenshot& shot);
    void removeShot(const QString& id, int shotId);
    void assignShotStep(const QString& id, int shotId, int step);
    void moveShot(const QString& id, int shotId, int delta);
    void sortShotsByStep(const QString& id);
    int nextShotSequence();

    int executedCount() const;

signals:
    void casesChanged();                  // lista completa (alta/baja/reorden)
    void caseChanged(const QString& id);  // un caso concreto
    void selectionChanged(const QString& id);

private:
    void touch(const QString& id);
    void scheduleSave();

    std::shared_ptr<ITestCaseRepository> m_repo;
    QList<TestCase> m_cases;
    QString m_selectedId;
    int m_shotSeq = 0;
    QTimer m_saveTimer;
    bool m_dirty = false;
};

} // namespace qaflow
