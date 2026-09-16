#pragma once

#include "application/RevisionPublishService.h"
#include "core/models/Issue.h"   // QaOutcome

#include <QDialog>
#include <QHash>
#include <QList>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QVBoxLayout;

namespace qaflow {

class TextArea;

/// Publicar el resultado de una revisión terminada: en una sola pantalla se ve qué va a cada destino
/// (los ciclos de los planes a Zephyr con sus casos, el resultado y el acta al issue del gestor, y el
/// registro en GESREQ), se elige cuáles se hacen y se ve cómo termina cada uno.

///
/// Nada se manda hasta pulsar «Publicar», y lo que ya se hizo en esta revisión viene desmarcado para
/// no repetirlo sin querer. Registrar en GESREQ cambia el estado del requerimiento: se avisa aquí.
class RevisionPublishDialog : public QDialog {
    Q_OBJECT
public:
    RevisionPublishDialog(RevisionPublishService& service, const QString& issueId, QaOutcome outcome,
                          const QString& comment, const QString& documentPath, QWidget* parent = nullptr);

signals:
    /// La publicación terminó; `ok` es que todos los pasos elegidos salieron bien.
    void published(bool ok);

private:
    void buildSteps(QVBoxLayout* v);
    void start();
    void showOutcome(const RevisionPublishService::Outcome& outcome);
    /// Vuelve a preguntar si GESREQ aceptaría el registro con el resultado y el acta de ahora, y lo
    /// dice en su paso: cambiar el resultado puede desbloquearlo (o bloquearlo).
    void refreshRequirementStep();
    /// El acta que se adjuntaría ahora mismo; vacía si no hay o si se desmarcó.
    QString documentPath() const;
    QaOutcome outcome() const;


    RevisionPublishService& m_service;
    QString m_issueId;
    QString m_documentPath;
    QList<RevisionPublishService::Step> m_steps;
    QHash<int, QCheckBox*> m_choices;    // destino → su casilla
    QHash<int, QLabel*> m_status;        // destino → cómo terminó
    QHash<int, QLabel*> m_details;       // destino → qué se haría
    QHash<int, QString> m_baseDetails;   // destino → ese texto, para volver a él
    QComboBox* m_outcome;
    TextArea* m_comment;
    QCheckBox* m_attach = nullptr;
    QPushButton* m_publish;
    QPushButton* m_close;
    bool m_running = false;
};

} // namespace qaflow
