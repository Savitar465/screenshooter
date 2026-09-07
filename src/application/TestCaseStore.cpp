#include "TestCaseStore.h"

#include "application/SeedData.h"

#include <QSet>
#include <algorithm>

namespace qaflow {

TestCaseStore::TestCaseStore(std::shared_ptr<ITestCaseRepository> repo, QObject* parent)
    : QObject(parent), m_repo(std::move(repo)) {
    // Las ediciones de texto llegan tecla a tecla; agrupamos las escrituras a disco.
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, [this]() { save(); });
}

TestCaseStore::~TestCaseStore() {
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
    m_dirty = false;
    return m_repo && m_repo->saveCases(m_cases);
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
    QStringList out = seed::defaultSuites();
    for (const auto& c : m_cases) if (!out.contains(c.suite)) out << c.suite;
    return out;
}

void TestCaseStore::select(const QString& id) {
    if (m_selectedId == id || !find(id)) return;
    m_selectedId = id;
    emit selectionChanged(id);
}

QString TestCaseStore::createCase() {
    int maxNum = 100;
    for (const auto& c : m_cases) {
        bool ok = false;
        const int n = c.id.mid(3).toInt(&ok);
        if (ok) maxNum = std::max(maxNum, n);
    }
    TestCase c;
    c.id = QStringLiteral("TC-%1").arg(maxNum + 1);
    c.suite = seed::defaultSuites().first();
    c.priority = Priority::Media;
    c.status = CaseStatus::Borrador;
    c.steps.append(TestStep{});
    m_cases.append(c);
    scheduleSave();
    emit casesChanged();
    select(c.id);
    return c.id;
}

void TestCaseStore::updateCase(const QString& id, const std::function<void(TestCase&)>& mutate) {
    if (auto* c = find(id)) { mutate(*c); touch(id); }
}

void TestCaseStore::addStep(const QString& id) {
    updateCase(id, [](TestCase& c) { c.steps.append(TestStep{}); });
}

void TestCaseStore::removeStep(const QString& id, int index) {
    updateCase(id, [index](TestCase& c) {
        if (index < 0 || index >= c.steps.size()) return;
        c.steps.removeAt(index);
        for (auto& s : c.shots) {
            if (s.step == index + 1) s.step = 0;
            else if (s.step > index + 1) --s.step;
        }
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

void TestCaseStore::removeShot(const QString& id, int shotId) {
    updateCase(id, [shotId](TestCase& c) {
        c.shots.erase(std::remove_if(c.shots.begin(), c.shots.end(), [&](const Screenshot& s) { return s.id == shotId; }), c.shots.end());
    });
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

int TestCaseStore::executedCount() const {
    return static_cast<int>(std::count_if(m_cases.cbegin(), m_cases.cend(),
        [](const TestCase& c) { return c.lastRun.outcome != RunOutcome::None; }));
}

void TestCaseStore::touch(const QString& id) {
    scheduleSave();
    emit caseChanged(id);
}

} // namespace qaflow
