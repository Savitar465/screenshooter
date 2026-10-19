---
description: Publicar los ciclos de prueba en Zephyr for Jira.
---

# Zephyr

Si tu proyecto usa **Zephyr for Jira**, QAflow rellena los ciclos solo: un **Test** por caso, una ejecución
por caso con el veredicto de cada paso, las evidencias colgadas donde corresponde y los bugs como defectos.

## Activarlo

En **Ajustes**, bajo el gestor de incidencias, sección **Gestión de pruebas**. Usa la **misma instancia y
credenciales de Jira**. Indica:

| Ajuste | Para qué |
|--------|----------|
| **Versión** | La versión del proyecto a la que van los ciclos |
| **Tipo de incidencia de los Tests** | `Test` por defecto; cámbialo si tu Jira está traducido y lo llama de otra manera |

Funciona con la **ZAPI** pública (incluida desde Zephyr 5.6) y con la API del propio plugin
(`/rest/zephyr/latest`), la única de versiones anteriores como la **5.3**. QAflow prueba las dos.

## Cómo se organiza

| Pruebas | Ciclo en Zephyr | Tests |
|---------|-----------------|-------|
| **De un requerimiento** (issue) | Uno **por fase**: `GREQ 2026997 · QA`, `GREQ 2026997 · PRE` | Uno por caso, el mismo en QA y en PRE |
| **Sueltas** (plan sin issue) | Uno por ciclo de plan, con plan, fecha y ambiente en el nombre | Uno por ejecución |

En un requerimiento, los reintentos, las continuaciones y las revisiones siguientes de la misma fase
**actualizan** el ciclo de esa fase: en Zephyr queda lo último que se probó.

## Correspondencia de veredictos

| QAflow | Zephyr |
|--------|--------|
| Pasa | PASS (1) |
| Falla | FAIL (2) |
| Bloqueado | BLOCKED (4) |
| N/A | sin ejecutar (-1) |

## Qué se asigna a quién

Los Tests y las ejecuciones nuevas quedan **asignados al usuario de la conexión**. Si Zephyr rechaza la
asignación, se publica igual y se avisa.

## Si algo queda fuera

Un Test que Jira rechaza, una evidencia que ya no está en disco o los veredictos que sobran porque el Test
tiene menos pasos que el caso no paran la publicación: al terminar se listan uno por uno con su motivo.

Más en [Publicar el resultado](../guias/publicacion.md) y [Historial y métricas](../guias/historial-y-metricas.md).
