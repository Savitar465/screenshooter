#pragma once

#include "core/models/PlanReport.h"

#include <QCoreApplication>
#include <QString>
#include <functional>

class QWidget;

namespace qaflow {

class TestPublishService;

/// Lo que comparten la pantalla de historial y la de planes al mandar un informe a Zephyr: el
/// aviso previo (qué ciclo, cuántas ejecuciones, qué Tests se estrenan, si ya estaba publicado), la
/// llamada al servicio y cómo se cuenta el resultado (toast, y un diálogo con lo que se quedó fuera).
class ZephyrPublishFlow {
    Q_DECLARE_TR_FUNCTIONS(qaflow::ZephyrPublishFlow)
public:
    using Toast = std::function<void(const QString& message, const QString& color)>;

    /// Con `update` a false crea un ciclo nuevo en Zephyr; a true actualiza aquel en el que el
    /// informe ya está publicado. `parent` es el padre de los diálogos.
    static void run(QWidget* parent, TestPublishService& service, const PlanReport& report, bool update, const Toast& toast);
};

} // namespace qaflow
