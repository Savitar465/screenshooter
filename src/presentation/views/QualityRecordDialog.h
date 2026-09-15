#pragma once

#include "core/models/Issue.h"       // QaOutcome
#include "core/models/QualityRecord.h"

#include <QDialog>
#include <QList>

class QCheckBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QSpinBox;
class QVBoxLayout;

namespace qaflow {

class TextArea;

/// El acta de control de calidad (R-213) antes de generarla: QAflow propone lo que sabe del
/// requerimiento, de los casos, de las ejecuciones y de los bugs, y aquí se corrige y se completa lo
/// que el sistema no tiene (servidor, base de datos, módulo…). Lo escrito se guarda en la revisión,
/// así que la próxima acta del proyecto parte de ello.
class QualityRecordDialog : public QDialog {
    Q_OBJECT
public:
    /// `outcome` es el resultado propuesto y `blockers`, por qué se propone (se enseñan arriba).
    QualityRecordDialog(const QualityRecord& record, QaOutcome outcome, const QStringList& blockers, QWidget* parent = nullptr);

    /// El acta con lo que hay ahora en el formulario.
    QualityRecord record() const;

private:
    void buildGeneral(QVBoxLayout* v);
    void buildSummary(QVBoxLayout* v);
    void buildDetails(QVBoxLayout* v);
    void buildResults(QVBoxLayout* v);
    void refreshTotals();
    void pickLogo();
    void addImage();
    /// Añade una captura a la lista de la ejecución (con su botón de quitar); ignora las repetidas.
    void appendImage(const QString& path);

    QualityRecord m_record;   // lo que no se edita aquí (greq, proceso) viaja tal cual

    QLineEdit* m_system;
    QLineEdit* m_moduleLink;
    QLineEdit* m_server;
    QLineEdit* m_dbAccess;
    QLineEdit* m_dbSchema;
    QLineEdit* m_dbUser;
    QLineEdit* m_appUser;
    QLineEdit* m_tables;
    QLineEdit* m_functions;
    TextArea* m_description;
    QLineEdit* m_developedBy;
    QLineEdit* m_qaResource;
    QLineEdit* m_department;
    QSpinBox* m_revision;
    QDateEdit* m_from;
    QDateEdit* m_to;
    QLineEdit* m_logo;

    struct ObservationRow {
        QString type;
        QSpinBox* observations;
        QSpinBox* corrections;
    };
    QList<ObservationRow> m_observations;
    QLabel* m_totals;

    TextArea* m_caseDesign;
    TextArea* m_execution;
    TextArea* m_bugs;
    QStringList m_images;
    QVBoxLayout* m_imageList;

    struct CharacteristicRow {
        QString text;
        QCheckBox* satisfied;
        QLineEdit* note;
    };
    QList<CharacteristicRow> m_characteristics;
    TextArea* m_generalNotes;
};

} // namespace qaflow
