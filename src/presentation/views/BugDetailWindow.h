#pragma once

#include "core/models/IssueLink.h"

#include <QDialog>

class QLabel;
class QPushButton;

namespace qaflow {

class BugReportService;

/// Ficha de un bug reportado, en su **propia ventana**: se abre desde la lista de bugs de la ejecución
/// para mirar el detalle sin perder de vista la prueba (no es modal y se puede dejar abierta al lado).
///
/// Enseña lo que QAflow guarda del bug —clasificación, severidad, estado en el gestor, el caso y el
/// paso del que salió y cuándo se reportó— y lleva al gestor, que es donde está el parte entero. Con el
/// servicio de bugs (`setBugService`) y un gestor que sepa hacerlo, también lo **cierra** en él.
class BugDetailWindow : public QDialog {
    Q_OBJECT
public:
    /// `stepAction` es el texto del paso del que salió el bug (vacío si no se sabe o es del caso entero).
    BugDetailWindow(const IssueLink& bug, const QString& caseTitle, const QString& stepAction, QWidget* parent = nullptr);

    /// Vuelve a pintar la ficha con lo último que se sepa del bug (su estado cambia al consultarlo).
    void setBug(const IssueLink& bug);

    QString bugKey() const { return m_bug.key; }
    /// Con él, la ficha ofrece «Cerrar en el gestor» mientras el bug siga abierto. Sin él (o con un
    /// gestor que no cierra issues) el botón no sale.
    void setBugService(BugReportService* service);

signals:
    void openUrlRequested(const QString& url);

private:
    void refresh();
    /// Pregunta y cierra el bug en el gestor.
    void closeInTracker();

    IssueLink m_bug;
    QString m_caseTitle;
    QString m_stepAction;
    QWidget* m_pills;
    QLabel* m_title;
    QLabel* m_where;
    QLabel* m_meta;
    QLabel* m_status;
    QPushButton* m_closeBug;
    BugReportService* m_service = nullptr;
    bool m_closing = false;
};

} // namespace qaflow
