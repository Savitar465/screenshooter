// ai:: (core/models/AiCaseDraft.h): prompt para la IA y lectura tolerante de su respuesta.

#include "core/models/AiCaseDraft.h"

#include <QtTest>

using namespace qaflow;

class AiCaseDraftTest : public QObject {
    Q_OBJECT
private slots:
    // ---- Prompt ------------------------------------------------------------------------

    void promptCarriesRequirementInstructionsAndFormat() {
        ai::GenerationRequest r;
        r.requirementId = QStringLiteral("2025175");
        r.title = QStringLiteral("Registro de tránsitos");
        r.system = QStringLiteral("SUMA TRANSITO-TRANSITOS");
        r.source = QStringLiteral("El sistema debe validar la placa.");
        r.instructions = QStringLiteral("Máximo 5 casos");
        const QString p = ai::buildPrompt(r);
        QVERIFY(p.contains(QStringLiteral("Número: 2025175")));
        QVERIFY(p.contains(QStringLiteral("Sistema: SUMA TRANSITO-TRANSITOS")));
        QVERIFY(p.contains(QStringLiteral("El sistema debe validar la placa.")));
        QVERIFY(p.contains(QStringLiteral("Máximo 5 casos")));
        QVERIFY(p.contains(QStringLiteral("\"expected\"")));
        // El ejemplo del formato se lee con el mismo parser: si cambia uno, el otro lo nota.
        const qsizetype start = p.indexOf(QLatin1Char('{'));
        const qsizetype end = p.indexOf(QStringLiteral("=== REQUERIMIENTO"));
        const auto example = ai::parseResponse(p.mid(start, end - start));
        QVERIFY(example.ok);
        QVERIFY(example.warnings.isEmpty());
    }

    void promptAsksForFewGroupedCases() {
        ai::GenerationRequest r;
        r.source = QStringLiteral("Texto");
        const QString p = ai::buildPrompt(r);
        QVERIFY(p.contains(QStringLiteral("la menor cantidad de casos")));
        QVERIFY(p.contains(QStringLiteral("de 2 a 4 casos")));
        QVERIFY(p.contains(QStringLiteral("de 5 a 10")));
        QVERIFY(p.contains(QStringLiteral("No superes 12 casos")));
        QVERIFY(p.contains(QStringLiteral("paginación")));
        QVERIFY(!p.contains(QStringLiteral("Un caso prueba una sola cosa")));
        // El ejemplo del formato es un caso agrupado, no uno de un solo paso: es el estilo que se copia.
        const qsizetype start = p.indexOf(QStringLiteral("{\n  \"cases\""));
        QVERIFY(start >= 0);
        const auto example = ai::parseResponse(p.mid(start, p.indexOf(QStringLiteral("=== REQUERIMIENTO")) - start));
        QCOMPARE(example.cases.size(), 1);
        QVERIFY(example.cases.first().steps.size() >= 4);
    }

    void promptOmitsEmptyFields() {
        ai::GenerationRequest r;
        r.source = QStringLiteral("Texto");
        const QString p = ai::buildPrompt(r);
        QVERIFY(!p.contains(QStringLiteral("Número:")));
        QVERIFY(!p.contains(QStringLiteral("Indicaciones adicionales")));
    }

    // ---- Respuesta ---------------------------------------------------------------------

    void readsTheRequestedFormatAsDraftCasesWithoutId() {
        const auto r = ai::parseResponse(QStringLiteral(R"({"cases":[{"title":"Alta de tránsito","priority":"Alta",
            "preconditions":"Usuario operador","component":"Registro","tags":["positivo"],
            "steps":[{"action":"Abrir","data":"","expected":"Formulario vacío"},{"action":"Guardar","data":"ABC-123","expected":"Registrado"}]}]})"));
        QVERIFY(r.ok);
        QVERIFY(r.warnings.isEmpty());
        QCOMPARE(r.cases.size(), 1);
        const TestCase& c = r.cases.first();
        QVERIFY(c.id.isEmpty());
        QCOMPARE(c.title, QStringLiteral("Alta de tránsito"));
        QCOMPARE(toString(c.priority), toString(Priority::Alta));
        QCOMPARE(toString(c.status), toString(CaseStatus::Borrador));
        QCOMPARE(c.preconditions, QStringLiteral("Usuario operador"));
        QCOMPARE(c.component, QStringLiteral("Registro"));
        QCOMPARE(c.tags, (QStringList{ai::generatedTag(), QStringLiteral("positivo")}));
        QCOMPARE(c.steps.size(), 2);
        QCOMPARE(c.steps[1].data, QStringLiteral("ABC-123"));
        QCOMPARE(c.steps[1].expected, QStringLiteral("Registrado"));
    }

    void findsTheJsonInsideChatText() {
        const auto r = ai::parseResponse(QStringLiteral(
            "¡Claro! Aquí tienes los casos:\n\n```json\n[{\"title\":\"Uno\",\"steps\":[{\"action\":\"a\",\"expected\":\"b\"}]}]\n```\n\n"
            "Espero que te sirvan."));
        QVERIFY(r.ok);
        QCOMPARE(r.cases.size(), 1);
        QCOMPARE(r.cases.first().title, QStringLiteral("Uno"));
    }

    void findsTheJsonWithoutFence() {
        const auto r = ai::parseResponse(QStringLiteral("Casos: {\"cases\": [{\"title\": \"Uno\", \"steps\": []}]} fin"));
        QVERIFY(r.ok);
        QCOMPARE(r.cases.size(), 1);
    }

    void toleratesTrailingCommas() {
        const auto r = ai::parseResponse(QStringLiteral(R"([{"title":"Uno","steps":[{"action":"a","expected":"b",},],},])"));
        QVERIFY(r.ok);
        QCOMPARE(r.cases.first().steps.size(), 1);
    }

    void acceptsSpanishKeysAndLooseValues() {
        const auto r = ai::parseResponse(QStringLiteral(R"({"casos_de_prueba":[{"Título":"Placa inválida","Prioridad":"high",
            "Precondiciones":["Sesión iniciada","Formulario abierto"],"Etiquetas":"negativo, validación",
            "Pasos":[{"Acción":"Ingresar placa","Datos de prueba":"???","Resultado_Esperado":"Mensaje de error"},"Pulsar guardar"],
            "resultado esperado":"No se guarda"}]})"));
        QVERIFY(r.ok);
        const TestCase& c = r.cases.first();
        QCOMPARE(c.title, QStringLiteral("Placa inválida"));
        QCOMPARE(toString(c.priority), toString(Priority::Alta));
        QCOMPARE(c.preconditions, QStringLiteral("Sesión iniciada\nFormulario abierto"));
        QCOMPARE(c.tags, (QStringList{ai::generatedTag(), QStringLiteral("negativo"), QStringLiteral("validación")}));
        QCOMPARE(c.steps.size(), 2);
        QCOMPARE(c.steps[0].data, QStringLiteral("???"));
        QCOMPARE(c.steps[0].expected, QStringLiteral("Mensaje de error"));
        // El paso suelto es sólo acción; el resultado del caso completa el último paso.
        QCOMPARE(c.steps[1].action, QStringLiteral("Pulsar guardar"));
        QCOMPARE(c.steps[1].expected, QStringLiteral("No se guarda"));
        QVERIFY(r.warnings.isEmpty());
    }

    void acceptsASingleCaseObject() {
        const auto r = ai::parseResponse(QStringLiteral(R"({"title":"Solo","steps":[{"action":"a","expected":"b"}]})"));
        QVERIFY(r.ok);
        QCOMPARE(r.cases.size(), 1);
    }

    void warnsAboutWhatNeedsReview() {
        const auto r = ai::parseResponse(QStringLiteral(R"([{"steps":[{"action":"a","expected":"b"}]},
            {"title":"Sin resultado","steps":[{"action":"a"},{"action":"b","expected":"c"},{"action":"d"}]},
            {"title":"Sin pasos"}])"));
        QVERIFY(r.ok);
        QCOMPARE(r.cases.size(), 2);
        QCOMPARE(r.warnings.size(), 3);
        QVERIFY(r.warnings[0].contains(QStringLiteral("no tiene título")));
        QVERIFY(r.warnings[1].contains(QStringLiteral("paso 1, 3")));
        QVERIFY(r.warnings[2].contains(QStringLiteral("no tiene pasos")));
        QCOMPARE(r.cases[1].steps.size(), 1);   // queda un paso vacío para completarlo en el editor
    }

    void unknownPriorityIsMedia() {
        const auto r = ai::parseResponse(QStringLiteral(R"([{"title":"a","priority":"Normal"},{"title":"b","priority":"Baja"}])"));
        QCOMPARE(toString(r.cases[0].priority), toString(Priority::Media));
        QCOMPARE(toString(r.cases[1].priority), toString(Priority::Baja));
    }

    void failsWithoutUsableCases() {
        QVERIFY(!ai::parseResponse(QString()).ok);
        const auto noJson = ai::parseResponse(QStringLiteral("Lo siento, no puedo ayudarte con eso."));
        QVERIFY(!noJson.ok);
        QVERIFY(noJson.error.contains(QStringLiteral("JSON")));
        QVERIFY(!ai::parseResponse(QStringLiteral(R"({"resumen":"nada"})")).ok);
        QVERIFY(!ai::parseResponse(QStringLiteral(R"([{"steps":[]}])")).ok);
    }
};

QTEST_APPLESS_MAIN(AiCaseDraftTest)
#include "test_ai_case_draft.moc"
