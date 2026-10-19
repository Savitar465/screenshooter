---
description: Cómo abrir los ajustes y qué es general y qué es de cada proyecto.
---

# Ajustes generales

Los ajustes se abren con **Archivo → Ajustes** (`Ctrl+,`) en una ventana propia que no bloquea la principal.
Los cambios **se guardan al momento**; no hay botón de aceptar.

![Ajustes](../.gitbook/assets/05-ajustes.png)

## Qué es general y qué es del proyecto

| Ámbito | Ajustes |
|--------|---------|
| **Del proyecto abierto** («Configuración del proyecto») | Código de proyecto Jira, sistema de GESREQ, fases del control de calidad |
| **Generales** (todos los proyectos) | Idioma, tema, tamaño, bandeja y actualizaciones; conexión con el gestor; Zephyr; conexión con GESREQ; IA; capturas y grabaciones; atajos de la ejecución |

## General

| Ajuste | Valores |
|--------|---------|
| **Idioma** | Español, inglés o el del sistema. Se aplica al instante |
| **Tema** | Oscuro, claro o el del sistema. Se aplica al instante |
| **Tamaño de la interfaz** | 100 %, 110 %, 125 %, 150 % o 175 %. Se aplica al **reiniciar** QAflow; útil en portátiles o pantallas de alta densidad |
| **Al cerrar la ventana, seguir en la bandeja del sistema** | Cerrar la ventana la oculta en la bandeja en vez de salir |
| **Buscar actualizaciones automáticamente** | Una vez al día |
| **Canal** | **Versiones estables** o **Estables y beta** (las beta traen lo nuevo antes, pero pueden tener fallos) |

El idioma y el tema también se cambian desde el menú **Ver → Tema** y **Ver → Idioma**.

![QAflow en inglés y con tema claro](../.gitbook/assets/11-tema-claro-en.png)

## Secciones

* [Proyectos](proyectos.md): código Jira, sistema de GESREQ y fases.
* [Gestor de incidencias](gestor-de-incidencias.md): Jira, GitHub, GitLab o Azure DevOps.
* [Zephyr](zephyr.md): publicar ciclos en Jira.
* [GESREQ](gesreq.md): la bandeja de requerimientos.
* [IA para generar casos](ia.md): Anthropic, OpenAI o Gemini.
* [Capturas y atajos](capturas-y-atajos.md).

## Secretos

Tokens, contraseñas y claves de API **no** se guardan en el fichero de ajustes sino en el **llavero del
sistema**: `secret-tool` en Linux, Keychain en macOS y DPAPI en Windows. Si no hay llavero disponible, quedan
en el fichero de ajustes y la ventana lo avisa junto al campo («se guarda en: …»).
