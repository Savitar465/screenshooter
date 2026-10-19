---
description: Publicar el resultado de una revisión en Zephyr, el gestor y GESREQ, y cerrar el issue.
---

# Publicar el resultado

Con la revisión **cerrada**, el paso **5 · Publicar** ofrece **Publicar…**. Una sola ventana
enseña los destinos, qué iría a cada uno y lo que ya se hizo, para elegir y ver cómo termina cada paso.

## Los destinos, en orden

| # | Destino | Qué manda | Cuándo se puede |
|---|---------|-----------|-----------------|
| 1 | **Zephyr** | Los ciclos de la revisión al ciclo de su fase: casos, pasos, evidencias y **defectos** (cada bug en la ejecución y en el resultado de su paso). Lo ya publicado se actualiza | Zephyr activado y algún caso ejecutado |
| 2 | **El gestor** | Un comentario en el issue con el resumen, los enlaces de los ciclos de Zephyr y el acta adjunta; y se enlazan al issue sus bugs y Tests | El issue está en Jira |
| 3 | **GESREQ** | El registro del control: resultado (`OK` / `OBSERVADO`), comentario, las cinco cifras A–E y el acta. El estado que devuelve GESREQ actualiza el issue | El issue viene de GESREQ, el acta existe, las reglas de GESREQ se cumplen, no se registró ya y no es una fase aprobada antes de la última |
| 4 | **Cerrar el issue del gestor** | La transición a un estado resuelto | Conforme en la última fase y todo lo anterior salió bien |

Zephyr y el gestor ya se hacen **solos al cerrar la revisión**; aquí se completa lo que no salió entonces.

{% hint style="warning" %}
**Registrar en GESREQ es definitivo y se hace una vez por revisión.** Cambia el estado del requerimiento.
Un Conforme registrado no se repite nunca; sólo un Observado deja que la revisión siguiente registre el suyo.
{% endhint %}

## Cómo se comporta

* Nada se envía hasta pulsar **Publicar**.
* Lo ya hecho en la revisión viene **desmarcado**: repetirlo es una decisión, no un descuido.
* Un paso que falla **no impide** los demás; cada uno cuenta su resultado.
* Un envío cortado sin respuesta queda **sin confirmar**: compruébalo en el otro sistema antes de repetirlo.
* Si GESREQ no aceptaría el registro, el paso aparece **bloqueado con el motivo** (falta el acta, un «OK» con
  observaciones sin corregir, un «OBSERVADO» sin ninguna).

## Publicar revisiones anteriores

Una revisión cerrada que quedó a medias (se volvió a probar antes de publicarla) se completa eligiendo su
**pestaña** (`Rev 1`, `Rev 2`…) en el detalle del issue y pulsando **Completar publicación…** en su paso 5. El
panel del tablero enseña además una caja por destino (ZEPHYR / GESTOR / GESREQ) con cómo quedó.

## Publicar ciclos sueltos en Zephyr

Los ciclos de planes sin issue se publican desde su informe en el historial o desde el historial de ciclos del
plan: ver [Historial y métricas](historial-y-metricas.md#informe-de-ciclo).
