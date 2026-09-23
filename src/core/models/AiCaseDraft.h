#pragma once

#include "core/models/TestCase.h"

#include <QList>
#include <QString>
#include <QStringList>

/// Casos de prueba propuestos por una IA a partir de un requerimiento. Funciones puras: el prompt que
/// se le pide y la lectura de lo que devuelve. Quién hace la pregunta (una API con token o el propio
/// usuario pegando la respuesta de ChatGPT) no importa aquí: ambos acaban en `parseResponse`.
namespace qaflow::ai {

/// Lo que se le cuenta a la IA del requerimiento. `source` es el texto con el que trabaja (el alcance de
/// la ficha de GESREQ, lo copiado de sus adjuntos…), ya revisado por el usuario: es lo que sale del equipo.
struct GenerationRequest {
    QString requirementId;   // número GREQ; vacío si el issue no viene de GESREQ
    QString title;
    QString system;
    QString requestType;     // "NUEVA FUNCIONALIDAD"
    QString source;
    QString instructions;    // indicaciones del usuario ("céntrate en las validaciones del formulario")
};

/// El prompt completo: rol, reglas de cobertura, el formato JSON exacto que se espera y el requerimiento.
QString buildPrompt(const GenerationRequest& request);

/// Etiqueta con la que se marcan los casos generados, para encontrarlos y revisarlos después.
QString generatedTag();

struct ParseResult {
    bool ok = false;
    /// Casos leídos, sin id (lo asigna el store al guardarlos), en Borrador y con `generatedTag()`.
    QList<TestCase> cases;
    /// Lo que se leyó pero hay que revisar: casos descartados, pasos sin resultado esperado…
    QStringList warnings;
    QString error;
};

/// Lee la respuesta de la IA tal como se pegó. Tolera lo habitual de un chat: texto antes y después, el
/// JSON dentro de un bloque ```json, comas finales, claves en español o inglés ("titulo", "pasos",
/// "resultado_esperado"…) y pasos como texto suelto. Sólo falla si no hay ningún caso aprovechable.
ParseResult parseResponse(const QString& text);

} // namespace qaflow::ai
