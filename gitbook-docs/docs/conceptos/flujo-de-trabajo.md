---
description: Del requerimiento en GESREQ al resultado publicado en Zephyr, Jira y GESREQ.
---

# El flujo de trabajo

QAflow se puede usar de dos maneras:

* **Pruebas sueltas**: casos y planes que se ejecutan y se reportan, sin requerimientos. Es todo lo que
  necesitas si no trabajas con GESREQ.
* **Control de calidad de requerimientos**: el flujo completo, guiado desde la pantalla **Issues**.

## Pruebas sueltas

```
Casos  →  Plan  →  Ciclo (ejecución de cada caso)  →  Bugs  →  Informe  →  Zephyr (opcional)
```

1. Escribe los [casos](../guias/casos-de-prueba.md) y agrúpalos en un [plan](../guias/planes-y-ciclos.md).
2. Arranca un **ciclo** del plan: eliges el ambiente y qué casos entran.
3. [Ejecuta](../guias/ejecucion.md) cada caso paso a paso, con [evidencias](../guias/evidencias.md).
4. Reporta los [bugs](../guias/bugs.md) desde el paso en el que aparecen.
5. Al terminar, el ciclo deja su **informe** en el [historial](../guias/historial-y-metricas.md), exportable a
   Markdown y publicable en Zephyr.

## Control de calidad de un requerimiento

Cada requerimiento es un **issue**, y su trabajo se organiza en **revisiones**. La pantalla del issue enseña
la revisión como cinco pasos —**Plan, Ejecutar, Acta, Cerrar, Publicar**— y siempre dice **qué toca ahora**
(«Lo siguiente» en el tablero, «TOCA AHORA» en el detalle).

{% stepper %}
{% step %}
### Iniciar pruebas desde GESREQ

En **Issues → GREQS** aparece tu bandeja de GESREQ. «Iniciar pruebas» en un requerimiento:

* lo lleva al proyecto que trabaja su sistema (o te pregunta en cuál, la primera vez),
* crea su **issue** en QAflow,
* crea su **issue en Jira**, asignado a ti,
* y deja listo su **plan de pruebas**.
{% endstep %}

{% step %}
### 1 · Preparar el plan

Añade casos al plan: escríbelos, reutiliza los del proyecto o [genéralos con IA](../guias/casos-con-ia.md) a
partir del requerimiento y sus adjuntos. «Subir a Zephyr» crea los Tests y deja el plan en el ciclo de su fase.
{% endstep %}

{% step %}
### 2 · Ejecutar el plan

«Ejecutar plan…» arranca un ciclo en la **fase** de la revisión (QA o PRE). Al arrancar, el plan sube sin
resultados a Zephyr. Ejecuta los casos; si algo falla, «Continuar lo fallado» repite sólo lo roto.
{% endstep %}

{% step %}
### Revisar los bugs reportados

Dentro del mismo paso, los bugs de los ciclos de la revisión aparecen con su clasificación. Los que ya pasaron
el reintento salen como **VERIFICADO** y se cierran en el gestor con «Cerrar verificados». El paso no se da por
hecho mientras quede alguno abierto.
{% endstep %}

{% step %}
### 3 · Generar el acta (R-213)

QAflow propone el acta con el requerimiento, el ciclo elegido y los bugs por clasificación. La revisas y se
genera el `.docx`.
{% endstep %}

{% step %}
### 4 · Cerrar la revisión

Eliges **Conforme** u **Observado**. Al cerrar, los resultados y bugs suben a Zephyr y el resultado con el
acta se comenta en el issue de Jira.
{% endstep %}

{% step %}
### 5 · Publicar el resultado

«Publicar…» registra el control en **GESREQ** (cambia el estado del requerimiento) y, con el Conforme de la
última fase, **cierra** el issue de Jira.
{% endstep %}

{% step %}
### Volver a probar o pasar a PRE

Desde el panel derecho del issue:

* **Observado**: el requerimiento vuelve a desarrollo. Cuando llegue la corrección, **↻ Volver a probar** abre la
  revisión siguiente (o «Continuar lo fallado»).
* **Conforme en QA**: la fase QA queda aprobada y «▶ Empezar PRE…» arranca la siguiente fase.
* **Conforme en PRE**: el control termina y el issue queda **Finalizado**.
{% endstep %}
{% endstepper %}

## Cómo se mueve un issue por el tablero

| Columna | Cuándo está ahí |
|---------|-----------------|
| **Pendiente** | Recién creado, sin plan con casos |
| **En preparación** | Tiene plan con casos, todavía sin ciclos |
| **En pruebas** | Se arrancó un ciclo de su plan |
| **Fallido / bloqueado** | La revisión en curso tiene casos fallidos o bloqueados, o la última quedó Observada |
| **Finalizado** | Conforme en la última fase (sale del tablero a los 7 días; sigue en la lista) |

El estado avanza solo con el trabajo y nunca retrocede por sí mismo; también se puede cambiar a mano
arrastrando la tarjeta. Más detalle en [Issues y requerimientos](../guias/issues.md) y
[Revisiones y fases](../guias/revisiones-y-fases.md).

## Qué viaja a cada sistema

| Sistema | Qué recibe | Cuándo |
|---------|-----------|--------|
| **Jira (gestor)** | El issue del requerimiento | Al iniciar pruebas |
| | Los bugs, con evidencias | Al reportarlos |
| | Comentario con el resultado y el acta; enlaces a bugs y Tests | Al cerrar la revisión |
| | Cierre del issue | Al publicar el Conforme final |
| **Zephyr** | Tests y ciclo de la fase, sin resultados | Al subir el plan o arrancar un ciclo |
| | Veredictos, pasos, evidencias y defectos | Al cerrar la revisión (o a mano desde el informe) |
| **GESREQ** | Registro del control: resultado, comentario, cifras A–E y acta | Al publicar (una vez por revisión) |

{% hint style="warning" %}
Registrar en GESREQ es **definitivo**: cambia el estado del requerimiento y lo saca de la bandeja de control.
QAflow lo pregunta siempre antes y no deja registrar dos veces la misma revisión.
{% endhint %}
