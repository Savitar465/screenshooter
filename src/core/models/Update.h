#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QUrl>

#include <optional>

namespace qaflow {

/// Versión semántica de QAflow ("1.6.0", "v1.6.0-beta.2"). Las versiones previas ordenan antes que la
/// final del mismo número: 1.6.0-beta.2 < 1.6.0-rc.1 < 1.6.0.
struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;
    QString preRelease;   // "beta.2"; vacío en una versión final

    /// Admite la "v" de las etiquetas de git y omite los metadatos de compilación ("+abc"). Sin número
    /// válido, nada.
    static std::optional<Version> parse(const QString& text);
    QString toString() const;
    bool isPreRelease() const { return !preRelease.isEmpty(); }

    /// <0, 0 o >0, con las reglas de precedencia de SemVer 2.0.
    static int compare(const Version& a, const Version& b);
    friend bool operator==(const Version& a, const Version& b) { return compare(a, b) == 0; }
    friend bool operator<(const Version& a, const Version& b) { return compare(a, b) < 0; }
};

/// Qué versiones se ofrecen: sólo las finales o también las previas (beta, rc).
enum class UpdateChannel { Stable, Beta };

QString toString(UpdateChannel c);   // "stable", "beta": se persiste
UpdateChannel updateChannelFromString(const QString& s);

/// Un fichero de una release: un paquete ("QAflow-1.6.0-x86_64.AppImage") o las sumas de comprobación.
struct UpdateAsset {
    QString name;
    QUrl url;
    qint64 size = 0;
};

/// Las sumas SHA-256 de todos los paquetes de la release y su firma Ed25519: sin las dos, QAflow no se
/// instala solo (sólo ofrece la descarga).
inline const QString kChecksumsAsset = QStringLiteral("SHA256SUMS");
inline const QString kChecksumsSignatureAsset = QStringLiteral("SHA256SUMS.sig");

/// Una versión publicada (una release de GitHub).
struct UpdateRelease {
    Version version;
    QString title;        // el nombre de la release; si no tiene, la etiqueta
    QString notes;        // novedades, en Markdown
    QUrl pageUrl;         // la página de la release, con sus paquetes
    QDateTime publishedAt;
    QList<UpdateAsset> assets;

    /// El fichero con ese nombre exacto; nullptr si la release no lo trae.
    const UpdateAsset* asset(const QString& name) const;
};

/// Las sumas de un fichero en el formato de `sha256sum` ("<hex>  <nombre>" por línea; el nombre puede
/// llevar delante el "*" del modo binario). Devuelve nombre → hex en minúsculas; las líneas que no
/// encajan se ignoran.
QHash<QString, QString> parseChecksums(const QByteArray& text);

struct UpdateCheckResult {
    bool ok = false;
    QString error;
    QList<UpdateRelease> releases;
};

/// La versión más nueva de `releases` que es posterior a `current` y entra en el canal; nada si no hay.
std::optional<UpdateRelease> newestUpdate(const QList<UpdateRelease>& releases, const Version& current, UpdateChannel channel);

/// Preferencias del buscador de actualizaciones y lo que recuerda entre arranques.
struct UpdatePreferences {
    bool autoCheck = true;
    UpdateChannel channel = UpdateChannel::Stable;
    QString skippedVersion;   // "Omitir esta versión": la búsqueda automática no vuelve a avisar de ella
    QDateTime lastCheck;      // última búsqueda que llegó a responder
};

} // namespace qaflow
