#pragma once

#include "core/models/Issue.h"       // QaOutcome
#include "core/models/QualityRecord.h"

#include <QDialog>
#include <QList>
#include <QString>

#include <functional>

class QCheckBox;
class QComboBox;
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
    /// Una ejecución de plan entre las que elegir de cuál habla el acta.
    struct CycleChoice {
        QString planRunId;   // vacío = todos los ciclos de la revisión
        QString text;        // "Regresión Sprint 14 · 12/05/2026 · 4 de 4 ejecutados"
    };

    /// `outcome` es el resultado propuesto y `blockers`, por qué se propone (se enseñan arriba).
    /// `cycles` son las ejecuciones de los planes del issue en esta revisión: al cambiar de una a otra
    /// el acta se rehace con `redraft`, que devuelve la propuesta para ese ciclo.
    QualityRecordDialog(const QualityRecord& record, QaOutcome outcome, const QStringList& blockers,
                        const QList<CycleChoice>& cycles = {}, const QString& currentCycle = QString(),
                        std::function<QualityRecord(const QString& planRunId)> redraft = {}, QWidget* parent = nullptr);

    /// El acta con lo que hay ahora en el formulario.
    QualityRecord record() const;
    /// Ciclo del que habla el acta; vacío si habla de todos los de la revisión.
    QString planRunId() const;

private:
    void buildCycles(QVBoxLayout* v, const QList<CycleChoice>& cycles, const QString& currentCycle);
    /// Rellena el formulario con otra acta (al cambiar de ciclo). Lo que se hubiera escrito a mano en
    /// los campos que dependen de la ejecución se reemplaza: el acta habla de otro ciclo.
    void loadRecord(const QualityRecord& record);
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
    std::function<QualityRecord(const QString& planRunId)> m_redraft;
    QComboBox* m_cycles = nullptr;
    QLabel* m_proposal = nullptr;

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
