---
description: Crea un caso, ejecútalo con evidencias y reporta un bug en cinco minutos.
---

# Recorrido rápido

Este recorrido usa los datos de ejemplo del primer arranque. No necesitas tener nada conectado.

{% stepper %}
{% step %}
### Crea un caso de prueba

Abre **Casos de prueba** (`Ctrl+1`) y pulsa **Nuevo caso** (`Ctrl+N`). Escribe el título y, en la tabla de
pasos, la **acción**, los **datos de la prueba** (opcional) y el **resultado esperado** de cada paso.

El caso nace en estado **Listo**: se puede ejecutar enseguida. Se guarda solo mientras escribes.
{% endstep %}

{% step %}
### Ejecútalo

En la barra superior, elige el caso en el desplegable de la derecha (puedes escribir para buscarlo) y pulsa
**▶** o `F5`. Se abre la pantalla de **Ejecución** con el primer paso.
{% endstep %}

{% step %}
### Da veredicto a cada paso

Pulsa `P` (pasa), `F` (falla), `B` (bloqueado) o `S` (N/A). La ejecución salta al siguiente paso pendiente.
Puedes volver a cualquier paso con un clic en su número, con `Retroceso` o con `Alt+←`.
{% endstep %}

{% step %}
### Toma evidencias

Pulsa `Ctrl+Shift+S` para capturar la pantalla: la captura queda asignada al paso en el que estás. Haz clic en
la miniatura para verla en grande o anotarla (flechas, rectángulos, texto, difuminado).
{% endstep %}

{% step %}
### Reporta un bug

Con un paso fallado, pulsa `Ctrl+B`. El parte se abre prellenado con el caso, el paso, los pasos para
reproducir y las evidencias. Sin gestor configurado, el bug queda en la **cola de pendientes** y se envía
cuando lo conectes.
{% endstep %}

{% step %}
### Cierra el caso y mira el historial

Cuando todos los pasos tengan veredicto, pulsa **Finalizar caso**. La ejecución se archiva en **Historial**
(`Ctrl+4`) con sus pasos, notas, duración, evidencias y bugs.
{% endstep %}
{% endstepper %}

## Y después

* Agrupa casos en un [plan](../guias/planes-y-ciclos.md) y ejecútalo como un ciclo.
* Conecta tu [gestor de incidencias](../configuracion/gestor-de-incidencias.md) y [Zephyr](../configuracion/zephyr.md).
* Si tu equipo trabaja con GESREQ, sigue [El flujo de trabajo](../conceptos/flujo-de-trabajo.md).
