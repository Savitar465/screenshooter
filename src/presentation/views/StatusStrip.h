#pragma once

#include "presentation/Screen.h"

#include <QFrame>

class QHBoxLayout;
class QLabel;
class QProgressBar;
class QPushButton;

namespace qaflow {

class TestCaseStore;
class PlanStore;
class RunController;
class RunHistoryStore;

/// Barra inferior con un único indicador para el plan o caso en ejecución y métricas globales.
/// Cada bloque es clicable y lleva a su pantalla.
class StatusStrip : public QFrame {
    Q_OBJECT
public:
    StatusStrip(TestCaseStore& cases, PlanStore& plan, RunController& run, RunHistoryStore& history, QWidget* parent = nullptr);

    /// La versión nueva y en qué punto está su instalación ("QAflow 1.6.0 disponible"), en un bloque al
    /// final de la barra; vacío lo oculta.
    void setUpdate(const QString& text);

signals:
    void navigate(Screen s);
    /// Abrir las métricas (tasa de éxito por suite y evolución entre ciclos) en el historial.
    void metricsRequested();
    /// Ver la versión nueva disponible.
    void updateRequested();

private:
    void refresh();
    /// Bloque clicable de la barra, con su layout horizontal listo para añadir etiquetas.
    QPushButton* item(const QString& tooltip, QHBoxLayout** body);

    TestCaseStore& m_cases;
    PlanStore& m_plan;
    RunController& m_run;
    RunHistoryStore& m_history;

    QFrame* m_runDot;
    QLabel* m_runText;
    QLabel* m_rate;
    QLabel* m_rateDetail;
    QLabel* m_trend;
    QProgressBar* m_runBar;
    QPushButton* m_update;
    QLabel* m_updateText;
};

} // namespace qaflow
