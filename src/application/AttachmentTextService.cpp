#include "AttachmentTextService.h"

#include "application/RequirementSourceService.h"

#include <QCoreApplication>
#include <QPointer>

namespace qaflow {

AttachmentTextService::AttachmentTextService(RequirementSourceService& source, std::shared_ptr<IDocumentReader> reader, QObject* parent)
    : QObject(parent), m_source(source), m_reader(std::move(reader)) {}

void AttachmentTextService::read(const RequirementAttachment& attachment, std::function<void(const DocumentText&)> done) {
    if (const auto it = m_cache.constFind(attachment.url); it != m_cache.cend()) {
        done(DocumentText{true, it.value(), {}});
        return;
    }
    QPointer<AttachmentTextService> self(this);
    m_source.downloadAttachment(attachment, [self, attachment, done](const RequirementAttachmentResult& file) {
        if (!self) return;
        if (!file.ok) {
            done(DocumentText{false, {}, QCoreApplication::translate("application", "No se pudo descargar «%1» · %2")
                                             .arg(attachment.fileName, file.error)});
            return;
        }
        const QString name = file.fileName.isEmpty() ? attachment.fileName : file.fileName;
        self->m_reader->read(name, file.data, [self, url = attachment.url, done](const DocumentText& text) {
            if (self && text.ok) self->m_cache.insert(url, text.text);
            done(text);
        });
    });
}

} // namespace qaflow
