#pragma once

#include "core/services/IUpdateSource.h"
#include "infrastructure/http/HttpClient.h"

#include <QNetworkReply>
#include <QPointer>

namespace qaflow {

/// Versiones de QAflow publicadas como releases de GitHub. Repositorio público: sin token. Las etiquetas
/// que no son una versión ("nightly") y los borradores se descartan.
class GitHubReleaseSource : public HttpClient, public IUpdateSource {
    Q_OBJECT
public:
    /// `repo` es "propietario/repositorio"; `apiBase`, la API de GitHub (otra en tests).
    explicit GitHubReleaseSource(QString repo, QString apiBase = QStringLiteral("https://api.github.com"),
                                 QObject* parent = nullptr);

    void fetchReleases(std::function<void(const UpdateCheckResult&)> done) override;
    void download(const QUrl& url, const QString& path, std::function<void(qint64, qint64)> progress,
                  std::function<void(const DownloadResult&)> done) override;
    void cancelDownloads() override;

    /// Las releases de la respuesta JSON de `GET /repos/{repo}/releases`.
    static QList<UpdateRelease> parseReleases(const QJsonDocument& json);

private:
    QString m_repo;
    QString m_apiBase;
    QList<QPointer<QNetworkReply>> m_downloads;
};

} // namespace qaflow
