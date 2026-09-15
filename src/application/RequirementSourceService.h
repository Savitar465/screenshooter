#pragma once

#include "application/SettingsStore.h"
#include "core/services/IRequirementSource.h"

#include <QObject>
#include <QStringList>
#include <functional>
#include <memory>

namespace qaflow {

/// Conexión con el sistema de requerimientos (GESREQ) con los ajustes del SettingsStore. La conexión es
/// del usuario y común a todos los proyectos; qué sistema de GESREQ se trabaja en cada proyecto lo guarda
/// el catálogo (`ProjectStore::setRequirementSystem`).
class RequirementSourceService : public QObject {
    Q_OBJECT
public:
    RequirementSourceService(std::shared_ptr<IRequirementSource> source, SettingsStore& settings, QObject* parent = nullptr);

    /// Prueba la conexión con los ajustes actuales y deja en ellos si entró. Si entra, lee además la
    /// bandeja para ofrecer sus sistemas al vincular uno al proyecto.
    void testConnection(std::function<void(const ConnectionResult&)> done);
    /// Bandeja de control de calidad con los ajustes actuales; al leerla se actualizan también `systems()`.
    void fetchInbox(std::function<void(const RequirementInboxResult&)> done);
    /// Ficha de un requerimiento con los ajustes actuales.
    void fetchDetail(const QString& id, std::function<void(const RequirementDetailResult&)> done);
    /// Catálogo de sistemas de GESREQ con los ajustes actuales, para elegir el del proyecto.
    void fetchSystems(std::function<void(const RequirementSystemsResult&)> done);
    /// ¿Puede QAflow registrar el resultado del control de calidad en el sistema? Hace falta que el
    /// conector lo implemente y que la conexión esté configurada.
    bool canRegisterResult() const;
    /// Registra el resultado de una revisión en el requerimiento. Es la única operación que cambia algo
    /// en GESREQ: sólo se llama cuando alguien lo confirma en la pantalla.
    void registerResult(const RequirementRegistration& registration, std::function<void(const RequirementRegistrationResult&)> done);
    /// Códigos de sistema de la última bandeja leída, ordenados y sin repetir.
    const QStringList& systems() const { return m_systems; }
    /// Conexión con la que se identifican los requerimientos importados: la dirección de GESREQ.
    QString connection() const { return m_settings.requirementSource().baseUrl(); }

signals:
    void systemsChanged();

private:
    void rememberSystems(const QList<ExternalRequirement>& inbox);

    std::shared_ptr<IRequirementSource> m_source;
    SettingsStore& m_settings;
    QStringList m_systems;
};

} // namespace qaflow
