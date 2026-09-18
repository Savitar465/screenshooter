#pragma once

#include "core/models/IssueLink.h"

#include <QHash>
#include <QPointer>
#include <QWidget>

class QLabel;
class QLineEdit;
class QVBoxLayout;
class QPushButton;
class QScrollArea;

namespace qaflow {

class TestCaseStore;
class SettingsStore;
class BugReportService;
class BugStore;
class BugDetailWindow;

/// Pantalla "Bugs": **el libro de bugs del proyecto**. Lista los que se han reportado con su estado
/// en el gestor, deja traerlos del propio gestor (los que creó QAflow, aunque los reportara otro
/// equipo) y refrescar sus estados, enseña la cola de los que no se pudieron enviar, y abre la
/// ficha de cualquiera en su ventana.
///
/// La lista **se alarga al deslizar**: se pintan `kPage` filas y, al llegar abajo, se añaden las
/// siguientes; cuando ya no quedan en el libro pero el gestor tiene más, se pide sola la página
/// siguiente al gestor.
///
/// Reportar uno no se hace aquí: el parte se abre en su propia ventana (`BugDialog`), porque se
/// escribe mientras se prueba y no debería costar perder de vista la ejecución.
class BugView : public QWidget {
    Q_OBJECT
public:
    BugView(TestCaseStore& cases, SettingsStore& settings, BugReportService& bugs, BugStore& ledger, QWidget* parent = nullptr);

signals:
    /// Abrir el parte de bug nuevo (lo atiende la ventana principal, que es quien tiene el diálogo).
    void createRequested();
    void openIssueRequested(const QString& url);
    void toast(const QString& message, const QString& color);

private:
    /// Cómo se filtra la lista por el estado que el gestor da a cada bug.
    enum class Filter { Todos, Abiertos, Resueltos };

    /// Filas que se pintan de una vez, y las que se añaden al llegar abajo.
    static constexpr int kPage = 25;

    void buildHeader(QVBoxLayout* v);
    void buildPending(QVBoxLayout* v);
    void buildList(QVBoxLayout* v);
    void refreshHeader();
    void refreshIssues();
    void refreshPending();
    /// Bugs que pasan el filtro de estado y la búsqueda, del más reciente al más antiguo.
    QList<IssueLink> visibleIssues() const;
    void setFilter(Filter f);
    void openDetail(const QString& key);
    /// Se ha llegado abajo: pinta más filas y, si ya no quedan, pide otra página al gestor.
    /// `mayAskTracker` en falso se queda en lo que ya está en el libro (rellenar el hueco no es
    /// deslizar: nadie ha pedido ir al gestor).
    void loadMore(bool mayAskTracker = true);
    /// Con la lista más corta que el hueco no hay barra que deslizar: se sigue llenando sola.
    void fillViewport();
    void importFromTracker(int startAt);
    void retryPending();
    void refreshStatuses();

    TestCaseStore& m_cases;
    SettingsStore& m_settings;
    BugReportService& m_bugs;
    BugStore& m_ledger;
    bool m_busy = false;
    Filter m_filter = Filter::Todos;
    QString m_search;
    int m_shown = kPage;        // filas pintadas ahora mismo
    int m_trackerNext = -1;     // desde dónde seguir pidiéndole bugs al gestor; -1 = no se sabe o no quedan
    int m_trackerTotal = 0;     // cuántos dijo el gestor que tiene

    QScrollArea* m_scroll;
    QLabel* m_eyebrow;
    QPushButton* m_import;
    QPushButton* m_refreshStatuses;
    QList<QPushButton*> m_chips;   // uno por Filter, en su orden
    QLineEdit* m_searchBox;
    QLabel* m_issuesHeader;
    QVBoxLayout* m_issuesList;
    QLabel* m_more;             // pie de la lista: cuántas quedan o que se están trayendo
    QWidget* m_pendingBlock;
    QLabel* m_pendingHeader;
    QPushButton* m_retry;
    QVBoxLayout* m_pendingList;
    /// Una ventana por bug: volver a pedir el mismo trae la que ya está abierta.
    QHash<QString, QPointer<BugDetailWindow>> m_detailWindows;
};

} // namespace qaflow
