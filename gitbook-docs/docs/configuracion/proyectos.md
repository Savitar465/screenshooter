---
description: Crear, cambiar y configurar proyectos.
---

# Proyectos

Cada proyecto tiene sus propios **casos, planes, ejecuciones, bugs e issues**. Las suites son un catálogo
común a todos, y las evidencias se guardan en una subcarpeta por proyecto para que no se pisen.

## Crear y cambiar de proyecto

* El **selector** de la barra superior cambia de proyecto dentro de la misma ventana. Se recuerda el último
  proyecto abierto.
* El menú `⋯` junto al selector permite **crear** un proyecto vacío o **renombrar** el actual.
* Al crear un proyecto puedes indicar ya su código Jira y su sistema de GESREQ (ambos opcionales).

{% hint style="warning" %}
No se puede cambiar de proyecto con una **ejecución o una captura en curso**: termínala o detenla antes. Los
cambios se guardan antes de abrir el otro proyecto; si el guardado falla, te quedas en el actual.
{% endhint %}

Los datos anteriores a la llegada de los proyectos siguen en **Proyecto principal**, con sus archivos y rutas.

## Configuración del proyecto

En **Ajustes → Configuración del proyecto** (se aplica sólo al proyecto abierto):

| Ajuste | Para qué | Cómo |
|--------|----------|------|
| **Código Jira** | Proyecto de Jira donde van los bugs, los issues de requerimientos y los ciclos de Zephyr | Escríbelo o pulsa «Buscar…» para elegirlo de los proyectos que ves en Jira |
| **Sistema de GESREQ** | Qué requerimientos de GESREQ se trabajan en este proyecto (`SUMA TRANSITO`) | Escríbelo o pulsa «Buscar…» para elegirlo del catálogo de GESREQ |
| **Fases** | Las fases por las que pasa un requerimiento, en orden | Por defecto **QA, PRE** |

{% hint style="info" %}
Cada sistema de GESREQ se trabaja en **un único proyecto**. Si escribes uno que ya es de otro proyecto,
QAflow lo avisa y no lo guarda: al iniciar las pruebas de un requerimiento tiene que haber un solo proyecto
al que ir.
{% endhint %}

En el sistema de GESREQ, el código es lo que va antes del primer guion en la columna «Sistema» de la bandeja.

## Cambiar de proyecto desde un issue

Desde la pantalla **Issues** puedes abrir un issue de otro proyecto o iniciar las pruebas de un requerimiento
de otro sistema. QAflow **pregunta antes** de cambiar, guarda el proyecto actual y abre el otro. Ver
[Issues y requerimientos](../guias/issues.md).
