---
description: Las palabras que usa QAflow y cómo se relacionan entre sí.
---

# Glosario

## Organización

**Proyecto**
: Espacio de trabajo con sus propios casos, planes, ejecuciones, bugs e issues. Tiene un código de proyecto
  Jira, un sistema de GESREQ y una lista de fases. Las conexiones (gestor, Zephyr, GESREQ, IA) son comunes a
  todos los proyectos.

**Suite**
: Agrupación de casos (Autenticación, Checkout…). Las suites forman un **catálogo común**: una suite creada en
  un proyecto se puede elegir en cualquier otro, sin compartir los casos.

## Pruebas

**Caso de prueba** (`TC-101`)
: Lo que se prueba: título, suite, prioridad (Alta, Media, Baja), estado, precondiciones, etiquetas,
  componente y una lista de **pasos**.

**Paso**
: Los tres campos del paso de Zephyr: **acción** (qué hace el tester), **datos de la prueba** (opcionales) y
  **resultado esperado**.

**Plan de pruebas** (`PL-0001`)
: Lista **ordenada** de casos que se prueban juntos. Se puede duplicar, archivar o eliminar.

**Ciclo** (`PR-…`)
: Una ejecución de un plan: qué casos entraron, en qué **ambiente** o **fase**, cuándo empezó y terminó, y el
  resultado de cada caso. Se puede pausar, continuar y publicar en Zephyr.

**Ejecución** (`R-0007`)
: Una pasada por un caso: el veredicto de cada paso, notas, tiempo, evidencias y bugs. Al cerrarla se archiva
  en el historial y no se modifica (salvo sus evidencias).

**Veredicto**
: El resultado de un paso: **Pasa**, **Falla**, **Bloqueado** o **N/A** (no cuenta para el resultado).

**Evidencia**
: Captura, grabación GIF o fichero adjunto. Pertenece a **una ejecución** y a un paso, no al caso.

## Bugs

**Bug**
: Un defecto reportado al gestor desde una ejecución. Pertenece a **un paso** de una ejecución concreta, tiene
  severidad (Bloqueante, Crítica, Mayor, Menor, Trivial), tipo (Error o Mejora) y una **clasificación A–E**.

**Clasificación A–E**
: La del acta y GESREQ: **A** Funcionamiento/Lógica, **B** Datos, **C** Estético/Forma, **D** Recomendaciones,
  **E** Vulnerabilidades.

**Cola offline**
: Bugs que no se pudieron enviar (sin red, gestor caído). Se reintentan solos o con «Reintentar envío».

## Control de calidad de requerimientos

**GESREQ**
: El sistema de gestión de requerimientos de donde llega el trabajo de QA. Su bandeja «Registro Control
  Calidad» lista los requerimientos asignados al usuario.

**GREQ**
: El número de un requerimiento en GESREQ (`2026997`). Es su identificador estable.

**Sistema de GESREQ**
: El código del sistema al que pertenece un requerimiento (`SUMA TRANSITO`). Cada sistema se trabaja en
  **un único proyecto** de QAflow.

**Issue** (`IS-0001`)
: El control de calidad de un requerimiento dentro de QAflow. Reúne el requerimiento importado, su **plan**,
  sus ciclos, sus bugs, sus revisiones y su publicación en el gestor.

**Revisión** (ronda)
: Cada vez que se prueba un requerimiento. Se cierra con un resultado: **Conforme** u **Observado**. Si queda
  observado, vuelve a desarrollo y se abre la revisión siguiente. Se numeran seguidas (Rev 1, Rev 2…).

**Fase**
: El entorno por el que pasa un requerimiento, en orden. Por defecto **QA** y después **PRE**. Cada revisión es
  de una fase.

**Acta (R-213)**
: El documento de Word «REVISIÓN CONTROL DE CALIDAD DE SOFTWARE» que resume una revisión. QAflow lo genera.

**Publicación**
: Mandar el resultado de una revisión a sus destinos: **Zephyr**, el **gestor** (comentario con el acta),
  **GESREQ** (registro del control) y, con el Conforme final, el **cierre** del issue del gestor.

## Integraciones

**Gestor de incidencias**
: Jira, GitHub, GitLab o Azure DevOps. Recibe los bugs y, en Jira, el issue de cada requerimiento.

**Zephyr (for Jira)**
: Plugin de gestión de pruebas de Jira. QAflow publica en él los ciclos, con un **Test** por caso, el veredicto
  de cada paso, las evidencias y los defectos.
