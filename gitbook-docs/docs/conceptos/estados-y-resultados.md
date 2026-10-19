---
description: Estados de los casos, veredictos, resultados de las revisiones y estados de los issues.
---

# Estados y resultados

## Estado de un caso

| Estado | Significado |
|--------|-------------|
| **Listo** | El caso se puede ejecutar. Es el estado con el que **nace todo caso**: al crearlo, duplicarlo, importarlo o generarlo con IA |
| **Obsoleto** | Ya no aplica. No entra en los planes ni se ofrece para ejecutar, pero se conserva con su historial. Si vuelve a estar Listo, recupera su sitio en los planes |

El estado se cambia en el editor del caso.

{% hint style="info" %}
Las versiones anteriores tenían un estado **Borrador**. Ya no existe: los casos guardados así se cargan como
**Listo**.
{% endhint %}

## Veredicto de un paso

| Veredicto | Tecla | En Zephyr |
|-----------|-------|-----------|
| **Pasa** | `P` | PASS (1) |
| **Falla** | `F` | FAIL (2) |
| **Bloqueado** | `B` | BLOCKED (4) |
| **N/A** | `S` | sin ejecutar (-1) |

Fallar o bloquear un paso **no corta** la ejecución: se sigue con el resto.

## Resultado de una ejecución

La «última ejecución» de un caso es **Pasó**, **Falló**, **Bloqueado** o **Sin ejecutar**. Una ejecución queda
**bloqueada** si algún paso se bloqueó; si no, **falla** si algún paso falló; si no, **pasa** (los N/A no cuentan).

## Resultado de una revisión

| Resultado | Cuándo se propone |
|-----------|-------------------|
| **Pendiente** | Faltan casos por ejecutar |
| **Conforme** | Se ejecutó todo, sin fallos ni bugs abiertos |
| **Observado** | Hay casos fallidos o bloqueados, o bugs abiertos |

Es una **propuesta**: la confirmas al cerrar la revisión. QAflow dice por qué propone cada uno («2 casos sin
ejecutar», «1 bug abierto»).

## Estado de QA de un issue

**Pendiente → En preparación → En pruebas → Finalizado**. Es de QAflow: no depende del estado en GESREQ ni en
Jira, que se enseñan aparte. La columna **Fallido / bloqueado** del tablero no es un estado: se calcula con
los resultados de la revisión en curso.

## Estados «sin confirmar»

Si un envío (a Jira, Zephyr o GESREQ) se corta sin respuesta, puede haberse hecho igualmente. QAflow lo
marca **sin confirmar** y, antes de repetirlo, pide que lo compruebes en el otro sistema. Así no se duplican
issues ni registros.
