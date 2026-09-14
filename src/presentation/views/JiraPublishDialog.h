#pragma once

#include "application/IssuePublishService.h"

#include <QDialog>

class QComboBox;
class QLineEdit;
class QPushButton;

namespace qaflow {

class TextArea;

/// Lo que se va a crear (o reescribir) en el gestor, para revisarlo antes de enviarlo: título,
/// descripción y, al crear, el tipo de incidencia. La publicación es siempre explícita; al actualizar se
/// avisa de que el texto del gestor se reescribe con el de QAflow.
class JiraPublishDialog : public QDialog {
    Q_OBJECT
public:
    enum class Mode { Create, Update };

    /// `destination` es dónde va ("Jira · SHOP"); `issueTypes`, los del proyecto (puede estar vacía y
    /// entonces el tipo se escribe a mano).
    JiraPublishDialog(Mode mode, const QString& destination, const IssueDraft& draft, const QStringList& issueTypes,
                      QWidget* parent = nullptr);

    IssueDraft draft() const;
    void accept() override;

signals:
    void confirmed(const IssueDraft& draft);

private:
    Mode m_mode;
    IssueDraft m_draft;
    QLineEdit* m_summary;
    TextArea* m_description;
    QComboBox* m_issueType;
};

} // namespace qaflow
