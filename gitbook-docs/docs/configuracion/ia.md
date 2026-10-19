---
description: Configurar Anthropic, OpenAI o Gemini para generar casos de prueba.
---

# IA para generar casos

La generación de casos con IA funciona **sin configurar nada**: el modo manual copia el prompt para pegarlo
en ChatGPT u otra IA y trae su respuesta. Configurar una clave de API añade el botón **«Generar con IA»**,
que hace la llamada desde QAflow.

## Configurar un proveedor

En **Ajustes → IA para generar casos**:

| Campo | Qué poner |
|-------|-----------|
| **Proveedor** | Anthropic, OpenAI o Gemini |
| **Clave de API** | La clave del proveedor. Se guarda en el llavero del sistema, una por proveedor |
| **Modelo** | El modelo a usar. El botón de al lado lista los modelos disponibles para tu clave |
| **Tope de tokens de la respuesta** | Si los casos llegan cortados, súbelo |
| **Dirección de la API** (opcional) | Sólo si usas un proxy de la organización o un servidor compatible; vacío = la API oficial |

Modelos propuestos por defecto: `claude-sonnet-5` (Anthropic), `gpt-4.1` (OpenAI) y `gemini-2.5-flash`
(Gemini). Usa el selector para elegir otro.

Pulsa **Probar** para comprobar la clave con el proveedor.

{% hint style="warning" %}
Lo que se envía a la IA es el texto del requerimiento. QAflow **siempre** te enseña el prompt completo antes
de enviarlo, para que quites lo que no deba salir del equipo. Nunca se envía nada sin ese paso.
{% endhint %}

Cómo se usa: [Generar casos con IA](../guias/casos-con-ia.md).
