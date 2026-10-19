---
description: Generar el acta de control de calidad R-213 en Word a partir de la revisión.
---

# Acta de control de calidad (R-213)

Cada revisión termina en el formulario **R-213, «REVISIÓN CONTROL DE CALIDAD DE SOFTWARE»**: un documento de
Word con los datos del requerimiento, el resumen de observaciones por tipo, los casos y bugs, y los
resultados. QAflow lo **genera desde cero**, sin plantillas.

## Generarla

En el detalle del issue, paso **4 · Generar el acta (R-213)**. El diálogo enseña el acta completa, campo a
campo y corregible, antes de escribir el fichero.

Si la revisión tuvo **varias ejecuciones** del plan, elige con cuál se levanta el acta (o con todas); el acta
se rehace al cambiar.

## De dónde sale cada parte

| Sección | Se rellena con |
|---------|----------------|
| **1. Generales** | La ficha de GESREQ: GREQ, sistema, módulo, alcance, solicitante, prioridad, estado, desarrollador, servidor, esquema, tablas y funciones, repositorio; el número y las fechas de la revisión; el recurso de QA |
| **2. Resumen Observaciones** | Los bugs de la revisión por **clasificación A–E**, con sus observaciones y correcciones y el total |
| **3. Detalles de la revisión** | Los casos con su Test de Zephyr, cómo terminó cada uno, el enlace del ciclo publicado y los bugs, con las capturas que elijas |
| **4. Resultados** | El resultado de la revisión |
| **5. Observaciones Generales** | Texto libre |

Lo que GESREQ no tiene (base de datos, usuarios, departamento, **membrete**) se **hereda del acta anterior del
proyecto**: sólo se escribe una vez. Lo que nadie rellena queda como en el formulario (`S/D`, `n/a`).

{% hint style="info" %}
El **membrete** se elige una vez en el diálogo y se hereda en las actas siguientes.
{% endhint %}

## El fichero

Se propone el nombre `ControlCalidad_<GREQ>_rev<N>_<fase>_<marca>.docx` y se guarda donde elijas. El issue
recuerda la ruta de cada revisión para abrirla después. El documento usa página carta, con el formato del
formulario oficial y se abre en Word y LibreOffice.

## A dónde va

* Adjunta al **comentario del resultado** en el issue de Jira (al cerrar la revisión).
* Adjunta al **registro en GESREQ** (al publicar), que la exige.
