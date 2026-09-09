#include "CaseFormats.h"

#include <QJsonDocument>
#include <QStringList>

namespace qaflow::formats {

// ---- JSON ---------------------------------------------------------------------------------

QJsonObject caseToJson(const TestCase& c, bool includeShots) {
    QJsonArray steps;
    for (const auto& s : c.steps) steps.append(QJsonObject{{"action", s.action}, {"expected", s.expected}});
    QJsonObject o{
        {"id", c.id}, {"title", c.title}, {"suite", c.suite},
        {"priority", toString(c.priority)}, {"status", toString(c.status)},
        {"preconditions", c.preconditions}, {"steps", steps},
        {"tags", QJsonArray::fromStringList(c.tags)}, {"component", c.component}, {"jiraKey", c.jiraKey}, {"testKey", c.testKey},
    };
    if (includeShots) {
        QJsonArray shots;
        for (const auto& s : c.shots) shots.append(QJsonObject{{"id", s.id}, {"step", s.step}, {"fileName", s.fileName}, {"path", s.path}});
        o["shots"] = shots;
    }
    if (c.lastRun.outcome != RunOutcome::None) {
        o["lastRunOutcome"] = c.lastRun.outcome == RunOutcome::Passed ? "passed" : c.lastRun.outcome == RunOutcome::Blocked ? "blocked" : "failed";
        o["lastRunAt"] = c.lastRun.at.toString(Qt::ISODate);
    }
    return o;
}

TestCase caseFromJson(const QJsonObject& o) {
    TestCase c;
    c.id = o["id"].toString();
    c.title = o["title"].toString();
    c.suite = o["suite"].toString();
    c.priority = priorityFromString(o["priority"].toString());
    c.status = statusFromString(o["status"].toString());
    c.preconditions = o["preconditions"].toString();
    c.component = o["component"].toString();
    c.jiraKey = o["jiraKey"].toString();
    c.testKey = o["testKey"].toString();
    for (const auto& v : o["tags"].toArray()) if (!v.toString().trimmed().isEmpty()) c.tags << v.toString().trimmed();
    for (const auto& v : o["steps"].toArray()) {
        const auto s = v.toObject();
        c.steps.append(TestStep{s["action"].toString(), s["expected"].toString()});
    }
    for (const auto& v : o["shots"].toArray()) {
        const auto s = v.toObject();
        c.shots.append(Screenshot{s["id"].toInt(), s["step"].toInt(), s["fileName"].toString(), s["path"].toString()});
    }
    const QString outcome = o["lastRunOutcome"].toString();
    if (!outcome.isEmpty()) {
        c.lastRun.outcome = outcome == "passed" ? RunOutcome::Passed : outcome == "blocked" ? RunOutcome::Blocked : RunOutcome::Failed;
        c.lastRun.at = QDateTime::fromString(o["lastRunAt"].toString(), Qt::ISODate);
    }
    return c;
}

QJsonArray casesToJson(const QList<TestCase>& cases, bool includeShots) {
    QJsonArray arr;
    for (const auto& c : cases) arr.append(caseToJson(c, includeShots));
    return arr;
}

std::optional<QList<TestCase>> casesFromJson(const QByteArray& bytes, QString* error) {
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError) {
        if (error) *error = QStringLiteral("JSON no válido: %1").arg(err.errorString());
        return std::nullopt;
    }
    QJsonArray arr;
    if (doc.isArray()) arr = doc.array();
    else if (doc.isObject() && doc.object()["cases"].isArray()) arr = doc.object()["cases"].toArray();
    else {
        if (error) *error = QStringLiteral("Se esperaba una lista de casos");
        return std::nullopt;
    }
    QList<TestCase> out;
    for (const auto& v : arr) {
        if (!v.isObject()) continue;
        TestCase c = caseFromJson(v.toObject());
        if (!c.id.trimmed().isEmpty()) out.append(c);
    }
    return out;
}

// ---- CSV ----------------------------------------------------------------------------------

namespace {

const QStringList kCsvHeader = {
    QStringLiteral("id"), QStringLiteral("title"), QStringLiteral("suite"), QStringLiteral("priority"), QStringLiteral("status"),
    QStringLiteral("tags"), QStringLiteral("component"), QStringLiteral("jira"), QStringLiteral("test"), QStringLiteral("preconditions"),
    QStringLiteral("step"), QStringLiteral("action"), QStringLiteral("expected")};

QString csvField(const QString& v) {
    if (v.contains(QLatin1Char(',')) || v.contains(QLatin1Char('"')) || v.contains(QLatin1Char('\n')) || v.contains(QLatin1Char('\r'))) {
        QString q = v;
        q.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QLatin1Char('"') + q + QLatin1Char('"');
    }
    return v;
}

/// Parser RFC 4180: campos entre comillas pueden contener comas, comillas dobladas y saltos de línea.
QList<QStringList> parseCsv(const QString& text) {
    QList<QStringList> rows;
    QStringList row;
    QString field;
    bool quoted = false;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text[i];
        if (quoted) {
            if (ch == QLatin1Char('"')) {
                if (i + 1 < text.size() && text[i + 1] == QLatin1Char('"')) { field += QLatin1Char('"'); ++i; }
                else quoted = false;
            } else field += ch;
        } else if (ch == QLatin1Char('"')) quoted = true;
        else if (ch == QLatin1Char(',')) { row << field; field.clear(); }
        else if (ch == QLatin1Char('\n') || ch == QLatin1Char('\r')) {
            if (ch == QLatin1Char('\r') && i + 1 < text.size() && text[i + 1] == QLatin1Char('\n')) ++i;
            row << field; field.clear();
            rows << row; row.clear();
        } else field += ch;
    }
    if (!field.isEmpty() || !row.isEmpty()) { row << field; rows << row; }
    return rows;
}

} // namespace

QString casesToCsv(const QList<TestCase>& cases) {
    QStringList lines{kCsvHeader.join(QLatin1Char(','))};
    for (const auto& c : cases) {
        const QStringList base{c.id, c.title, c.suite, toString(c.priority), toString(c.status),
                               c.tags.join(QStringLiteral("; ")), c.component, c.jiraKey, c.testKey, c.preconditions};
        auto writeRow = [&](int stepNo, const QString& action, const QString& expected) {
            QStringList f;
            for (const auto& b : base) f << csvField(b);
            f << (stepNo ? QString::number(stepNo) : QString()) << csvField(action) << csvField(expected);
            lines << f.join(QLatin1Char(','));
        };
        if (c.steps.isEmpty()) writeRow(0, {}, {});
        for (int i = 0; i < c.steps.size(); ++i) writeRow(i + 1, c.steps[i].action, c.steps[i].expected);
    }
    return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

std::optional<QList<TestCase>> casesFromCsv(const QString& text, QString* error) {
    const auto rows = parseCsv(text);
    if (rows.isEmpty()) { if (error) *error = QStringLiteral("El archivo está vacío"); return std::nullopt; }
    QStringList header;
    for (const auto& h : rows.first()) header << h.trimmed().toLower();
    auto col = [&](const char* name) { return header.indexOf(QString::fromLatin1(name)); };
    const int iId = col("id"), iTitle = col("title"), iSuite = col("suite"), iPrio = col("priority"), iStatus = col("status"),
              iTags = col("tags"), iComp = col("component"), iJira = col("jira"), iTest = col("test"), iPre = col("preconditions"),
              iAction = col("action"), iExpected = col("expected");
    if (iId < 0 || iTitle < 0) {
        if (error) *error = QStringLiteral("Faltan las columnas obligatorias id y title");
        return std::nullopt;
    }
    auto at = [](const QStringList& r, int i) { return i >= 0 && i < r.size() ? r[i] : QString(); };

    QList<TestCase> out;
    for (int n = 1; n < rows.size(); ++n) {
        const QStringList& r = rows[n];
        if (r.size() == 1 && r[0].trimmed().isEmpty()) continue; // línea en blanco
        const QString id = at(r, iId).trimmed();
        if (id.isEmpty()) continue;
        TestCase* c = nullptr;
        for (auto& x : out) if (x.id == id) { c = &x; break; }
        if (!c) {
            TestCase fresh;
            fresh.id = id;
            fresh.title = at(r, iTitle).trimmed();
            fresh.suite = at(r, iSuite).trimmed();
            fresh.priority = priorityFromString(at(r, iPrio).trimmed());
            fresh.status = statusFromString(at(r, iStatus).trimmed());
            fresh.tags = parseTags(at(r, iTags).replace(QLatin1Char(';'), QLatin1Char(',')));
            fresh.component = at(r, iComp).trimmed();
            fresh.jiraKey = at(r, iJira).trimmed();
            fresh.testKey = at(r, iTest).trimmed();
            fresh.preconditions = at(r, iPre);
            out.append(fresh);
            c = &out.last();
        }
        const QString action = at(r, iAction), expected = at(r, iExpected);
        if (!action.trimmed().isEmpty() || !expected.trimmed().isEmpty()) c->steps.append(TestStep{action, expected});
    }
    return out;
}

// ---- Markdown -----------------------------------------------------------------------------

QString casesToMarkdown(const QList<TestCase>& cases) {
    QStringList out;
    out << QStringLiteral("# Casos de prueba") << QString();
    for (const auto& c : cases) {
        out << QStringLiteral("## %1 · %2").arg(c.id, c.title.isEmpty() ? QStringLiteral("(sin título)") : c.title);
        out << QString();
        QStringList meta;
        meta << QStringLiteral("**Suite:** %1").arg(c.suite.isEmpty() ? QStringLiteral("—") : c.suite);
        meta << QStringLiteral("**Prioridad:** %1").arg(toString(c.priority));
        meta << QStringLiteral("**Estado:** %1").arg(toString(c.status));
        if (!c.component.isEmpty()) meta << QStringLiteral("**Componente:** %1").arg(c.component);
        if (!c.jiraKey.isEmpty()) meta << QStringLiteral("**Historia:** %1").arg(c.jiraKey);
        if (!c.testKey.isEmpty()) meta << QStringLiteral("**Test:** %1").arg(c.testKey);
        if (!c.tags.isEmpty()) meta << QStringLiteral("**Etiquetas:** %1").arg(c.tags.join(QStringLiteral(", ")));
        out << meta.join(QStringLiteral(" · ")) << QString();
        if (!c.preconditions.trimmed().isEmpty()) out << QStringLiteral("**Precondiciones:** %1").arg(c.preconditions.trimmed()) << QString();
        out << QStringLiteral("| # | Acción | Resultado esperado |") << QStringLiteral("|---|--------|--------------------|");
        for (int i = 0; i < c.steps.size(); ++i) {
            auto cell = [](QString s) { return s.replace(QLatin1Char('|'), QStringLiteral("\\|")).replace(QLatin1Char('\n'), QStringLiteral(" ")); };
            out << QStringLiteral("| %1 | %2 | %3 |").arg(i + 1).arg(cell(c.steps[i].action), cell(c.steps[i].expected));
        }
        out << QString();
    }
    return out.join(QLatin1Char('\n'));
}

} // namespace qaflow::formats
