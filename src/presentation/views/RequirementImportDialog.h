#pragma once

#include "application/IssueStore.h"

#include <QDialog>
#include <functional>
#include <optional>

class QListWidget;
class QPushButton;

namespace qaflow {

/// Vista previa de lo que se importa de la bandeja de GESREQ: cada requerimiento del sistema del proyecto
/// dice si es nuevo, si cambió (y en qué) o si ya está importado y sin cambios, para elegir cuáles entran.
/// Los nuevos y los que cambiaron van marcados; los que no traen nada, no.
///
/// La bandeja es del usuario, no del proyecto: los requerimientos de otros sistemas también se ven, y
/// desde cualquiera de ellos se pueden iniciar las pruebas, que ocurren en el proyecto vinculado a su
/// sistema. Si ninguno trabaja ese sistema, empezar sigue valiendo: primero se elige o se crea el proyecto.
/// Consultar y previsualizar nunca cambia de proyecto: sólo «Iniciar pruebas» lo pide.
class RequirementImportDialog : public QDialog {
    Q_OBJECT
public:
    /// `others`: requerimientos de la bandeja de otros sistemas, que no se importan en este proyecto.
    /// `missing`: issues importados que ya no están en la bandeja (se conservan).
    /// `projectForSystem`: nombre del proyecto que trabaja ese sistema; vacío si ninguno lo tiene vinculado.
    RequirementImportDialog(const QString& system, const QList<IssueStore::ImportCandidate>& candidates,
                            const QList<ExternalRequirement>& others, int missing,
                            std::function<QString(const QString&)> projectForSystem, QWidget* parent = nullptr);

    /// Requerimientos marcados para importar.
    QList<ExternalRequirement> selected() const;
    void accept() override;

signals:
    void importRequested(const QList<ExternalRequirement>& requirements);
    /// Empezar las pruebas del requerimiento elegido, esté en el sistema del proyecto o en otro.
    void startTestingRequested(const ExternalRequirement& requirement);

private:
    void refreshButtons();
    /// Deja marcado como actual el requerimiento elegido en una lista y quita la selección de la otra.
    void selectIn(QListWidget* list);

    QList<IssueStore::ImportCandidate> m_candidates;
    QList<ExternalRequirement> m_others;
    std::function<QString(const QString&)> m_projectForSystem;
    std::optional<ExternalRequirement> m_current;   // el requerimiento del que se iniciarían las pruebas
    bool m_selecting = false;                       // evita que limpiar una lista reentre en la otra
    QListWidget* m_list;
    QListWidget* m_otherList = nullptr;
    QPushButton* m_import;
    QPushButton* m_start;
};

} // namespace qaflow
