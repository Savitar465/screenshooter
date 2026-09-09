#pragma once

#include "core/services/IIssueTracker.h"
#include "infrastructure/http/HttpClient.h"

namespace qaflow {

/// Base de los clientes REST de gestores de incidencias: la base HTTP más la interfaz que espera
/// la capa de aplicación. Zephyr comparte `HttpClient` pero implementa otra interfaz.
class HttpTrackerClient : public HttpClient, public IIssueTracker {
    Q_OBJECT
public:
    explicit HttpTrackerClient(QObject* parent = nullptr) : HttpClient(parent) {}
};

} // namespace qaflow
