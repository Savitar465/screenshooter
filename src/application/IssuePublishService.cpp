#include "IssuePublishService.h"

#include <QDateTime>

namespace qaflow {

namespace {
/// Etiquetas con las que el issue publicado se encuentra en el gestor: la de QAflow, la del issue local y,
/// si vino de GESREQ, la del requerimiento. Sirven también para buscarlo cuando un envío queda sin confirmar.
QStringList labelsFor(const Issue& issue) {
    QStringList labels{QStringLiteral("qaflow"), issue.id};
    if (issue.isImported()) labels << QStringLiteral("GREQ-%1").arg(issue.requirement.data.id);
    return labels;
}
} // namespace

IssuePublishService::IssuePublishService(std::shared_ptr<IIssueTracker> tracker, IssueStore& issues, SettingsStore& settings, QObject* parent)
    : QObject(parent), m_tracker(std::move(tracker)), m_issues(issues), m_settings(settings) {}

QString IssuePublishService::metadataKey() const {
    const TrackerSettings& t = m_settings.tracker();
    return toString(t.kind) + QLatin1Char('|') + t.project.trimmed() + QLatin1Char('@') + t.baseUrl();
}

bool IssuePublishService::canPublish() const {
    const TrackerSettings& t = m_settings.tracker();
    return m_tracker && m_tracker->canPublishIssues(t) && !t.baseUrl().isEmpty() && !t.project.trimmed().isEmpty();
}

QString IssuePublishService::destination() const {
    const TrackerSettings& t = m_settings.tracker();
    if (t.project.trimmed().isEmpty() || t.baseUrl().isEmpty()) return {};
    return QStringLiteral("%1 · %2").arg(toString(t.kind), t.project.trimmed());
}

QString IssuePublishService::defaultIssueType(const QStringList& types) {
    for (const auto& preferred : {QStringLiteral("Tarea"), QStringLiteral("Task"), QStringLiteral("Historia"), QStringLiteral("Story")})
        for (const auto& type : types)
            if (type.compare(preferred, Qt::CaseInsensitive) == 0) return type;
    return types.isEmpty() ? QStringLiteral("Tarea") : types.first();
}

IssueDraft IssuePublishService::draftFor(const Issue& issue) const {
    IssueDraft draft;
    draft.summary = issue.title.trimmed();
    draft.issueType = issue.publication.issueType.isEmpty() ? defaultIssueType(m_issueTypes) : issue.publication.issueType;
    draft.labels = labelsFor(issue);

    QStringList lines;
    if (issue.isImported()) {
        const ExternalRequirement& r = issue.requirement.data;
        lines << tr("Requerimiento GESREQ %1").arg(r.id);
        auto line = [&lines](const QString& name, const QString& value) {
            if (!value.trimmed().isEmpty()) lines << QStringLiteral("* %1: %2").arg(name, value.trimmed());
        };
        line(tr("Sistema"), r.system.isEmpty() ? r.systemCode : r.system);
        line(tr("Estado en GESREQ"), r.states.join(QStringLiteral(" + ")));
        line(tr("Descripción corta"), r.summary);
        if (r.assignedFrom.isValid())
            line(tr("Asignado a QA"), QStringLiteral("%1 – %2").arg(r.assignedFrom.toString(QStringLiteral("dd/MM/yyyy")),
                                                                   r.assignedUntil.toString(QStringLiteral("dd/MM/yyyy"))));
        line(tr("Solicitante"), r.requester);
        line(tr("Ficha"), r.detailUrl);
        if (!issue.requirement.detail.description.trimmed().isEmpty()) lines << QString() << issue.requirement.detail.description.trimmed();
        lines << QString();
    }
    if (!issue.notes.trimmed().isEmpty()) lines << tr("Notas de QA") << issue.notes.trimmed() << QString();
    lines << tr("Publicado desde QAflow · %1").arg(issue.id);
    draft.description = lines.join(QLatin1Char('\n'));
    return draft;
}

bool IssuePublishService::needsUpdate(const Issue& issue) const {
    if (!issue.isPublished() || issue.publication.linked) return false;   // lo vinculado lo escribió otro
    const IssueDraft draft = draftFor(issue);
    return draft.summary != issue.publication.publishedTitle || draft.description != issue.publication.publishedDescription;
}

void IssuePublishService::publish(const QString& issueId, const IssueDraft& draft, std::function<void(const Result&)> done) {
    const Issue* issue = m_issues.find(issueId);
    if (!issue) { done(Result{false, {}, {}, tr("El issue ya no existe"), false, false}); return; }
    if (!canPublish()) { done(Result{false, {}, {}, tr("Configura la conexión con el gestor y su proyecto en Ajustes"), false, false}); return; }
    const TrackerSettings settings = m_settings.tracker();
    TrackerIssueDraft request{draft.summary, draft.description, draft.issueType, draft.labels};
    m_tracker->publishIssue(settings, request, [this, issueId, draft, settings, done](const IssueResult& r) {
        Result out;
        out.error = r.error;
        out.retryable = r.retryable;
        if (!r.ok) {
            // Un corte de red deja el envío sin confirmar: puede haberse creado en el gestor igualmente.
            out.uncertain = r.retryable;
            m_issues.updateIssue(issueId, [&](Issue& i) {
                i.publication.uncertain = out.uncertain;
                i.publication.lastError = r.error;
            });
            done(out);
            return;
        }
        out.ok = true;
        out.key = r.key;
        out.url = r.url;
        m_issues.updateIssue(issueId, [&](Issue& i) {
            IssuePublication& p = i.publication;
            p.tracker = toString(settings.kind);
            p.baseUrl = settings.baseUrl();
            p.project = settings.project.trimmed();
            p.key = r.key;
            p.url = r.url;
            p.issueType = draft.issueType;
            p.publishedAt = QDateTime::currentDateTime();
            p.publishedTitle = draft.summary;
            p.publishedDescription = draft.description;
            p.linked = false;
            p.uncertain = false;
            p.lastError.clear();
        });
        done(out);
    });
}

void IssuePublishService::update(const QString& issueId, const IssueDraft& draft, std::function<void(const Result&)> done) {
    const Issue* issue = m_issues.find(issueId);
    if (!issue || !issue->isPublished()) { done(Result{false, {}, {}, tr("El issue no está publicado"), false, false}); return; }
    if (!canPublish()) { done(Result{false, {}, {}, tr("Configura la conexión con el gestor y su proyecto en Ajustes"), false, false}); return; }
    const QString key = issue->publication.key;
    const QString url = issue->publication.url;
    TrackerIssueDraft request{draft.summary, draft.description, draft.issueType, draft.labels};
    m_tracker->updateIssue(m_settings.tracker(), key, request, [this, issueId, draft, key, url, done](const IssueResult& r) {
        Result out;
        out.key = key;
        out.url = url;
        out.error = r.error;
        out.retryable = r.retryable;
        if (!r.ok) { done(out); return; }
        out.ok = true;
        m_issues.updateIssue(issueId, [&](Issue& i) {
            i.publication.publishedTitle = draft.summary;
            i.publication.publishedDescription = draft.description;
            i.publication.publishedAt = QDateTime::currentDateTime();
            i.publication.lastError.clear();
        });
        done(out);
    });
}

void IssuePublishService::link(const QString& issueId, const QString& key, std::function<void(const Result&)> done) {
    const Issue* issue = m_issues.find(issueId);
    if (!issue) { done(Result{false, {}, {}, tr("El issue ya no existe"), false, false}); return; }
    const QString clean = key.trimmed();
    if (clean.isEmpty()) { done(Result{false, {}, {}, tr("Indica la clave del issue del gestor"), false, false}); return; }
    if (!canPublish()) { done(Result{false, {}, {}, tr("Configura la conexión con el gestor y su proyecto en Ajustes"), false, false}); return; }
    const TrackerSettings settings = m_settings.tracker();
    m_tracker->fetchIssue(settings, clean, [this, issueId, clean, settings, done](const TrackerIssueInfo& info) {
        Result out;
        out.error = info.error;
        out.retryable = info.retryable;
        if (!info.ok) { done(out); return; }
        out.ok = true;
        out.key = info.key.isEmpty() ? clean : info.key;
        out.url = info.url;
        m_issues.updateIssue(issueId, [&](Issue& i) {
            IssuePublication& p = i.publication;
            p.tracker = toString(settings.kind);
            p.baseUrl = settings.baseUrl();
            p.project = settings.project.trimmed();
            p.key = out.key;
            p.url = out.url;
            p.issueType = info.issueType;
            p.publishedAt = QDateTime::currentDateTime();
            // Lo vinculado no salió de QAflow: no se guarda como «lo último publicado» ni se ofrece actualizarlo.
            p.publishedTitle.clear();
            p.publishedDescription.clear();
            p.status = info.status;
            p.resolved = info.resolved;
            p.statusCheckedAt = QDateTime::currentDateTime();
            p.linked = true;
            p.uncertain = false;
            p.lastError.clear();
        });
        done(out);
    });
}

void IssuePublishService::unlink(const QString& issueId) {
    const Issue* issue = m_issues.find(issueId);
    if (!issue || !issue->isPublished()) return;
    m_issues.updateIssue(issueId, [](Issue& i) { i.publication = IssuePublication{}; });
}

void IssuePublishService::refreshStatus(const QString& issueId, std::function<void(const Result&)> done) {
    const Issue* issue = m_issues.find(issueId);
    if (!issue || !issue->isPublished()) { done(Result{false, {}, {}, tr("El issue no está publicado"), false, false}); return; }
    const QString key = issue->publication.key;
    const QString url = issue->publication.url;
    m_tracker->fetchStatus(m_settings.tracker(), key, [this, issueId, key, url, done](const IssueStatus& s) {
        Result out;
        out.key = key;
        out.url = url;
        out.error = s.error;
        if (!s.ok) { done(out); return; }
        out.ok = true;
        m_issues.updateIssue(issueId, [&](Issue& i) {
            i.publication.status = s.status;
            i.publication.resolved = s.resolved;
            i.publication.statusCheckedAt = QDateTime::currentDateTime();
        });
        done(out);
    });
}

void IssuePublishService::fetchIssueTypes(std::function<void(const QStringList&)> done) {
    const QString key = metadataKey();
    if (key == m_issueTypesFor) { done(m_issueTypes); return; }
    if (!canPublish()) { done({}); return; }
    m_tracker->fetchMetadata(m_settings.tracker(), [this, key, done](const MetadataResult& r) {
        if (r.ok) {
            m_issueTypes = r.metadata.issueTypes;
            m_issueTypesFor = key;
        }
        done(m_issueTypes);
    });
}

} // namespace qaflow
