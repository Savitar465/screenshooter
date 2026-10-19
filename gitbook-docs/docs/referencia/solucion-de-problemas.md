---
description: Problemas habituales y cómo resolverlos.
---

# Solución de problemas

## Conexiones

<details>

<summary>Jira rechaza el usuario aunque la contraseña es correcta</summary>

Tras varios intentos fallidos Jira pide un **CAPTCHA**. Entra una vez por el navegador, resuélvelo y vuelve a
«Probar conexión».

</details>

<details>

<summary>«PAT» no funciona en Jira Server</summary>

Los tokens personales existen desde Jira 8.14. En versiones anteriores (como la 8.5.1) usa **usuario y
contraseña**.

</details>

<details>

<summary>Zephyr dice que no existe el tipo de incidencia Test</summary>

En un Jira traducido el tipo puede llamarse de otra manera. Cámbialo en **Ajustes → Gestión de pruebas**.

</details>

<details>

<summary>«Traer de Jira» devuelve un error 400</summary>

QAflow busca los tipos **Error** y **Mejora**. Si tu instancia los llama de otra manera, el mensaje dice qué
tipos se buscaron.

</details>

<details>

<summary>GESREQ dice «la página cambió»</summary>

GESREQ modificó sus páginas y QAflow no encuentra lo que busca. Avisa al equipo de desarrollo: hay que ajustar
el lector a la página nueva.

</details>

<details>

<summary>El paso de GESREQ aparece bloqueado al publicar</summary>

El motivo aparece junto al paso: falta el acta, el resultado es «OK» con observaciones sin corregir, o
«OBSERVADO» sin ninguna. Corrígelo (genera el acta, cierra los bugs verificados) y vuelve a abrir «Publicar…».

</details>

## Capturas y atajos

<details>

<summary>El atajo global no funciona</summary>

Ajustes indica «Atajo sólo con QAflow en primer plano» si el sistema lo rechazó (otro programa lo usa). Elige
otra combinación. En Linux, X11 necesita `libx11`/`libxcb` y Wayland el portal `GlobalShortcuts`.

</details>

<details>

<summary>Las capturas salen en negro en Linux</summary>

Estás en Wayland sin `xdg-desktop-portal`. Instálalo: QAflow captura a través del portal.

</details>

<details>

<summary>No puedo grabar GIF</summary>

La grabación no está disponible en Wayland.

</details>

<details>

<summary>Capturar no hace nada</summary>

Las evidencias son de una ejecución: sin una ejecución en curso, capturar sólo avisa.

</details>

## Datos

<details>

<summary>No puedo cambiar de proyecto</summary>

Hay una ejecución o una captura en curso. Termínala, páusala (ciclos) o detenla.

</details>

<details>

<summary>Un envío quedó «sin confirmar»</summary>

El envío se cortó sin respuesta y puede haberse hecho. Búscalo en el otro sistema (en Jira, por la etiqueta
del issue) antes de repetirlo; QAflow pedirá confirmación expresa.

</details>

<details>

<summary>Tenía casos en «Borrador»</summary>

El estado Borrador ya no existe: esos casos se cargan como **Listo**.

</details>
