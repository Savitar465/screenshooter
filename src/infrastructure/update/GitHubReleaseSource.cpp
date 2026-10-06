#include "GitHubReleaseSource.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>

namespace qaflow {

GitHubReleaseSource::GitHubReleaseSource(QString repo, QString apiBase, QObject* parent)
    : HttpClient(parent), m_repo(std::move(repo)), m_apiBase(std::move(apiBase)) {}

void GitHubReleaseSource::fetchReleases(std::function<void(const UpdateCheckResult&)> done) {
    QNetworkRequest req = jsonRequest(QStringLiteral("%1/repos/%2/releases?per_page=20").arg(m_apiBase, m_repo));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    // GitHub rechaza las peticiones sin User-Agent.
    req.setRawHeader("User-Agent", "QAflow/" + QCoreApplication::applicationVersion().toUtf8());
    get(req, [done](const Response& r) {
        if (!r.ok) {
            done(UpdateCheckResult{false, r.error, {}});
            return;
        }
        if (!r.json.isArray()) {
            done(UpdateCheckResult{false, QCoreApplication::translate("infrastructure", "GitHub respondió algo que no es una lista de versiones"), {}});
            return;
        }
        done(UpdateCheckResult{true, {}, parseReleases(r.json)});
    });
}

void GitHubReleaseSource::download(const QUrl& url, const QString& path, std::function<void(qint64, qint64)> progress,
                                   std::function<void(const DownloadResult&)> done) {
    // Los paquetes de una release redirigen a otro servidor de GitHub: Qt sigue la redirección (https → https).
    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/octet-stream");
    req.setRawHeader("User-Agent", "QAflow/" + QCoreApplication::applicationVersion().toUtf8());
    req.setTransferTimeout(30000);   // sin recibir nada en ese tiempo, se da por cortada
    QNetworkReply* reply = downloadFile(req, path, std::move(progress), [done](const Response& r) {
        done(DownloadResult{r.ok, r.error});
    });
    m_downloads.removeIf([](const QPointer<QNetworkReply>& p) { return p.isNull(); });
    m_downloads << reply;
}

void GitHubReleaseSource::cancelDownloads() {
    const auto downloads = std::exchange(m_downloads, {});
    for (const auto& reply : downloads)
        if (reply) reply->abort();
}

QList<UpdateRelease> GitHubReleaseSource::parseReleases(const QJsonDocument& json) {
    QList<UpdateRelease> out;
    for (const QJsonValue& v : json.array()) {
        const QJsonObject o = v.toObject();
        if (o[QStringLiteral("draft")].toBool()) continue;
        const QString tag = o[QStringLiteral("tag_name")].toString();
        const auto version = Version::parse(tag);
        if (!version) continue;
        UpdateRelease r;
        r.version = *version;
        // Una release marcada como previa en GitHub lo es aunque su etiqueta no lo diga.
        if (o[QStringLiteral("prerelease")].toBool() && r.version.preRelease.isEmpty()) r.version.preRelease = QStringLiteral("pre");
        const QString name = o[QStringLiteral("name")].toString().trimmed();
        r.title = name.isEmpty() ? tag : name;
        r.notes = o[QStringLiteral("body")].toString();
        r.pageUrl = QUrl(o[QStringLiteral("html_url")].toString());
        r.publishedAt = QDateTime::fromString(o[QStringLiteral("published_at")].toString(), Qt::ISODate);
        for (const QJsonValue& a : o[QStringLiteral("assets")].toArray()) {
            const QJsonObject asset = a.toObject();
            r.assets << UpdateAsset{asset[QStringLiteral("name")].toString(),
                                    QUrl(asset[QStringLiteral("browser_download_url")].toString()),
                                    asset[QStringLiteral("size")].toInteger()};
        }
        out << r;
    }
    return out;
}

} // namespace qaflow
