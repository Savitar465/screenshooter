---
description: Preferencias de captura y grabación, carpeta de evidencias y atajos globales.
---

# Capturas y atajos

## Capturas de pantalla y grabaciones

En **Ajustes → Capturas de pantalla y grabaciones**:

| Ajuste | Qué hace |
|--------|----------|
| **Atajo de captura** | Por defecto `Ctrl+Shift+S` |
| **Atajo de grabación** | Inicia o detiene la grabación de GIF. Por defecto `Ctrl+Shift+G` |
| **Formato** | Formato de imagen de las capturas |
| **Modo** | Pantalla completa, ventana activa o región |
| **Retardo** | Sin retardo, 3, 5 o 10 s con cuenta atrás en pantalla (para abrir menús o tooltips) |
| **GIF · fotogramas** | Fotogramas por segundo de la grabación |
| **GIF · duración máxima** | La grabación se detiene sola al llegar a ella |
| **Atajos globales** | Capturar y avanzar de paso aunque QAflow no tenga el foco |
| **Abrir el editor de anotaciones después de cada captura** | |
| **Copiar la captura al portapapeles** | |
| **Carpeta de capturas** | Dónde se guardan las evidencias. Por defecto `~/QAflow/capturas` (con una subcarpeta por proyecto) |

## Atajos de la ejecución

En **Ajustes → Atajos de la ejecución**, para avanzar por los pasos **sin volver a la ventana de QAflow**
mientras pruebas otra aplicación:

| Acción | Por defecto | Qué hace |
|--------|-------------|----------|
| **Pasa y siguiente** | `Ctrl+Alt+P` | Marca el paso actual como superado y avanza |
| **Falla y siguiente** | `Ctrl+Alt+F` | Marca el paso actual como fallido y avanza |
| **Paso anterior** | `Ctrl+Alt+A` | Vuelve al anterior sin tocar su veredicto |
| **Paso siguiente** | `Ctrl+Alt+D` | Pasa al siguiente sin darle veredicto |

Tras cada atajo, una notificación de la bandeja dice en qué paso quedó la ejecución. Las capturas siguientes se
asignan solas al paso nuevo.

## Atajos globales según el sistema

| Sistema | Cómo se registra |
|---------|------------------|
| Windows | `RegisterHotKey` |
| Linux X11 | `XGrabKey` |
| Linux Wayland | Portal `GlobalShortcuts` (`xdg-desktop-portal`) |
| macOS | Carbon |

Si el sistema rechaza un atajo, Ajustes lo indica («Atajo sólo con QAflow en primer plano») y sigue
funcionando con la app en primer plano.

{% hint style="info" %}
En **Wayland** las capturas pasan por el portal del escritorio: «Ventana activa» abre el selector del
compositor y la grabación de GIF no está disponible.
{% endhint %}
