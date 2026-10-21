#pragma once

#include "core/models/Issue.h"

#include <QDate>
#include <QDialog>
#include <QList>
#include <QPair>
#include <QPointer>

class QLayout;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace qaflow {

/// Lo que se enseña del requerimiento en más de un sitio (la tarjeta del panel y su ficha).
namespace requirement {

/// Cómo va el periodo asignado a QA visto desde `today`. `left` < 0: ya venció; `started` falso: aún no empieza.
/// Todos los días son hábiles: sábados y domingos no cuentan.
struct QaPeriod {
    bool valid = false;
    bool started = false;
    int total = 0;        // días del periodo, ambos incluidos (al menos 1)
    int elapsed = 0;      // días ya transcurridos, entre 0 y `total`
    int left = 0;         // días hasta el último del periodo
    int untilStart = 0;   // días hasta el primero, si aún no empieza
};
/// Días hábiles (de lunes a viernes) de `from` a `to`, sin contar `from`: negativo si `to` es anterior.
int workingDaysTo(const QDate& from, const QDate& to);
QaPeriod qaPeriod(const ExternalRequirement& r, const QDate& today);

/// Color del estado de GESREQ: observado en ámbar, conforme en verde, el resto en azul.
QString stateColor(const QString& state);
/// Color de la prioridad tal cual la escribe GESREQ ("ALTA").
QString priorityColor(const QString& priority);
/// "CONTROL DE CALIDAD OBSERVADO" → "Control de calidad observado".
QString sentenceCase(const QString& text);

/// Los bloques de la ficha de GESREQ que se reconocen por su título.
enum class Block {
    Development,    // "Datos Asignado": quién lo programó, cuándo y su ruta SVN
    QaRequest,      // "Datos Control de Calidad Solicitado": dónde probar
    QaAssignment,   // "Datos Control de Calidad Asignado": a quién de QA le toca
    Functional,     // "Datos Control Funcional"
};
const RequirementSection* block(const RequirementDetail& d, Block b);
/// Valor del primer campo no vacío del bloque cuya etiqueta empieza por `labelPrefix` (sin tildes ni
/// mayúsculas: "usuario aplicacion"); vacío si el bloque o el campo no están.
QString blockField(const RequirementDetail& d, Block b, const QString& labelPrefix);
/// Quién tiene asignado el control de calidad. No es el primer "Recurso(s)" de la ficha: ése es el de
/// desarrollo.
QString qaResource(const RequirementDetail& d);

} // namespace requirement

/// La ficha del requerimiento de GESREQ en su **propia ventana**: a la izquierda, fijo, lo esencial
/// (estados, el plazo de QA, prioridad, quién lo pide, a quién le toca y los adjuntos); a la derecha,
/// desplazable, la ficha ordenada para probar: qué probar (alcance y entrega), dónde probar (aplicación,
/// base de datos, usuarios, con botón de copiar), el recorrido del requerimiento y, al final, la ficha
/// de GESREQ entera sin los campos vacíos. No es modal: se puede dejar abierta al lado mientras se
/// escriben los casos.
///
/// No lee GESREQ por sí misma: pide la lectura (`loadRequested`) y quien la abrió le pasa el resultado.
class RequirementWindow : public QDialog {
    Q_OBJECT
public:
    explicit RequirementWindow(QWidget* parent = nullptr);

    /// Pinta la ficha del issue. `canLoad` falso (sin conexión a GESREQ) esconde «Cargar ficha».
    void setRequirement(const QString& issueId, const RequirementLink& link, bool canLoad);
    /// Mientras se lee la ficha de GESREQ, el botón lo dice y no se puede volver a pulsar.
    void setReading(bool reading);
    QString issueId() const { return m_issueId; }

signals:
    void openUrlRequested(const QString& url);
    void loadRequested();

private:
    void refresh();
    /// Una sección de la columna de contenido, con su entrada en el índice de arriba.
    QVBoxLayout* addSection(const QString& title);
    /// Marca en el índice la sección que se está leyendo.
    void markCurrentSection();
    void buildRail();
    void buildWhatToTest(bool hasDetail);
    void buildWhereToTest();
    void buildJourney();
    void buildRecord(bool hasDetail);

    QString m_issueId;
    RequirementLink m_link;
    bool m_canLoad = false;
    bool m_reading = false;

    QVBoxLayout* m_rail;           // columna fija, se rehace en cada refresco
    QLayout* m_nav;                // índice de secciones
    QList<QPair<QPushButton*, QPointer<QWidget>>> m_jumps;   // entrada del índice → su sección
    QVBoxLayout* m_content;        // secciones, se rehacen en cada refresco
    QScrollArea* m_scroll;
    QPushButton* m_load;
    QPushButton* m_open;
    QLabel* m_readAt;
};

} // namespace qaflow
