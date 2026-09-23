#pragma once

#include "core/models/Settings.h"

#include <QString>
#include <QStringList>
#include <functional>

namespace qaflow {

/// La respuesta de la IA a un prompt.
struct AiCompletion {
    bool ok = false;
    QString text;
    /// La respuesta se cortó por el tope de tokens: el JSON puede venir incompleto.
    bool truncated = false;
    QString model;      // el modelo que respondió, como lo nombra el proveedor
    QString error;
    bool retryable = false;   // red, saturación o límite de uso: puede salir bien más tarde
};

struct AiModelList {
    bool ok = false;
    QStringList models;   // ids que acepta `AiProviderSettings::model`, ordenados
    QString error;
};

/// Cliente de las API de IA (Anthropic, OpenAI y Gemini) con la clave del usuario. Asíncrono: las
/// llamadas devuelven por callback en el hilo principal. Usa el proveedor elegido en los ajustes.
class IAiClient {
public:
    virtual ~IAiClient() = default;
    /// Manda el prompt tal cual como mensaje del usuario y devuelve el texto de la respuesta.
    virtual void complete(const AiSettings& s, const QString& prompt, std::function<void(const AiCompletion&)> done) = 0;
    /// Modelos disponibles para la clave: sirve de prueba de conexión y para elegir el modelo.
    virtual void listModels(const AiSettings& s, std::function<void(const AiModelList&)> done) = 0;
};

} // namespace qaflow
