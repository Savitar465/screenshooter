---
description: Paquetes disponibles para cada sistema y dependencias opcionales.
---

# Instalación

QAflow se distribuye en las *releases* del repositorio. Elige el paquete de tu sistema:

{% tabs %}
{% tab title="Linux" %}
| Paquete | Para | Se actualiza solo |
|---------|------|-------------------|
| **AppImage** | Casi cualquier distribución (lleva Qt dentro) | Sí |
| **.tar.gz** portátil | Igual que el AppImage, descomprimir y ejecutar | No, sólo avisa |
| **.deb** | Debian 12+ y Ubuntu 24.04+ (usa el Qt del sistema) | No, sólo avisa |

```bash
chmod +x QAflow-*.AppImage
./QAflow-*.AppImage
```

**Dependencias opcionales** (la app funciona sin ellas, con menos prestaciones):

| Paquete | Para qué |
|---------|----------|
| `libsecret-tools` (`secret-tool`) | Guardar tokens y contraseñas en el llavero del sistema. Sin él quedan en el fichero de ajustes y la app lo avisa |
| `xdotool` | Modo de captura «Ventana activa» en X11 |
| `xdg-desktop-portal` | Capturas y atajo global en **Wayland** |
| `pdftotext` (poppler-utils) | Leer adjuntos PDF de GESREQ para la IA |
| LibreOffice | Leer adjuntos `.doc`/`.rtf` de GESREQ para la IA |
{% endtab %}

{% tab title="Windows" %}
| Paquete | Se actualiza solo |
|---------|-------------------|
| **Instalador NSIS** (`.exe`) | Sí |
| **.zip** portátil | No, sólo avisa |
{% endtab %}

{% tab title="macOS" %}
Descarga el **.dmg**, ábrelo y arrastra QAflow a Aplicaciones. En macOS la app avisa de las versiones
nuevas pero no se instala sola.
{% endtab %}
{% endtabs %}

## Actualizaciones

QAflow busca versiones nuevas **una vez al día** (se puede desactivar en Ajustes) y también desde **Ayuda →
Buscar actualizaciones…**. En Ajustes eliges el canal: sólo **versiones estables** o **estables y beta**.
Instalada como AppImage o con el instalador de Windows, descarga el paquete, comprueba su firma y lo
instala al reiniciar o al cerrar. Con los demás paquetes sólo avisa.

## Primer arranque

La primera vez se cargan **casos y un plan de ejemplo** para que puedas probar la aplicación sin preparar
nada. Puedes borrarlos cuando quieras o crear un proyecto nuevo y vacío (ver [Proyectos](../configuracion/proyectos.md)).

Siguiente paso: [Recorrido rápido](recorrido-rapido.md).

{% hint style="info" %}
¿Prefieres compilarla tú? Consulta [Compilar y probar](../desarrollo/compilar-y-probar.md).
{% endhint %}
