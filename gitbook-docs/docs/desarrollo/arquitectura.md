---
description: Capas, dependencias y piezas principales del código.
---

# Arquitectura

QAflow es Qt 6 Widgets y C++20, organizado en cuatro capas. Cada una es una biblioteca estática que sólo enlaza
con las capas de las que puede depender: una dependencia al revés falla al enlazar.

```
presentation ──► application ──► core ◄── infrastructure
                      ▲                          │
                      └──────── main.cpp ────────┘   (raíz de composición)
```

| Capa | Qué contiene | Reglas |
|------|--------------|--------|
| **core** | Modelos (`TestCase`, `TestPlan`, `RunHistory`, `PlanReport`, `Issue`, `QualityRecord`…) e interfaces (`IIssueTracker`, `ITestManagement`, `IRequirementSource`, repositorios…) | Sin widgets, sin red, sin disco. Funciones puras |
| **application** | Stores y servicios observables (`TestCaseStore`, `PlanStore`, `RunController`, `RunHistoryStore`, `BugReportService`, `IssueStore`, `RevisionPublishService`, `QualityRecordService`…) | Sin UI. Toda mutación pasa por un store, que emite señales |
| **infrastructure** | Implementaciones: JSON en disco, QSettings, llaveros, captura, atajos globales, `JiraClient`/`GitHubClient`/`GitLabClient`/`AzureDevOpsClient`, `ZephyrClient`, `GesreqClient`, el `.docx` del acta | Sólo la conoce `main.cpp` |
| **presentation** | Una clase por pantalla (`IssuesView`, `CasesView`, `PlanView`, `RunView`, `HistoryView`, `BugView`, `SettingsView`), diálogos y widgets reutilizables | Nunca muta el modelo directamente: llama al store y se redibuja con su señal |

## Principios

* **`main.cpp` es la raíz de composición**: crea repositorios, clientes y llavero, y los inyecta por las
  interfaces de `core/services/`.
* **Un proyecto, una sesión** (`ProjectSession`): sus stores y su ventana. Cambiar de proyecto se hace en pasos
  para que la interfaz no se congele.
* **Las pantallas se construyen al abrirlas**, no al arrancar.
* **Nada sale sin revisión**: toda publicación a un sistema externo se enseña antes y queda «sin confirmar» si
  se corta.
* **Los clientes externos se prueban contra servidores HTTP falsos**; las páginas de GESREQ, con fixtures
  anonimizados.

## Documentación técnica completa

El detalle de cada módulo —persistencia, ejecución, Zephyr, GESREQ, acta, fases— está en
[`docs/architecture.md`](https://github.com/Savitar465/screenshoter/blob/main/docs/architecture.md) del repositorio.
