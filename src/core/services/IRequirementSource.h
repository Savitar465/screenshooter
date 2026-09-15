#pragma once

#include "core/models/Requirement.h"
#include "core/services/IIssueTracker.h"   // ConnectionResult

#include <QCoreApplication>
#include <functional>

namespace qaflow {

/// Sistema externo del que se importan los requerimientos (GESREQ). Lee la bandeja y las fichas, y —si
/// el conector lo implementa— registra el resultado del control de calidad, que es lo único que
/// escribe. Asíncrona: las llamadas devuelven por callback en el hilo principal.
class IRequirementSource {
public:
    virtual ~IRequirementSource() = default;
    /// Inicia sesión con estos ajustes y lee la bandeja; `displayName` dice con qué usuario se entró y
    /// cuántos requerimientos tiene asignados.
    virtual void testConnection(const RequirementSourceSettings& s, std::function<void(const ConnectionResult&)> done) = 0;
    /// Requerimientos asignados al usuario para control de calidad. Una bandeja sin requerimientos es
    /// `ok` con la lista vacía; una página que no se puede interpretar nunca lo es.
    virtual void fetchInbox(const RequirementSourceSettings& s, std::function<void(const RequirementInboxResult&)> done) = 0;
    /// Ficha completa de un requerimiento por su número.
    virtual void fetchDetail(const RequirementSourceSettings& s, const QString& id,
                             std::function<void(const RequirementDetailResult&)> done) = 0;
    /// Catálogo de sistemas, con el mismo código que usa la bandeja: de ahí se elige el de cada proyecto.
    virtual void fetchSystems(const RequirementSourceSettings& s, std::function<void(const RequirementSystemsResult&)> done) = 0;

    /// ¿Sabe este conector registrar el resultado del control de calidad? Es la única operación que
    /// cambia algo en el sistema, así que se pregunta antes de ofrecerla.
    virtual bool canRegisterResult() const { return false; }
    /// Registra el resultado de la revisión en el requerimiento. Nunca se llama sola: siempre la pide
    /// quien cierra la revisión, después de confirmarlo.
    virtual void registerResult(const RequirementSourceSettings& s, const RequirementRegistration& registration,
                                std::function<void(const RequirementRegistrationResult&)> done) {
        Q_UNUSED(s); Q_UNUSED(registration);
        RequirementRegistrationResult result;
        result.failure = RequirementSourceFailure::Configuration;
        result.error = QCoreApplication::translate("core", "Este conector no registra resultados en el sistema de requerimientos");
        done(result);
    }
};

} // namespace qaflow
