---
description: Conectar QAflow con la bandeja de control de calidad de GESREQ.
---

# GESREQ

**GESREQ** es el sistema de gestión de requerimientos de donde llega el trabajo de QA. Con la conexión
configurada, QAflow:

* lee tu bandeja **«Registro Control Calidad»** (los requerimientos asignados a ti),
* lee la ficha de cada requerimiento (alcance, solicitante, desarrollador, servidor, adjuntos…),
* descarga sus adjuntos para [generar casos con IA](../guias/casos-con-ia.md),
* y **registra el resultado** del control de calidad con el acta, cuando tú lo publicas.

## Configurar la conexión

En **Ajustes → Conexión general con GESREQ** (común a todos los proyectos):

| Campo | Qué poner |
|-------|-----------|
| **URL** | La dirección de la aplicación con su ruta, la misma con la que entras desde el navegador (por ejemplo `http://servidor:7401/greq/`) |
| **Usuario** y **Contraseña** | Tus credenciales de GESREQ. La contraseña se guarda en el llavero del sistema |

Pulsa la insignia **● Conectado / Desconectado** para probar la conexión. Si entra, QAflow lee la bandeja y
autocompleta los sistemas en el campo del proyecto.

## Vincular cada proyecto con su sistema

Cada proyecto de QAflow trabaja **un sistema** de GESREQ (`SUMA TRANSITO`, `SUMA2SALIDA`…). Se indica en
**Ajustes → Configuración del proyecto → Sistema de GESREQ**, escribiéndolo o con «Buscar…». Ver
[Proyectos](proyectos.md).

No hace falta hacerlo de antemano: la primera vez que inicias las pruebas de un requerimiento de un sistema
sin proyecto, QAflow pregunta en qué proyecto se prueba (uno existente o uno nuevo) y lo vincula.

## Lo que lee y lo que escribe

| Operación | ¿Cambia algo en GESREQ? |
|-----------|-------------------------|
| Leer la bandeja y las fichas | No |
| Descargar adjuntos | No |
| **Registrar el control de calidad** | **Sí**: cambia el estado del requerimiento y lo saca de la bandeja |

El registro sólo se hace desde «Publicar…» de una revisión, siempre con confirmación, y **una sola vez por
revisión**. Antes de enviarlo, QAflow comprueba las reglas de GESREQ (acta obligatoria, «OK» sin
observaciones sin corregir, «OBSERVADO» con al menos una) y bloquea el paso con el motivo si no se cumplen.

{% hint style="info" %}
Si la sesión de GESREQ caduca, QAflow vuelve a entrar solo una vez. Si GESREQ cambia sus páginas y QAflow no
las entiende, lo dice («la página cambió») en vez de enseñar una bandeja vacía.
{% endhint %}
