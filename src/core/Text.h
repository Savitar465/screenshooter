#pragma once

#include <QString>

namespace qaflow {

/// Acorta un título a lo sumo a `max` caracteres, terminándolo en «…». Corta por la última palabra
/// entera que quepa (si eso no deja menos de la mitad) y quita los espacios y la puntuación sueltos
/// que queden al final, así lo acortado se sigue leyendo como un título.
///
/// Es lo que se usa para lo que sale de QAflow con un título derivado del issue: el del gestor y el
/// del plan. El título del issue no se toca: se guarda entero.
QString elideTitle(const QString& text, int max);

} // namespace qaflow
