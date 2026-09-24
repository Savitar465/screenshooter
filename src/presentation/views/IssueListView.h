#pragma once

#include "application/IssueDirectory.h"

#include <QWidget>

class QTableWidget;

namespace qaflow {

struct AppContext;
class IssueStore;
class ProjectStore;

/// La vista de **lista** de la pantalla de issues, la alternativa al tablero: todos los issues —también los
/// finalizados que ya salieron del tablero— en una tabla ordenable por cualquier columna. No tiene cabecera
/// propia: la búsqueda y los filtros son los del tablero (`setFilter`), y elegir una fila abre el mismo panel
/// del issue a la derecha.
class IssueListView : public QWidget {
    Q_OBJECT
public:
    explicit IssueListView(const AppContext& ctx, QWidget* parent = nullptr);

    /// Los filtros del tablero; con `allProjects`, también los issues de los demás proyectos.
    void setFilter(const IssueFilter& filter, bool allProjects);
    /// Marca la fila del issue elegido (vacío: ninguna).
    void setSelected(const QString& projectId, const QString& issueId);
    /// Ordena por fecha de finalización, los más recientes primero: a donde lleva el aviso del tablero de
    /// que hay finalizados que ya no se enseñan allí.
    void sortByFinished();
    /// Cuántas filas enseña con los filtros de ahora.
    int shownCount() const;
    /// Rehace la tabla.
    void refresh();

signals:
    /// Se eligió una fila.
    void issueSelected(const QString& projectId, const QString& issueId);
    /// Doble clic (o Intro) en una fila: abrir el issue.
    void openIssueRequested(const QString& projectId, const QString& issueId);

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    /// El título se queda con lo que dejan las demás columnas, pero nunca menos que un mínimo legible: si no
    /// cabe, la tabla se desplaza de lado en vez de aplastarlo.
    void fitTitle();
    QString projectName(const QString& projectId) const;

    IssueStore& m_issues;
    IssueDirectory* m_directory;
    ProjectStore* m_projects;
    QString m_projectId;
    IssueFilter m_filter;
    bool m_allProjects = true;
    QString m_selectedProject;
    QString m_selectedIssue;
    bool m_filling = false;   // mientras se rellena, cambiar la fila actual no es elegir

    QTableWidget* m_table;
};

} // namespace qaflow
