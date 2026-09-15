#pragma once

#include "core/models/Issue.h"   // QaOutcome

#include <QDialog>

class QCheckBox;
class QComboBox;

namespace qaflow {

class TextArea;

/// Lo que se va a mandar del resultado de una revisión, para revisarlo antes: a dónde va, con qué
/// resultado, el texto que se envía y si se adjunta el acta.
///
/// Sirve para los dos destinos: el issue del gestor (un comentario con el acta adjunta) y GESREQ, donde
/// además se elige el resultado y se avisa de que registrarlo **cambia el estado del requerimiento**.
class RevisionResultDialog : public QDialog {
    Q_OBJECT
public:
    enum class Target { Tracker, Requirement };

    /// `destination` es dónde va ("Jira · SUMA2-2907", "GESREQ · GREQ 2026997"); `documentPath`, el acta
    /// generada (vacía si todavía no hay ninguna).
    RevisionResultDialog(Target target, const QString& destination, QaOutcome outcome, const QString& comment,
                         const QString& documentPath, QWidget* parent = nullptr);

    QaOutcome outcome() const;
    QString comment() const;
    /// El acta que hay que adjuntar; vacía si no hay o si se desmarcó.
    QString documentPath() const;

private:
    QaOutcome m_outcome;
    QString m_documentPath;
    QComboBox* m_outcomeBox = nullptr;   // sólo en GESREQ: el gestor recibe el resultado ya decidido
    TextArea* m_comment;
    QCheckBox* m_attach = nullptr;       // sólo si hay acta que adjuntar
};

} // namespace qaflow
