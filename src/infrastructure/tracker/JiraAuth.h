#pragma once

#include "core/models/Settings.h"

#include <QByteArray>

namespace qaflow {

/// Cabecera `Authorization` para Jira y para las APIs que comparten su sesión, como la de Zephyr
/// for Jira: Bearer con un token personal y Basic en los demás modos.
QByteArray jiraAuthorization(const TrackerSettings& s);

} // namespace qaflow
