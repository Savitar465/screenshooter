#pragma once

namespace qaflow {

/// Pantallas de la pila de la ventana principal. El valor es su posición en la pila, así que las nuevas
/// van al final aunque en el rail aparezcan en otro orden.
enum class Screen { Casos = 0, Plan, Run, Historial, Bug, Issues };

} // namespace qaflow
