---
description: Formatos JSON, CSV y Markdown para importar y exportar casos de prueba.
---

# Importar y exportar casos

Desde **Archivo → Importar casos…** (`Ctrl+O`), **Archivo → Exportar casos** o el menú `⋯` de la lista de casos.

| Formato | Importar | Exportar | Notas |
|---------|----------|----------|-------|
| **JSON** | Sí | Sí | Formato nativo. Al compartir no lleva las evidencias |
| **CSV** | Sí | Sí | Una fila por paso, RFC 4180 |
| **Markdown** | No | Sí | Para leer o pegar en un documento |

## Al importar

* Los casos con un **id que ya existe** se actualizan, conservando sus evidencias y su última ejecución locales.
* Los demás se **añaden**.
* El estado se respeta (`Listo` u `Obsoleto`). Un valor desconocido o el antiguo `Borrador` se importan como
  **Listo**.

## JSON

```json
[
  {
    "id": "TC-104",
    "title": "Pago con tarjeta y cupón de descuento aplicado",
    "suite": "Checkout",
    "priority": "Alta",
    "status": "Listo",
    "preconditions": "Carrito con 2 productos",
    "tags": ["regresión", "smoke"],
    "component": "Pagos",
    "jiraKey": "SHOP-12",
    "steps": [
      { "action": "Aplicar el cupón", "data": "DESC10", "expected": "Se descuenta el 10 %" }
    ]
  }
]
```

| Campo | Valores |
|-------|---------|
| `priority` | `Alta`, `Media`, `Baja` |
| `status` | `Listo`, `Obsoleto` |

## CSV

Cabecera:

```
id,title,suite,priority,status,tags,component,jira,preconditions,step,action,data,expected
```

Una fila por paso: los datos del caso se repiten en cada fila y `step` es el número de paso. Las etiquetas van
separadas por comas dentro de su campo (entrecomillado).
