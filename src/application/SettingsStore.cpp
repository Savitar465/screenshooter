#include "SettingsStore.h"

#include <QDir>
#include <QStandardPaths>

namespace qaflow {

SettingsStore::SettingsStore(std::shared_ptr<ISettingsRepository> repo, std::shared_ptr<ISecretStore> secrets, QObject* parent)
    : QObject(parent), m_repo(std::move(repo)), m_secrets(std::move(secrets)) {}

QString SettingsStore::tokenKey(TrackerKind kind) {
    return QStringLiteral("tracker/%1/token").arg(toString(kind).toLower().remove(QLatin1Char(' ')));
}

void SettingsStore::load() {
    if (m_repo) {
        m_tracker = m_repo->loadTracker();
        m_capture = m_repo->loadCapture();
        m_app = m_repo->loadApp();
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
    }
    if (m_capture.folder.isEmpty())
        m_capture.folder = QDir(QStandardPaths::writableLocation(QStandardPaths::HomeLocation)).filePath(QStringLiteral("QAflow/capturas"));
    emit trackerChanged();
    emit captureChanged();
    emit appChanged();
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
    }
    emit trackerChanged();
}

void SettingsStore::updateCapture(const std::function<void(CaptureSettings&)>& mutate) {
    mutate(m_capture);
    if (m_repo) m_repo->saveCapture(m_capture);
    emit captureChanged();
}

void SettingsStore::updateApp(const std::function<void(AppSettings&)>& mutate) {
    mutate(m_app);
    if (m_repo) m_repo->saveApp(m_app);
    emit appChanged();
}

QString SettingsStore::secretBackend() const {
    return m_secrets ? m_secrets->description() : tr("Sin cifrar (fichero de ajustes)");
}

bool SettingsStore::secretsAreSecure() const { return m_secrets && m_secrets->isSecure(); }

} // namespace qaflow
