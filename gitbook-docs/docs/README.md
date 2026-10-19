---
description: >-
  Aplicación de escritorio para equipos de QA: casos, planes, ejecución manual con
  evidencias, bugs y el control de calidad de cada requerimiento de punta a punta.
---

# Bienvenido a QAflow

**QAflow** es una aplicación de escritorio (Qt 6 · C++20, Linux, Windows y macOS) que reúne en un solo
lugar todo el trabajo de un equipo de QA:

* **Casos de prueba** organizados en suites, con pasos de tres campos (acción, datos y resultado esperado).
* **Planes de prueba** que se ejecutan en **ciclos**, eligiendo en cada uno qué casos entran.
* **Ejecución manual paso a paso** con cronómetro, veredictos por teclado y atajos globales que funcionan
  aunque estés probando otra aplicación.
* **Evidencias**: capturas anotadas, grabaciones GIF, logs y vídeos adjuntos, siempre ligados a la
  ejecución y al paso en el que se tomaron.
* **Bugs** reportados a Jira, GitHub, GitLab o Azure DevOps sin salir de la prueba, con las evidencias
  subidas y una cola offline si no hay red.
* **Issues**: el control de calidad de un requerimiento de **GESREQ** de principio a fin —plan, ciclos,
  bugs, acta R-213, publicación en Zephyr, en el gestor y registro en GESREQ—.
* **Historial y métricas**: informes de cada ciclo, tasa de éxito por suite y evolución entre ciclos.

![La pantalla de ejecución](.gitbook/assets/03-ejecucion.png)

## Por dónde empezar

| Si quieres… | Lee |
|-------------|-----|
| Instalar QAflow | [Instalación](primeros-pasos/instalacion.md) |
| Probar algo en cinco minutos | [Recorrido rápido](primeros-pasos/recorrido-rapido.md) |
| Entender las palabras que usa la aplicación | [Glosario](conceptos/glosario.md) |
| Ver cómo encaja todo, de GESREQ a Jira | [El flujo de trabajo](conceptos/flujo-de-trabajo.md) |
| Conectar Jira, Zephyr o GESREQ | [Configuración](configuracion/ajustes-generales.md) |
| Consultar un atajo | [Atajos de teclado](referencia/atajos.md) |
| Compilar o contribuir | [Compilar y probar](desarrollo/compilar-y-probar.md) |

{% hint style="info" %}
La interfaz está en **español o inglés** y admite **tema oscuro o claro**. Esta documentación usa los
nombres de la interfaz en español.
{% endhint %}

## Qué no hace QAflow

* No automatiza pruebas: está pensado para la **prueba manual** y para documentarla bien.
* No sustituye al gestor de incidencias ni a Zephyr: los **alimenta**. Lo que se publica siempre se puede
  revisar antes de enviarlo, y nada se sobrescribe solo.
