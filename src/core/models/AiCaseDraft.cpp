#include "AiCaseDraft.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>
#include <initializer_list>
#include <optional>

namespace qaflow::ai {

namespace {

// ---- Prompt -------------------------------------------------------------------------------

const char* const kFormat = R"({
  "cases": [
    {
      "title": "Consulta de tránsitos: filtros, paginación y columnas",
      "priority": "Alta",
      "preconditions": "Usuario con rol de consulta autenticado; existen más de 20 tránsitos registrados",
      "component": "Consulta de tránsitos",
      "tags": ["positivo", "reporte"],
      "steps": [
        { "action": "Abrir la pantalla de consulta de tránsitos", "data": "", "expected": "Se muestran los filtros vacíos y la grilla con las columnas Número, Placa, Aduana, Fecha y Estado" },
        { "action": "Filtrar por rango de fechas", "data": "01/09/2026 – 15/09/2026", "expected": "Sólo se listan tránsitos dentro del rango" },
        { "action": "Combinar el filtro de fechas con el de aduana", "data": "Aduana 201", "expected": "Se listan sólo los que cumplen ambos filtros" },
        { "action": "Pasar a la página siguiente", "data": "", "expected": "Se muestran los siguientes registros y el indicador de página se actualiza" },
        { "action": "Buscar con filtros sin resultados", "data": "Placa ZZZ-000", "expected": "Se informa que no hay resultados, sin error" }
      ]
    }
  ]
})";

// ---- Lectura ------------------------------------------------------------------------------

/// "Resultado_Esperado" → "resultadoesperado": sin mayúsculas, tildes ni separadores, para comparar
/// las claves que escribe cada IA.
QString normalizedKey(const QString& key) {
    QString out;
    for (const QChar ch : key.normalized(QString::NormalizationForm_D)) {
        if (ch.category() == QChar::Mark_NonSpacing) continue;
        if (ch.isLetterOrNumber()) out += ch.toLower();
    }
    return out;
}

QJsonValue field(const QJsonObject& o, std::initializer_list<const char*> aliases) {
    for (auto it = o.begin(); it != o.end(); ++it) {
        const QString key = normalizedKey(it.key());
        for (const char* alias : aliases)
            if (key == QLatin1String(alias)) return it.value();
    }
    return QJsonValue(QJsonValue::Undefined);   // no Null: así se distingue «no está» de «está vacío»
}

/// Texto de un valor: las listas se unen línea a línea, los números se escriben tal cual.
QString text(const QJsonValue& v) {
    if (v.isString()) return v.toString().trimmed();
    if (v.isDouble()) return QString::number(v.toDouble());
    if (v.isBool()) return v.toBool() ? QStringLiteral("sí") : QStringLiteral("no");
    if (v.isArray()) {
        QStringList lines;
        for (const auto& item : v.toArray())
            if (const QString t = text(item); !t.isEmpty()) lines << t;
        return lines.join(QLatin1Char('\n'));
    }
    return {};
}

Priority priorityOf(const QString& value) {
    const QString p = normalizedKey(value);
    if (p.startsWith(QLatin1String("alta")) || p.startsWith(QLatin1String("high")) || p.startsWith(QLatin1String("critic"))
        || p.startsWith(QLatin1String("urgent")) || p == QLatin1String("p1"))
        return Priority::Alta;
    if (p.startsWith(QLatin1String("baja")) || p.startsWith(QLatin1String("low")) || p == QLatin1String("p3")) return Priority::Baja;
    return Priority::Media;
}

TestStep stepOf(const QJsonValue& v) {
    if (!v.isObject()) return TestStep{text(v), {}, {}};
    const QJsonObject o = v.toObject();
    return TestStep{
        text(field(o, {"action", "accion", "step", "paso", "descripcion", "description"})),
        text(field(o, {"data", "datos", "testdata", "datosdeprueba", "datosdelaprueba", "datosprueba", "input", "entrada"})),
        text(field(o, {"expected", "expectedresult", "resultadoesperado", "esperado", "resultado", "result"})),
    };
}

/// Candidatos a JSON dentro de la respuesta, del más probable al menos: los bloques ``` y, si no hay,
/// lo que va del primer «{» o «[» al último «}» o «]».
QStringList jsonCandidates(const QString& response) {
    QStringList out;
    static const QRegularExpression fence(QStringLiteral("```[a-zA-Z]*\\s*\\n(.*?)```"), QRegularExpression::DotMatchesEverythingOption);
    for (auto it = fence.globalMatch(response); it.hasNext();) out << it.next().captured(1);
    const qsizetype brace = response.indexOf(QLatin1Char('{')), bracket = response.indexOf(QLatin1Char('['));
    const qsizetype start = brace < 0 ? bracket : bracket < 0 ? brace : std::min(brace, bracket);
    const qsizetype end = std::max(response.lastIndexOf(QLatin1Char('}')), response.lastIndexOf(QLatin1Char(']')));
    if (start >= 0 && end > start) out << response.mid(start, end - start + 1);
    return out;
}

std::optional<QJsonDocument> parseJson(const QString& candidate) {
    QJsonParseError err{};
    auto doc = QJsonDocument::fromJson(candidate.toUtf8(), &err);
    if (err.error == QJsonParseError::NoError) return doc;
    // Las comas antes de cerrar una lista u objeto son el error más común de quien escribe JSON a mano.
    static const QRegularExpression trailingComma(QStringLiteral(",\\s*([}\\]])"));
    QString fixed = candidate;
    fixed.replace(trailingComma, QStringLiteral("\\1"));
    doc = QJsonDocument::fromJson(fixed.toUtf8(), &err);
    if (err.error == QJsonParseError::NoError) return doc;
    return std::nullopt;
}

bool looksLikeCase(const QJsonObject& o) {
    return !field(o, {"title", "titulo", "nombre", "name"}).isUndefined() || !field(o, {"steps", "pasos"}).isUndefined();
}

/// La lista de casos de un documento: la raíz si es una lista, la lista de un objeto que la envuelve
/// ({"cases": […]}, {"casos_de_prueba": […]}) o un caso suelto.
QJsonArray casesOf(const QJsonDocument& doc) {
    if (doc.isArray()) return doc.array();
    const QJsonObject root = doc.object();
    const QJsonValue list = field(root, {"cases", "casos", "testcases", "casosdeprueba", "tests", "pruebas"});
    if (list.isArray()) return list.toArray();
    if (looksLikeCase(root)) return QJsonArray{root};
    return {};
}

} // namespace

QString generatedTag() { return QStringLiteral("ia"); }

QString buildPrompt(const GenerationRequest& request) {
    QStringList p;
    p << QStringLiteral("Actúa como analista de QA senior. Diseña los casos de prueba funcionales para validar el requerimiento "
                        "que aparece al final, de modo que un tester los pueda ejecutar paso a paso sin conocer el sistema.");
    p << QString();
    p << QStringLiteral("Objetivo: la menor cantidad de casos que cubra el requerimiento completo. Prefiere pocos casos bien "
                        "armados a muchos casos pequeños.");
    p << QString();
    p << QStringLiteral("Cómo agrupar:");
    p << QStringLiteral("- Organiza los casos por pantalla o funcionalidad, no por cada validación suelta. Un caso recorre, en "
                        "pasos consecutivos, todo lo que se comprueba en la misma pantalla y con el mismo flujo.");
    p << QStringLiteral("  Ejemplo: una pantalla de búsqueda o un reporte se prueba en un solo caso cuyos pasos verifican los "
                        "filtros (cada uno y combinados), la paginación, el ordenamiento, las columnas requeridas, el contenido "
                        "de la información mostrada y la exportación, si la hay.");
    p << QStringLiteral("- Separa en otro caso sólo lo que necesita otras precondiciones o un flujo distinto.");
    p << QStringLiteral("- Como referencia: un requerimiento pequeño suele necesitar de 2 a 4 casos y uno grande de 5 a 10. "
                        "No superes 12 casos salvo que el requerimiento describa realmente más funcionalidades independientes.");
    p << QStringLiteral("- Un caso no debería pasar de unos 15 pasos. Si crece más, divídelo por pantalla o por flujo.");
    p << QString();
    p << QStringLiteral("Reglas:");
    p << QStringLiteral("- Cubre el flujo principal, los alternativos, las validaciones de datos y los errores que se desprendan "
                        "del requerimiento, dentro de los casos agrupados como se indica arriba.");
    p << QStringLiteral("- No inventes funcionalidad que el requerimiento no mencione. Si algo es ambiguo, redacta el caso con "
                        "la interpretación más razonable y deja la duda en «preconditions» empezando por «Supuesto:».");
    p << QStringLiteral("- El título dice qué pantalla o funcionalidad se valida y con qué alcance, por ejemplo "
                        "«Reporte de tránsitos: filtros, paginación y columnas».");
    p << QStringLiteral("- Cada paso tiene una acción concreta («action»), los datos a usar si los hay («data», o \"\") y el "
                        "resultado esperado observable («expected»). Ningún paso puede quedar sin resultado esperado.");
    p << QStringLiteral("- «priority» es exactamente \"Alta\", \"Media\" o \"Baja\" según el impacto en el negocio.");
    p << QStringLiteral("- «tags» clasifica el caso: \"positivo\", \"negativo\", \"validación\", \"reporte\"…");
    p << QStringLiteral("- Escribe en español.");
    p << QString();
    p << QStringLiteral("Responde ÚNICAMENTE con un JSON válido, sin texto antes ni después, con esta estructura:");
    p << QString::fromUtf8(kFormat);
    p << QString();
    if (!request.instructions.trimmed().isEmpty()) {
        p << QStringLiteral("Indicaciones adicionales del equipo de QA:") << request.instructions.trimmed() << QString();
    }
    p << QStringLiteral("=== REQUERIMIENTO ===");
    if (!request.requirementId.trimmed().isEmpty()) p << QStringLiteral("Número: %1").arg(request.requirementId.trimmed());
    if (!request.title.trimmed().isEmpty()) p << QStringLiteral("Título: %1").arg(request.title.trimmed());
    if (!request.system.trimmed().isEmpty()) p << QStringLiteral("Sistema: %1").arg(request.system.trimmed());
    if (!request.requestType.trimmed().isEmpty()) p << QStringLiteral("Tipo de solicitud: %1").arg(request.requestType.trimmed());
    p << QString() << request.source.trimmed();
    p << QStringLiteral("=== FIN DEL REQUERIMIENTO ===");
    return p.join(QLatin1Char('\n'));
}

ParseResult parseResponse(const QString& response) {
    ParseResult r;
    if (response.trimmed().isEmpty()) {
        r.error = QStringLiteral("Pega la respuesta de la IA");
        return r;
    }
    std::optional<QJsonDocument> doc;
    for (const auto& candidate : jsonCandidates(response))
        if ((doc = parseJson(candidate)) && !casesOf(*doc).isEmpty()) break;
    if (!doc) {
        r.error = QStringLiteral("La respuesta no contiene un JSON válido: pide a la IA que responda sólo con el JSON del formato indicado");
        return r;
    }
    const QJsonArray list = casesOf(*doc);
    if (list.isEmpty()) {
        r.error = QStringLiteral("El JSON no tiene una lista de casos («cases»)");
        return r;
    }

    int number = 0;
    for (const auto& v : list) {
        ++number;
        if (!v.isObject()) {
            r.warnings << QStringLiteral("El elemento %1 no es un caso: se descarta").arg(number);
            continue;
        }
        const QJsonObject o = v.toObject();
        TestCase c;
        c.title = text(field(o, {"title", "titulo", "nombre", "name", "summary", "resumen"})).simplified();
        if (c.title.isEmpty()) {
            r.warnings << QStringLiteral("El caso %1 no tiene título: se descarta").arg(number);
            continue;
        }
        c.priority = priorityOf(text(field(o, {"priority", "prioridad"})));
        c.status = CaseStatus::Borrador;
        c.preconditions = text(field(o, {"preconditions", "precondiciones", "precondicion", "prerequisites", "prerequisitos", "requisitos"}));
        c.component = text(field(o, {"component", "componente", "modulo", "module"}));
        c.suite = text(field(o, {"suite"}));
        const QJsonValue tags = field(o, {"tags", "etiquetas", "labels"});
        c.tags = parseTags(tags.isArray() ? text(tags).replace(QLatin1Char('\n'), QLatin1Char(',')) : text(tags));
        if (!c.tags.contains(generatedTag())) c.tags.prepend(generatedTag());
        for (const auto& s : field(o, {"steps", "pasos"}).toArray()) {
            const TestStep step = stepOf(s);
            if (!step.action.isEmpty() || !step.data.isEmpty() || !step.expected.isEmpty()) c.steps << step;
        }
        // Hay quien da un único resultado esperado para todo el caso: es el del último paso.
        const QString caseExpected = text(field(o, {"expected", "expectedresult", "resultadoesperado", "esperado"}));
        if (!caseExpected.isEmpty()) {
            if (c.steps.isEmpty()) c.steps << TestStep{};
            if (c.steps.last().expected.isEmpty()) c.steps.last().expected = caseExpected;
        }
        if (c.steps.isEmpty()) {
            r.warnings << QStringLiteral("«%1» no tiene pasos").arg(c.title);
            c.steps << TestStep{};
        } else {
            QStringList missing;
            for (int i = 0; i < c.steps.size(); ++i)
                if (c.steps[i].expected.isEmpty()) missing << QString::number(i + 1);
            if (!missing.isEmpty())
                r.warnings << QStringLiteral("«%1»: sin resultado esperado en el paso %2").arg(c.title, missing.join(QStringLiteral(", ")));
        }
        r.cases << c;
    }
    r.ok = !r.cases.isEmpty();
    if (!r.ok) r.error = QStringLiteral("Ningún caso de la respuesta se pudo aprovechar");
    return r;
}

} // namespace qaflow::ai
