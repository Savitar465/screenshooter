#include "TestCaseStore.h"

#include "application/SeedData.h"

#include <QSet>
#include <algorithm>

namespace qaflow {

namespace {
constexpr int kUndoWindowMs = 20000;

/// Renumera las capturas asignadas a pasos según una función índice antiguo → nuevo (0 = sin asignar).
template <typename F>
void remapShotSteps(TestCase& c, F newStepFor) {
    for (auto& s : c.shots) if (s.step > 0) s.step = newStepFor(s.step);
}
} // namespace

TestCaseStore::TestCaseStore(std::shared_ptr<ITestCaseRepository> repo, QObject* parent)
    : QObject(parent), m_repo(std::move(repo)) {
    // Las ediciones de texto llegan tecla a tecla; agrupamos las escrituras a disco.
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, [this]() { save(); });
    m_undoTimer.setSingleShot(true);
    m_undoTimer.setInterval(kUndoWindowMs);
    connect(&m_undoTimer, &QTimer::timeout, this, &TestCaseStore::commitUndo);
}

TestCaseStore::~TestCaseStore() {
    commitUndo();
    if (m_dirty) save();
}

void TestCaseStore::load() {
    auto loaded = m_repo ? m_repo->loadCases() : std::nullopt;
    m_cases = loaded ? *loaded : seed::sampleCases();
    for (const auto& c : m_cases)
        for (const auto& s : c.shots) m_shotSeq = std::max(m_shotSeq, s.id);
    if (!m_cases.isEmpty()) m_selectedId = seed::defaultSelection(m_cases);
    emit casesChanged();
    emit selectionChanged(m_selectedId);
}

bool TestCaseStore::save() {
    m_saveTimer.stop();
    if (!m_repo) { m_dirty = false; return false; }
    if (!m_repo->saveCases(m_cases)) {
        m_dirty = true;   // se reintenta en el siguiente guardado
        emit saveFailed(tr("los casos de prueba"));
        return false;
    }
    m_dirty = false;
    return true;
}

void TestCaseStore::scheduleSave() {
    m_dirty = true;
    m_saveTimer.start();
}

const TestCase* TestCaseStore::find(const QString& id) const {
    auto it = std::find_if(m_cases.cbegin(), m_cases.cend(), [&](const TestCase& c) { return c.id == id; });
    return it == m_cases.cend() ? nullptr : &*it;
}

TestCase* TestCaseStore::find(const QString& id) {
    auto it = std::find_if(m_cases.begin(), m_cases.end(), [&](const TestCase& c) { return c.id == id; });
    return it == m_cases.end() ? nullptr : &*it;
}

QStringList TestCaseStore::suites() const {
    QStringList out;
    for (const auto& c : m_cases) if (!c.suite.trimmed().isEmpty() && !out.contains(c.suite)) out << c.suite;
    std::sort(out.begin(), out.end(), [](const QString& a, const QString& b) { return a.localeAwareCompare(b) < 0; });
    return out;
}

QStringList TestCaseStore::tags() const {
    QStringList out;
    for (const auto& c : m_cases) for (const auto& t : c.tags) if (!out.contains(t, Qt::CaseInsensitive)) out << t;
    std::sort(out.begin(), out.end(), [](const QString& a, const QString& b) { return a.localeAwareCompare(b) < 0; });
    return out;
}

void TestCaseStore::select(const QString& id) {
    if (m_selectedId == id || !find(id)) return;
    m_selectedId = id;
    emit selectionChanged(id);
}

QString TestCaseStore::nextCaseId() const {
    int maxNum = 100;
    for (const auto& c : m_cases) {
        bool ok = false;
        const int n = c.id.mid(3).toInt(&ok);
        if (ok) maxNum = std::max(maxNum, n);
    }
    return QStringLiteral("TC-%1").arg(maxNum + 1);
}

QString TestCaseStore::createCase() {
    commitUndo();
    TestCase c;
    c.id = nextCaseId();
    const TestCase* cur = selected();
    const QStringList existing = suites();
    c.suite = cur && !cur->suite.isEmpty() ? cur->suite : existing.isEmpty() ? QStringLiteral("General") : existing.first();
    c.priority = Priority::Media;
    c.status = CaseStatus::Borrador;
    c.steps.append(TestStep{});
    m_cases.append(c);
    scheduleSave();
    emit casesChanged();
    select(c.id);
    return c.id;
}

QString TestCaseStore::duplicateCase(const QString& id) {
    const TestCase* src = find(id);
    if (!src) return {};
    commitUndo();
    TestCase c = *src;
    c.id = nextCaseId();
    c.title = src->title.trimmed().isEmpty() ? QString() : src->title + tr(" (copia)");
    c.status = CaseStatus::Borrador;
    c.lastRun = LastRun{};
    c.shots.clear();
    // La copia es un caso nuevo: heredar la clave haría que dos casos publicaran sus ejecuciones
    // sobre el mismo Test de Zephyr. Se enlaza o se crea cuando toque.
    c.testKey.clear();
    // Justo después del original, para que se vea de dónde sale.
    const int pos = static_cast<int>(src - m_cases.constData()) + 1;
    m_cases.insert(pos, c);
    scheduleSave();
    emit casesChanged();
    select(c.id);
    return c.id;
}

void TestCaseStore::removeCase(const QString& id) {
    const TestCase* c = find(id);
    if (!c) return;
    QStringList files;
    for (const auto& s : c->shots) files << s.path;
    pushUndo(tr("%1 eliminado").arg(id), files);

    const int pos = static_cast<int>(c - m_cases.constData());
    m_cases.removeAt(pos);
    scheduleSave();
    if (m_selectedId == id) {
        m_selectedId.clear();
        if (!m_cases.isEmpty()) m_selectedId = m_cases[std::min(pos, static_cast<int>(m_cases.size()) - 1)].id;
    }
    emit casesChanged();
    emit selectionChanged(m_selectedId);
}

void TestCaseStore::updateCase(const QString& id, const std::function<void(TestCase&)>& mutate) {
    if (auto* c = find(id)) { mutate(*c); touch(id); }
}

std::pair<int, int> TestCaseStore::mergeCases(const QList<TestCase>& incoming) {
    commitUndo();
    int added = 0, updated = 0;
    for (const auto& in : incoming) {
        if (TestCase* existing = find(in.id)) {
            // Lo local que no viaja en el intercambio se conserva.
            TestCase merged = in;
            merged.shots = existing->shots;
            merged.lastRun = existing->lastRun;
            *existing = merged;
            ++updated;
        } else {
            TestCase fresh = in;
            fresh.shots.clear();
            m_cases.append(fresh);
            ++added;
        }
    }
    if (added || updated) {
        scheduleSave();
        emit casesChanged();
        if (m_selectedId.isEmpty() && !m_cases.isEmpty()) select(m_cases.first().id);
    }
    return {added, updated};
}

void TestCaseStore::addStep(const QString& id) {
    updateCase(id, [](TestCase& c) { c.steps.append(TestStep{}); });
}

void TestCaseStore::insertStep(const QString& id, int index) {
    updateCase(id, [index](TestCase& c) {
        const int at = std::clamp(index, 0, static_cast<int>(c.steps.size()));
        c.steps.insert(at, TestStep{});
        remapShotSteps(c, [at](int step) { return step > at ? step + 1 : step; });
    });
}

void TestCaseStore::removeStep(const QString& id, int index) {
    const TestCase* c = find(id);
    if (!c || index < 0 || index >= c->steps.size()) return;
    pushUndo(tr("Paso %1 de %2 eliminado").arg(index + 1).arg(id));
    m_applyingDestructive = true;
    updateCase(id, [index](TestCase& tc) {
        tc.steps.removeAt(index);
        remapShotSteps(tc, [index](int step) { return step == index + 1 ? 0 : step > index + 1 ? step - 1 : step; });
    });
    m_applyingDestructive = false;
}

void TestCaseStore::moveStep(const QString& id, int index, int delta) {
    updateCase(id, [index, delta](TestCase& c) {
        const int j = index + delta;
        if (index < 0 || index >= c.steps.size() || j < 0 || j >= c.steps.size() || j == index) return;
        c.steps.move(index, j);
        // Las capturas siguen a su paso: los pasos entre medias se desplazan una posición.
        const int from = index + 1, to = j + 1;
        remapShotSteps(c, [from, to](int step) {
            if (step == from) return to;
            if (from < to && step > from && step <= to) return step - 1;
            if (from > to && step >= to && step < from) return step + 1;
            return step;
        });
    });
}

void TestCaseStore::updateStep(const QString& id, int index, const std::function<void(TestStep&)>& mutate) {
    updateCase(id, [&](TestCase& c) { if (index >= 0 && index < c.steps.size()) mutate(c.steps[index]); });
}

void TestCaseStore::recordOutcome(const QString& id, RunOutcome outcome) {
    updateCase(id, [outcome](TestCase& c) { c.lastRun = LastRun{outcome, QDateTime::currentDateTime()}; });
}

void TestCaseStore::addShot(const QString& id, const Screenshot& shot) {
    updateCase(id, [&](TestCase& c) { c.shots.append(shot); });
}

void TestCaseStore::sealShots(const QString& caseId, const QString& runId) {
    if (runId.isEmpty()) return;
    const TestCase* c = find(caseId);
    if (!c) return;
    bool any = false;
    for (const auto& s : c->shots) if (s.runId.isEmpty()) { any = true; break; }
    if (!any) return;   // sin evidencias sueltas no hay nada que sellar (ni que guardar)
    updateCase(caseId, [&runId](TestCase& tc) {
        for (auto& s : tc.shots) if (s.runId.isEmpty()) s.runId = runId;
    });
}

int TestCaseStore::adoptLooseShots(const QString& caseId, const QString& runId) {
    const TestCase* c = find(caseId);
    if (!c) return 0;
    int loose = 0;
    for (const auto& s : c->shots) if (s.runId.isEmpty()) ++loose;
    if (loose == 0) return 0;
    if (!runId.isEmpty()) { sealShots(caseId, runId); return loose; }
    // Un caso que nunca se ejecutó: esas evidencias no tienen ejecución en la que enseñarse. Se
    // sueltan de la lista sin tocar los ficheros, que siguen en la carpeta de capturas.
    updateCase(caseId, [](TestCase& tc) {
        tc.shots.erase(std::remove_if(tc.shots.begin(), tc.shots.end(), [](const Screenshot& s) { return s.runId.isEmpty(); }),
                       tc.shots.end());
    });
    return loose;
}

void TestCaseStore::removeShot(const QString& id, int shotId) {
    const TestCase* c = find(id);
    if (!c) return;
    auto it = std::find_if(c->shots.cbegin(), c->shots.cend(), [&](const Screenshot& s) { return s.id == shotId; });
    if (it == c->shots.cend()) return;
    pushUndo(tr("Captura %1 eliminada").arg(it->fileName), {it->path});
    m_applyingDestructive = true;
    updateCase(id, [shotId](TestCase& tc) {
        tc.shots.erase(std::remove_if(tc.shots.begin(), tc.shots.end(), [&](const Screenshot& s) { return s.id == shotId; }), tc.shots.end());
    });
    m_applyingDestructive = false;
}

void TestCaseStore::assignShotStep(const QString& id, int shotId, int step) {
    updateCase(id, [&](TestCase& c) { for (auto& s : c.shots) if (s.id == shotId) s.step = step; });
}

void TestCaseStore::moveShot(const QString& id, int shotId, int delta) {
    updateCase(id, [&](TestCase& c) {
        for (int i = 0; i < c.shots.size(); ++i) {
            if (c.shots[i].id != shotId) continue;
            const int j = i + delta;
            if (j >= 0 && j < c.shots.size()) c.shots.swapItemsAt(i, j);
            return;
        }
    });
}

void TestCaseStore::sortShotsByStep(const QString& id) {
    updateCase(id, [](TestCase& c) {
        std::stable_sort(c.shots.begin(), c.shots.end(), [](const Screenshot& a, const Screenshot& b) {
            const int sa = a.step == 0 ? 99 : a.step, sb = b.step == 0 ? 99 : b.step;
            return sa < sb;
        });
    });
}

int TestCaseStore::nextShotSequence() { return ++m_shotSeq; }

// ---- Deshacer ------------------------------------------------------------------------------

void TestCaseStore::pushUndo(const QString& label, const QStringList& releasedFiles) {
    commitUndo();
    m_undo = UndoEntry{label, m_cases, m_selectedId, releasedFiles};
    m_undoTimer.start();
    emit undoAvailable(label);
}

bool TestCaseStore::undo() {
    if (!m_undo) return false;
    m_undoTimer.stop();
    UndoEntry e = std::move(*m_undo);
    m_undo.reset();
    m_cases = e.snapshot;
    m_selectedId = find(e.selectedId) ? e.selectedId : (m_cases.isEmpty() ? QString() : m_cases.first().id);
    scheduleSave();
    emit casesChanged();
    emit selectionChanged(m_selectedId);
    return true;
}

void TestCaseStore::commitUndo() {
    if (!m_undo) return;
    m_undoTimer.stop();
    QStringList files;
    for (const auto& f : m_undo->releasedFiles) if (!f.isEmpty()) files << f;
    m_undo.reset();
    if (!files.isEmpty()) emit filesReleased(files);
}

int TestCaseStore::executedCount() const {
    return static_cast<int>(std::count_if(m_cases.cbegin(), m_cases.cend(),
        [](const TestCase& c) { return c.lastRun.outcome != RunOutcome::None; }));
}

void TestCaseStore::touch(const QString& id) {
    // Cualquier edición posterior invalida el deshacer: sólo cubre la última operación destructiva.
    if (!m_applyingDestructive) commitUndo();
    scheduleSave();
    emit caseChanged(id);
}

} // namespace qaflow
