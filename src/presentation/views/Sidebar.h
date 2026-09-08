#pragma once

#include "presentation/Screen.h"
#include "presentation/widgets/Icons.h"

#include <QFrame>
#include <QMap>

class QLabel;
class QPushButton;

namespace qaflow {

class TestCaseStore;
class PlanStore;
class RunController;
class RunHistoryStore;
class BugStore;

/// Rail de navegación al estilo de los IDE de JetBrains: una columna estrecha de iconos, uno por
/// pantalla, con el nombre y el atajo en el tooltip. La información de estado (ejecución en curso,
/// tasa de éxito y plan activo) vive en la StatusStrip del pie de la ventana; aquí sólo quedan las
/// insignias que piden atención: el progreso de la ejecución y los bugs pendientes o abiertos.
class Sidebar : public QFrame {
    Q_OBJECT
public:
    Sidebar(TestCaseStore& cases, PlanStore& plan, RunController& run, RunHistoryStore& history, BugStore& bugs, QWidget* parent = nullptr);
    void setActive(Screen s);

signals:
    void navigate(Screen s);
    /// Abrir las métricas (tasa de éxito por suite y evolución entre ciclos) en el historial.
    void metricsRequested();

private:
    /// Un botón del rail: el icono se redibuja en el color de su pantalla al activarse.
    struct NavItem {
        QPushButton* button = nullptr;
        QLabel* badge = nullptr;
        icons::Glyph glyph = icons::Glyph::Cases;
        QString accent;
        QString name;
        QString shortcut;
    };

    void refresh();
    QPushButton* railButton(icons::Glyph glyph, const QString& accent, const QString& tooltip);
    /// Botón de pantalla: como `railButton` pero además navega, recuerda su icono y lleva insignia.
    QPushButton* navButton(Screen s, icons::Glyph glyph, const QString& accent, const QString& name, const QString& shortcut);
    void setBadge(Screen s, const QString& text, const QString& color, const QString& textColor);
    void setTooltip(Screen s, const QString& detail);

    TestCaseStore& m_cases;
    PlanStore& m_plan;
    RunController& m_run;
    RunHistoryStore& m_history;
    BugStore& m_bugs;
    Screen m_active = Screen::Casos;

    QMap<Screen, NavItem> m_items;
};

} // namespace qaflow
