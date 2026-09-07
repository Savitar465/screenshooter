#pragma once

#include "core/models/TestCase.h"
#include "core/models/TestRun.h"
#include "core/services/ITestCaseRepository.h"

#include <QObject>
#include <QString>
#include <QTimer>
#include <memory>
#include <optional>

namespace qaflow {

/// Fuente de verdad de los casos de prueba. Toda mutación pasa por aquí y emite señales.
///
/// Las operaciones destructivas (borrar caso, paso o captura) guardan una instantánea que
/// `undo()` restaura mientras no haya otra mutación. Los ficheros de capturas afectados no se
/// tocan hasta que la posibilidad de deshacer expira: entonces se emite `filesReleased()` y
/// quien gestiona el disco (EvidenceService) los elimina.
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
    /// Suites en uso, ordenadas. Una suite existe mientras algún caso la use.
    QStringList suites() const;
    /// Etiquetas en uso, ordenadas.
    QStringList tags() const;

    // Selección actual (caso abierto en el editor)
    QString selectedId() const { return m_selectedId; }
    const TestCase* selected() const { return find(m_selectedId); }
    void select(const QString& id);

    // Mutaciones de caso
    QString createCase();
    /// Copia sin capturas ni resultado de ejecución, en estado Borrador. Devuelve el id nuevo.
    QString duplicateCase(const QString& id);
    void removeCase(const QString& id);
    void updateCase(const QString& id, const std::function<void(TestCase&)>& mutate);
    /// Sustituye (mismo id) o añade los casos dados. Devuelve {añadidos, actualizados}.
    std::pair<int, int> mergeCases(const QList<TestCase>& incoming);
    QString nextCaseId() const;

    // Pasos
    void addStep(const QString& id);
    /// Inserta un paso vacío en la posición `index` (0..n). Las capturas asignadas se desplazan.
    void insertStep(const QString& id, int index);
    void removeStep(const QString& id, int index);
    /// Mueve el paso `index` a `index + delta` (las capturas asignadas siguen a su paso).
    void moveStep(const QString& id, int index, int delta);
    void updateStep(const QString& id, int index, const std::function<void(TestStep&)>& mutate);
    void recordOutcome(const QString& id, RunOutcome outcome);

    // Evidencias
    void addShot(const QString& id, const Screenshot& shot);
    void removeShot(const QString& id, int shotId);
    void assignShotStep(const QString& id, int shotId, int step);
    void moveShot(const QString& id, int shotId, int delta);
    void sortShotsByStep(const QString& id);
    int nextShotSequence();

    // Deshacer (un nivel, sólo tras una operación destructiva)
    bool canUndo() const { return m_undo.has_value(); }
    QString undoLabel() const { return m_undo ? m_undo->label : QString(); }
    /// Restaura la instantánea previa a la última operación destructiva. Devuelve false si no había.
    bool undo();
    /// Descarta la posibilidad de deshacer y libera los ficheros pendientes.
    void commitUndo();

    int executedCount() const;

signals:
    void casesChanged();                  // lista completa (alta/baja/reorden)
    void caseChanged(const QString& id);  // un caso concreto
    void selectionChanged(const QString& id);
    /// Se ha borrado algo con posibilidad de deshacer (`label` describe qué).
    void undoAvailable(const QString& label);
    /// Ficheros de capturas que ya no referencia ningún caso y pueden borrarse del disco.
    void filesReleased(const QStringList& paths);

private:
    struct UndoEntry {
        QString label;
        QList<TestCase> snapshot;
        QString selectedId;
        QStringList releasedFiles;
    };

    void touch(const QString& id);
    void scheduleSave();
    void pushUndo(const QString& label, const QStringList& releasedFiles = {});

    std::shared_ptr<ITestCaseRepository> m_repo;
    QList<TestCase> m_cases;
    QString m_selectedId;
    int m_shotSeq = 0;
    QTimer m_saveTimer;
    bool m_dirty = false;
    std::optional<UndoEntry> m_undo;
    QTimer m_undoTimer;
    bool m_applyingDestructive = false;
};

} // namespace qaflow
