---
description: Conectar Jira (Cloud o Server), GitHub, GitLab o Azure DevOps.
---

# Gestor de incidencias

El gestor recibe los **bugs** que reportas. Con **Jira**, además, recibe el issue de cada requerimiento, el
resultado de cada revisión y sus enlaces, y es la base de [Zephyr](zephyr.md).

Se configura en **Ajustes → Conexión general con el gestor**. El código del proyecto va en
[Configuración del proyecto](proyectos.md).

## Jira

QAflow habla la **API REST v2**, la de Jira Cloud y Jira Server / Data Center. Elige cómo autenticarte:

| Modo | Credenciales | Para |
|------|--------------|------|
| **Jira Cloud · correo y API token** | Correo + API token | `https://empresa.atlassian.net`, con un token creado en id.atlassian.com |
| **Jira Server · usuario y contraseña** | Usuario + contraseña | Jira Server / Data Center, **incluida la 8.5.1**, que no tiene tokens personales |
| **Jira Server · token personal (PAT)** | Token personal | Jira Server / Data Center 8.14 o superior |

* En Server, la URL es la de la instancia con su *context path* si lo tiene: `https://jira.empresa.com` o
  `https://empresa.com/jira`.
* Pulsa **Probar conexión** para comprobarlo.

{% hint style="warning" %}
Si Jira bloquea al usuario tras varios intentos fallidos, «Probar conexión» lo dice: entra una vez por el
navegador y resuelve el CAPTCHA antes de que Jira vuelva a aceptar la API.
{% endhint %}

### Qué hace QAflow en Jira

| Acción | Cuándo |
|--------|--------|
| Crear bugs (tipo **Error** o **Mejora**) con etiqueta `qaflow` y evidencias adjuntas | Al reportar un bug |
| Crear el issue del requerimiento, asignado al usuario de la conexión | Al iniciar pruebas de un requerimiento |
| Comentar el resultado de la revisión con el acta adjunta | Al cerrar la revisión |
| Enlazar al issue sus bugs y los Tests de Zephyr | Al publicar la revisión |
| Cerrar el issue (transición a un estado «hecho») | Con el Conforme de la última fase |
| Cerrar bugs verificados | Desde la revisión o la ficha del bug |
| Traer los bugs creados por QAflow (`labels = qaflow`, tipos Error y Mejora) | «Traer de Jira» en la pantalla Bugs |

## GitHub, GitLab y Azure DevOps

| Gestor | Proyecto | Adjuntos | Campos |
|--------|----------|----------|--------|
| **GitHub** | `propietario/repositorio` | No se suben; se listan por nombre | Etiquetas (componentes + prioridad), asignados |
| **GitLab** (v4) | Id o ruta del proyecto | Se suben y se enlazan en Markdown | Etiquetas, asignado, tipo |
| **Azure DevOps** | Proyecto | Se suben como *AttachedFile* | Prioridad (1-4), asignado, etiquetas, versión |

En estos gestores sólo se reportan bugs: el flujo de issues de requerimientos, la publicación del resultado y
Zephyr necesitan Jira.

## Credenciales

El token o la contraseña se guarda en el llavero del sistema, uno por gestor. Ver
[Ajustes generales](ajustes-generales.md#secretos).
