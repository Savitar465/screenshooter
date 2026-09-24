#pragma once

#include "application/IssueDirectory.h"
#include "core/models/Requirement.h"

#include <QDateTime>
#include <QWidget>
#include <optional>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

namespace qaflow {

struct AppContext;
class IssueStore;
class ProjectStore;
class RequirementSourceService;

/// Pestaña «GREQS» de la pantalla de issues: la bandeja de control de calidad del usuario en GESREQ, con
/// lo que ya tiene issue (y en qué proyecto) y lo que no. Sustituye a «Consultar GESREQ»: desde aquí se
/// empiezan las pruebas de un requerimiento o se sigue con su issue.
///
/// También se busca un GREQ por su número aunque no esté asignado al usuario (se lee su ficha), para
/// empezar sus pruebas igual. La vista no crea issues ni cambia de proyecto por su cuenta: lo pide, y lo
/// resuelve la pantalla de issues, que es quien sabe hacerlo.
class GreqsView : public QWidget {
    Q_OBJECT
public:
    /// `tabs` son las pestañas de la pantalla (Issues | GREQS), que van en la cabecera, junto al título.
    GreqsView(const AppContext& ctx, QWidget* tabs, QWidget* parent = nullptr);

    /// Lee la bandeja si todavía no se leyó: es lo que pasa al entrar en la pestaña.
    void ensureLoaded();
    /// Vuelve a leer la bandeja. Al leerla, los issues ya importados de todos los proyectos se ponen al
    /// día (qué salió de ella y qué cambió).
    void reload();
    /// Busca un GREQ por su número: en la bandeja o, si no está asignado al usuario, en su ficha.
    void findRequirement(const QString& number);

signals:
    void toast(const QString& message, const QString& color);
    /// Falta configurar la conexión con GESREQ.
    void settingsRequested();
    /// Empezar las pruebas de un requerimiento que todavía no tiene issue.
    void startTestingRequested(const ExternalRequirement& requirement, const QString& connection, const QDateTime& fetchedAt);
    /// Seguir con el issue que ya tiene un requerimiento, en el proyecto que sea.
    void openIssueRequested(const QString& projectId, const QString& issueId);

protected:
    void showEvent(QShowEvent* e) override;

private:
    /// El issue de ese requerimiento, en el proyecto que sea; vacío si todavía no lo tiene.
    std::optional<IssueDirectory::Entry> issueFor(const QString& requirementId) const;
    QString projectName(const QString& projectId) const;
    /// Rehace la lista con lo leído y los filtros.
    void rebuild();
    /// Una fila: el requerimiento, dónde está su issue (o que no lo tiene) y la acción que toca.
    QWidget* row(const ExternalRequirement& requirement, bool assigned);
    void setLoading(bool on);

    IssueStore& m_issues;
    IssueDirectory* m_directory;
    ProjectStore* m_projects;
    RequirementSourceService* m_requirements;
    QString m_projectId;

    QList<ExternalRequirement> m_inbox;
    QDateTime m_fetchedAt;
    bool m_loaded = false;
    bool m_loading = false;
    QString m_error;
    // La búsqueda por número: lo encontrado (con si está en la bandeja) o por qué no.
    std::optional<ExternalRequirement> m_found;
    bool m_foundAssigned = false;
    QString m_searchError;
    bool m_searching = false;

    QLabel* m_count;
    QLineEdit* m_filterText;
    QComboBox* m_issueFilter;
    QPushButton* m_reload;
    QLineEdit* m_number;
    QPushButton* m_find;
    QVBoxLayout* m_list;
};

} // namespace qaflow
