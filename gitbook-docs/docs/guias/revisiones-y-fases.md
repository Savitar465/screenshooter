---
description: Los siete pasos de una revisión, los resultados Conforme y Observado, y las fases QA y PRE.
---

# Revisiones y fases (QA → PRE)

Un requerimiento se prueba en **rondas** llamadas **revisiones**. Cada una es de una **fase** y termina con un
resultado. La tarjeta «Revisión» del detalle del issue la enseña como una serie de pasos: el primero sin hacer
se destaca y los hechos se marcan con un visto.

## Los pasos de una revisión

| Paso | Hecho cuando | Acciones |
|------|--------------|----------|
| **1 · Preparar el plan de pruebas** | El plan tiene casos | Crear o vincular el plan, **Generar casos con IA…**, **Subir a Zephyr** / **Actualizar Zephyr** |
| **2 · Ejecutar el plan** | Algún caso se ejecutó en esta revisión | **Ejecutar plan…**, **Continuar lo fallado…**, **▶ Reanudar** (en pausa), **Ir al plan** |
| **3 · Revisar los bugs reportados** | No queda ninguno abierto (o la ronda ya se cerró) | **Cerrar verificados (N)…** |
| **4 · Generar el acta (R-213)** | La revisión tiene su `.docx` | Generar, regenerar o abrir el acta |
| **5 · Cerrar la revisión** | Está cerrada con su resultado | Cerrar como **Conforme** u **Observado** |
| **6 · Publicar el resultado** | No le falta ningún destino | **Publicar…** / **Completar publicación…** |
| **7 · Volver a probar** / **Probar en PRE** | — | Abrir la revisión siguiente, **Continuar lo fallado** o **▶ Empezar PRE…** |

## Resultado de la revisión

QAflow **propone** el resultado y dice por qué:

* **Observado** si hay casos fallidos o bloqueados o bugs abiertos.
* **Conforme** si se ejecutó todo sin fallos ni bugs abiertos.
* **Pendiente** mientras falte ejecutar.

Al **cerrar la revisión** lo confirmas. En ese momento, **solos**:

* los resultados y bugs suben al ciclo de su fase en **Zephyr**,
* y el resultado con el acta se comenta en el **issue de Jira**.

### Después de un Observado

El requerimiento vuelve a desarrollo. El issue sigue **En pruebas** y en la columna «Fallido / bloqueado». El
trabajo sigue en la misma ronda: el acta y la publicación (el registro en GESREQ). Cuando llegue la corrección:

* **Volver a probar** abre la revisión siguiente, en la misma fase.
* **Continuar lo fallado** repite sólo lo roto y abre esa revisión a la vez.

## Fases

Las fases son del proyecto (por defecto **QA, PRE**) y cada issue puede elegir las suyas con el menú
**«Fases…»** de la pista de fases («QA ✓ → PRE ●»). Por ejemplo, un requerimiento sólo de PRE empieza ya en PRE.

| La revisión se cierra… | Qué significa | Qué sigue |
|------------------------|---------------|-----------|
| **Observado**, en cualquier fase | Vuelve a desarrollo | La siguiente revisión es de la **misma** fase. Se registra en GESREQ |
| **Conforme** en una fase que no es la última | «**Aprobada en QA**» | La siguiente revisión es de la fase siguiente. Lleva su acta, pero **no** se registra en GESREQ ni cierra Jira |
| **Conforme** en la última fase | Cierre del control | El issue queda **Finalizado**, se registra el OK en GESREQ y se cierra el issue de Jira |

### Pasar a PRE

Con la fase QA limpia, el paso 7 pasa a ser **Probar en PRE** con **▶ Empezar PRE…**. También al arrancar un
ciclo puedes elegir la fase siguiente: si la revisión en curso salió limpia, el diálogo lo avisa en verde y la
cierra como «Aprobada en QA» antes de arrancar. Si no salió limpia, la fase aparece bloqueada con lo que falta
(«3 casos sin ejecutar»).

Las revisiones se numeran seguidas entre fases (Rev 1 QA, Rev 2 QA, Rev 3 PRE): es el «Número de Revisión» del
acta.

## Las cifras del cierre

El acta de la revisión que **cierra el control** declara **todo lo encontrado** en el requerimiento (QA y PRE)
como observaciones y **lo ya cerrado** como correcciones. Las demás revisiones cuentan sus bugs como
observaciones y los cerrados de rondas anteriores como correcciones.
