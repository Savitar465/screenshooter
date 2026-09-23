#include "SettingsStore.h"

#include <QDir>
#include <QStandardPaths>

namespace qaflow {

SettingsStore::SettingsStore(std::shared_ptr<ISettingsRepository> repo, std::shared_ptr<ISecretStore> secrets, QObject* parent)
    : QObject(parent), m_repo(std::move(repo)), m_secrets(std::move(secrets)) {}

QString SettingsStore::tokenKey(TrackerKind kind) {
    return QStringLiteral("tracker/%1/token").arg(toString(kind).toLower().remove(QLatin1Char(' ')));
}

QString SettingsStore::requirementPasswordKey() { return QStringLiteral("gesreq/password"); }

QString SettingsStore::aiKeyKey(AiProvider provider) { return QStringLiteral("ai/%1/apiKey").arg(toString(provider)); }

void SettingsStore::load() {
    if (m_repo) {
        m_tracker = m_repo->loadTracker();
        m_requirementSource = m_repo->loadRequirementSource();
        m_ai = m_repo->loadAi();
        m_capture = m_repo->loadCapture();
        m_capture.clamp();
        m_app = m_repo->loadApp();
        m_runShortcuts = m_repo->loadRunShortcuts();
    }
    if (m_secrets) {
        // Migración: si el repositorio aún traía el token en claro, pasa al llavero y desaparece del fichero.
        if (!m_tracker.token.isEmpty()) {
            if (m_secrets->write(tokenKey(m_tracker.kind), m_tracker.token) && m_repo) {
                TrackerSettings clean = m_tracker;
                clean.token.clear();
                m_repo->saveTracker(clean);
            }
        } else if (const auto stored = m_secrets->read(tokenKey(m_tracker.kind))) {
            m_tracker.token = *stored;
        }
        // La contraseña de GESREQ sigue el mismo camino.
        if (!m_requirementSource.password.isEmpty()) {
            if (m_secrets->write(requirementPasswordKey(), m_requirementSource.password) && m_repo) {
                RequirementSourceSettings clean = m_requirementSource;
                clean.password.clear();
                m_repo->saveRequirementSource(clean);
            }
        } else if (const auto stored = m_secrets->read(requirementPasswordKey())) {
            m_requirementSource.password = *stored;
        }
    }
    if (m_secrets) {
        // Las claves de IA, igual: la que quedó en el fichero pasa al llavero; si no, se lee de él.
        bool migrated = false;
        for (int i = 0; i < kAiProviders; ++i) {
            const auto provider = static_cast<AiProvider>(i);
            AiProviderSettings& p = m_ai.of(provider);
            if (!p.apiKey.isEmpty()) migrated = m_secrets->write(aiKeyKey(provider), p.apiKey) || migrated;
            else if (const auto stored = m_secrets->read(aiKeyKey(provider))) p.apiKey = *stored;
        }
        if (migrated && m_repo) {
            AiSettings clean = m_ai;
            for (auto& p : clean.providers) p.apiKey.clear();
            m_repo->saveAi(clean);
        }
    }
    if (m_capture.folder.isEmpty())
        m_capture.folder = QDir(QStandardPaths::writableLocation(QStandardPaths::HomeLocation)).filePath(QStringLiteral("QAflow/capturas"));
    emit trackerChanged();
    emit requirementSourceChanged();
    emit aiChanged();
    emit captureChanged();
    emit appChanged();
    emit runShortcutsChanged();
}

void SettingsStore::updateTracker(const std::function<void(TrackerSettings&)>& mutate) {
    const TrackerKind before = m_tracker.kind;
    const QString tokenBefore = m_tracker.token;
    mutate(m_tracker);

    if (m_secrets) {
        // Cada gestor guarda su propio token: al cambiar de gestor se recupera el suyo.
        if (m_tracker.kind != before) {
            m_tracker.token = m_secrets->read(tokenKey(m_tracker.kind)).value_or(QString());
        } else if (m_tracker.token != tokenBefore) {
            if (m_tracker.token.isEmpty()) m_secrets->remove(tokenKey(m_tracker.kind));
            else m_secrets->write(tokenKey(m_tracker.kind), m_tracker.token);
        }
    }
    if (m_repo) {
        TrackerSettings persisted = m_tracker;
        if (m_secrets) persisted.token.clear();   // el token no viaja al fichero de ajustes
        m_repo->saveTracker(persisted);
        if (m_tracker.kind != before) m_tracker.project = m_repo->loadTracker().project;
    }
    emit trackerChanged();
    emit saved();
}

void SettingsStore::updateRequirementSource(const std::function<void(RequirementSourceSettings&)>& mutate) {
    const QString passwordBefore = m_requirementSource.password;
    mutate(m_requirementSource);
    if (m_secrets && m_requirementSource.password != passwordBefore) {
        if (m_requirementSource.password.isEmpty()) m_secrets->remove(requirementPasswordKey());
        else m_secrets->write(requirementPasswordKey(), m_requirementSource.password);
    }
    if (m_repo) {
        RequirementSourceSettings persisted = m_requirementSource;
        if (m_secrets) persisted.password.clear();   // la contraseña no viaja al fichero de ajustes
        m_repo->saveRequirementSource(persisted);
    }
    emit requirementSourceChanged();
    emit saved();
}

void SettingsStore::updateAi(const std::function<void(AiSettings&)>& mutate) {
    QString keysBefore[kAiProviders];
    for (int i = 0; i < kAiProviders; ++i) keysBefore[i] = m_ai.providers[i].apiKey;
    mutate(m_ai);
    m_ai.clamp();
    if (m_secrets) {
        for (int i = 0; i < kAiProviders; ++i) {
            const QString& key = m_ai.providers[i].apiKey;
            if (key == keysBefore[i]) continue;
            if (key.isEmpty()) m_secrets->remove(aiKeyKey(static_cast<AiProvider>(i)));
            else m_secrets->write(aiKeyKey(static_cast<AiProvider>(i)), key);
        }
    }
    if (m_repo) {
        AiSettings persisted = m_ai;
        if (m_secrets) for (auto& p : persisted.providers) p.apiKey.clear();   // las claves no viajan al fichero
        m_repo->saveAi(persisted);
    }
    emit aiChanged();
    emit saved();
}

void SettingsStore::updateCapture(const std::function<void(CaptureSettings&)>& mutate) {
    mutate(m_capture);
    m_capture.clamp();
    if (m_repo) m_repo->saveCapture(m_capture);
    emit captureChanged();
    emit saved();
}

void SettingsStore::updateApp(const std::function<void(AppSettings&)>& mutate) {
    mutate(m_app);
    if (m_repo) m_repo->saveApp(m_app);
    emit appChanged();
    emit saved();
}

void SettingsStore::updateRunShortcuts(const std::function<void(RunShortcuts&)>& mutate) {
    mutate(m_runShortcuts);
    if (m_repo) m_repo->saveRunShortcuts(m_runShortcuts);
    emit runShortcutsChanged();
    emit saved();
}

QString SettingsStore::secretBackend() const {
    return m_secrets ? m_secrets->description() : tr("Sin cifrar (fichero de ajustes)");
}

bool SettingsStore::secretsAreSecure() const { return m_secrets && m_secrets->isSecure(); }

} // namespace qaflow
