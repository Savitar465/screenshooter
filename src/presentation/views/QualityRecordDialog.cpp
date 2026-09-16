#include "QualityRecordDialog.h"

#include "core/models/BugReport.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/TextArea.h"
#include "presentation/widgets/Ui.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>

#include <algorithm>
#include <utility>

namespace qaflow {

namespace {

QWidget* field(const QString& title, QWidget* w) {
    auto* box = new QWidget;
    auto* v = ui::vbox(box, 0, 6);
    v->addWidget(ui::label(title.toUpper(), "eyebrow"));
    v->addWidget(w);
    return box;
}

QLineEdit* line(const QString& value, const QString& objectName = QString()) {
    auto* edit = new QLineEdit(value);
    if (!objectName.isEmpty()) edit->setObjectName(objectName);
    return edit;
}

QSpinBox* counter(int value) {
    auto* box = new QSpinBox;
    box->setRange(0, 999);
    box->setValue(value);
    box->setFixedWidth(90);
    return box;
}

} // namespace

QualityRecordDialog::QualityRecordDialog(const QualityRecord& record, QaOutcome outcome, const QStringList& blockers,
                                         const QList<CycleChoice>& cycles, const QString& currentCycle,
                                         std::function<QualityRecord(const QString& planRunId)> redraft, QWidget* parent)
    : QDialog(parent), m_record(record), m_redraft(std::move(redraft)) {
    setObjectName(QStringLiteral("qualityRecordDialog"));
    setWindowTitle(tr("Acta de control de calidad"));
    setWindowIcon(ui::appIcon());
    setMinimumSize(760, 640);

    auto* root = ui::vbox(this, 18, 12);
    root->addWidget(ui::label(tr("Revisión %1 del GREQ %2").arg(record.revisionNumber).arg(record.greq), "h2"));
    const QString summary = blockers.isEmpty()
                                ? tr("Resultado propuesto: %1").arg(label(outcome))
                                : tr("Resultado propuesto: %1 · %2").arg(label(outcome), blockers.join(QStringLiteral(" · ")));
    m_proposal = ui::label(summary, "muted-sm");
    m_proposal->setWordWrap(true);
    m_proposal->setStyleSheet(QStringLiteral("color:%1;").arg(outcome == QaOutcome::Conforme ? theme::Green : theme::AmberSoft));
    root->addWidget(m_proposal);

    buildCycles(root, cycles, currentCycle);

    QWidget* content = nullptr;
    QVBoxLayout* v = nullptr;
    QScrollArea* scroll = ui::scrollArea(&content, &v);
    v->setContentsMargins(0, 0, 12, 0);
    v->setSpacing(14);
    buildGeneral(v);
    buildSummary(v);
    buildDetails(v);
    buildResults(v);
    v->addStretch(1);
    root->addWidget(scroll, 1);

    auto* buttons = new QWidget;
    auto* h = ui::hbox(buttons, 0, 8);
    h->addStretch(1);
    auto* cancel = ui::button(tr("Cancelar"), "outline");
    auto* accept = ui::button(tr("Generar acta…"), "primary");
    accept->setObjectName(QStringLiteral("qualityRecordAccept"));
    accept->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(accept, &QPushButton::clicked, this, &QDialog::accept);
    h->addWidget(cancel);
    h->addWidget(accept);
    root->addWidget(buttons);
}

void QualityRecordDialog::buildCycles(QVBoxLayout* v, const QList<CycleChoice>& cycles, const QString& currentCycle) {
    if (cycles.isEmpty()) {
        auto* none = ui::label(tr("La revisión no tiene ninguna ejecución de plan: el acta sale sin resultados de pruebas."), "muted-sm");
        none->setWordWrap(true);
        v->addWidget(none);
        return;
    }
    m_cycles = new QComboBox;
    m_cycles->setObjectName(QStringLiteral("recordCycle"));
    for (const auto& cycle : cycles) m_cycles->addItem(cycle.text, cycle.planRunId);
    // Con varias ejecuciones se puede levantar el acta de una o de todas.
    if (cycles.size() > 1) m_cycles->addItem(tr("Todas las ejecuciones de la revisión"), QString());
    m_cycles->setCurrentIndex(std::max(0, m_cycles->findData(currentCycle)));
    connect(m_cycles, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!m_redraft) return;
        loadRecord(m_redraft(planRunId()));
    });
    v->addWidget(field(tr("Ejecución del plan con la que se levanta el acta"), m_cycles));
}

void QualityRecordDialog::loadRecord(const QualityRecord& record) {
    m_record = record;
    m_system->setText(record.system);
    m_moduleLink->setText(record.moduleLink);
    m_server->setText(record.server);
    m_dbAccess->setText(record.dbAccess);
    m_dbSchema->setText(record.dbSchema);
    m_dbUser->setText(record.dbUser);
    m_appUser->setText(record.appUser);
    m_tables->setText(record.tables);
    m_functions->setText(record.functions);
    m_description->setTextSilently(record.description);
    m_developedBy->setText(record.developedBy);
    m_qaResource->setText(record.qaResource);
    m_department->setText(record.department);
    m_revision->setValue(record.revisionNumber);
    if (record.from.isValid()) m_from->setDate(record.from);
    if (record.to.isValid()) m_to->setDate(record.to);
    m_caseDesign->setTextSilently(record.caseDesign);
    m_execution->setTextSilently(record.execution);
    m_bugs->setTextSilently(record.bugs);
    for (const auto& row : m_observations) {
        const auto it = std::find_if(record.observations.cbegin(), record.observations.cend(),
                                     [&row](const ObservationCount& o) { return o.type == row.type; });
        if (it == record.observations.cend()) continue;
        row.observations->setValue(it->observations);
        row.corrections->setValue(it->corrections);
    }
    refreshTotals();
}

void QualityRecordDialog::buildGeneral(QVBoxLayout* v) {
    v->addWidget(ui::label(tr("GENERALES"), "eyebrow"));

    m_system = line(m_record.system, QStringLiteral("recordSystem"));
    m_moduleLink = line(m_record.moduleLink);
    m_moduleLink->setPlaceholderText(tr("Repositorio o módulo revisado"));
    v->addWidget(field(tr("Sistema"), m_system));
    v->addWidget(field(tr("Enlace/módulo"), m_moduleLink));

    auto* grid = new QWidget;
    auto* g = new QGridLayout(grid);
    g->setContentsMargins(0, 0, 0, 0);
    g->setHorizontalSpacing(12);
    m_server = line(m_record.server);
    m_dbAccess = line(m_record.dbAccess);
    m_dbSchema = line(m_record.dbSchema);
    m_dbUser = line(m_record.dbUser);
    m_appUser = line(m_record.appUser);
    m_tables = line(m_record.tables);
    m_functions = line(m_record.functions);
    g->addWidget(field(tr("Servidor"), m_server), 0, 0);
    g->addWidget(field(tr("Acceso a la BD"), m_dbAccess), 0, 1);
    g->addWidget(field(tr("Esquema BD"), m_dbSchema), 0, 2);
    g->addWidget(field(tr("Usuario de BD"), m_dbUser), 1, 0);
    g->addWidget(field(tr("Usuario aplicación"), m_appUser), 1, 1);
    g->addWidget(field(tr("Tablas afectadas"), m_tables), 1, 2);
    g->addWidget(field(tr("Funciones afectadas"), m_functions), 2, 0);
    for (int i = 0; i < 3; ++i) g->setColumnStretch(i, 1);
    v->addWidget(grid);

    m_description = new TextArea(5);
    m_description->setTextSilently(m_record.description);
    v->addWidget(field(tr("Descripción con detalle"), m_description));

    auto* people = new QWidget;
    auto* pg = new QGridLayout(people);
    pg->setContentsMargins(0, 0, 0, 0);
    pg->setHorizontalSpacing(12);
    m_developedBy = line(m_record.developedBy);
    m_qaResource = line(m_record.qaResource);
    m_department = line(m_record.department);
    pg->addWidget(field(tr("Desarrollado por"), m_developedBy), 0, 0);
    pg->addWidget(field(tr("Recurso(s) QA"), m_qaResource), 0, 1);
    pg->addWidget(field(tr("Departamento o institución"), m_department), 1, 0, 1, 2);
    for (int i = 0; i < 2; ++i) pg->setColumnStretch(i, 1);
    v->addWidget(people);

    auto* dates = new QWidget;
    auto* dg = new QHBoxLayout(dates);
    dg->setContentsMargins(0, 0, 0, 0);
    dg->setSpacing(12);
    m_revision = new QSpinBox;
    m_revision->setRange(1, 99);
    m_revision->setValue(m_record.revisionNumber);
    m_from = new QDateEdit(m_record.from.isValid() ? m_record.from : QDate::currentDate());
    m_to = new QDateEdit(m_record.to.isValid() ? m_record.to : QDate::currentDate());
    for (auto* edit : {m_from, m_to}) {
        edit->setCalendarPopup(true);
        edit->setDisplayFormat(QStringLiteral("dd/MM/yyyy"));
    }
    dg->addWidget(field(tr("Número de revisión"), m_revision));
    dg->addWidget(field(tr("Revisión desde"), m_from));
    dg->addWidget(field(tr("Revisión hasta"), m_to));
    dg->addStretch(1);
    v->addWidget(dates);

    auto* logo = new QWidget;
    auto* lh = ui::hbox(logo, 0, 8);
    m_logo = line(m_record.logoPath);
    m_logo->setPlaceholderText(tr("Imagen del membrete (opcional)"));
    auto* pick = ui::button(tr("Elegir…"), "outline");
    connect(pick, &QPushButton::clicked, this, &QualityRecordDialog::pickLogo);
    lh->addWidget(m_logo, 1);
    lh->addWidget(pick);
    v->addWidget(field(tr("Membrete"), logo));
}

void QualityRecordDialog::buildSummary(QVBoxLayout* v) {
    v->addWidget(ui::label(tr("RESUMEN DE OBSERVACIONES"), "eyebrow"));
    auto* hint = ui::label(tr("Se cuentan los bugs de esta revisión por su clasificación; las correcciones son las "
                              "observaciones de revisiones anteriores ya cerradas. Se puede corregir a mano."),
                           "muted-sm");
    hint->setWordWrap(true);
    v->addWidget(hint);

    auto* table = new QWidget;
    auto* g = new QGridLayout(table);
    g->setContentsMargins(0, 0, 0, 0);
    g->setHorizontalSpacing(12);
    g->setVerticalSpacing(6);
    g->addWidget(ui::label(tr("TIPO"), "eyebrow"), 0, 0);
    g->addWidget(ui::label(tr("OBSERVACIONES"), "eyebrow"), 0, 1);
    g->addWidget(ui::label(tr("CORRECCIONES"), "eyebrow"), 0, 2);
    int row = 1;
    for (const auto& observation : m_record.observations) {
        auto* name = ui::label(BugReport::classificationLabel(observation.type));
        auto* observations = counter(observation.observations);
        auto* corrections = counter(observation.corrections);
        connect(observations, &QSpinBox::valueChanged, this, [this](int) { refreshTotals(); });
        connect(corrections, &QSpinBox::valueChanged, this, [this](int) { refreshTotals(); });
        g->addWidget(name, row, 0);
        g->addWidget(observations, row, 1);
        g->addWidget(corrections, row, 2);
        m_observations << ObservationRow{observation.type, observations, corrections};
        ++row;
    }
    g->setColumnStretch(0, 1);
    v->addWidget(table);

    m_totals = ui::label(QString(), "muted-sm");
    m_totals->setObjectName(QStringLiteral("recordTotals"));
    v->addWidget(m_totals);
    refreshTotals();
}

void QualityRecordDialog::buildDetails(QVBoxLayout* v) {
    v->addWidget(ui::label(tr("DETALLES DE LA REVISIÓN"), "eyebrow"));
    m_caseDesign = new TextArea(4);
    m_caseDesign->setTextSilently(m_record.caseDesign);
    m_execution = new TextArea(3);
    m_execution->setTextSilently(m_record.execution);
    m_bugs = new TextArea(4);
    m_bugs->setTextSilently(m_record.bugs);
    v->addWidget(field(tr("Elaboración de casos de prueba"), m_caseDesign));
    v->addWidget(field(tr("Ejecución de casos de pruebas"), m_execution));
    v->addWidget(field(tr("Bugs reportados"), m_bugs));

    auto* images = new QWidget;
    auto* iv = ui::vbox(images, 0, 6);
    m_imageList = ui::vbox(new QWidget, 0, 4);
    iv->addWidget(m_imageList->parentWidget());
    auto* add = ui::button(tr("Añadir imagen…"), "outline");
    connect(add, &QPushButton::clicked, this, &QualityRecordDialog::addImage);
    iv->addWidget(add, 0, Qt::AlignLeft);
    v->addWidget(field(tr("Capturas junto a la ejecución"), images));
    // Las que ya traía el acta se ven al abrir; las demás las añade el usuario.
    for (const auto& path : m_record.executionImages) appendImage(path);
}

void QualityRecordDialog::appendImage(const QString& path) {
    if (path.trimmed().isEmpty() || m_images.contains(path)) return;
    m_images << path;
    auto* row = new QWidget;
    auto* h = ui::hbox(row, 0, 8);
    h->addWidget(ui::label(QFileInfo(path).fileName(), "muted-sm"), 1);
    auto* remove = ui::button(tr("Quitar"), "outline");
    connect(remove, &QPushButton::clicked, this, [this, row, path]() {
        m_images.removeAll(path);
        row->deleteLater();
    });
    h->addWidget(remove);
    m_imageList->addWidget(row);
}

void QualityRecordDialog::buildResults(QVBoxLayout* v) {
    v->addWidget(ui::label(tr("RESULTADOS"), "eyebrow"));
    for (const auto& characteristic : m_record.characteristics) {
        auto* row = new QWidget;
        auto* h = ui::hbox(row, 0, 10);
        auto* satisfied = new QCheckBox(tr("Satisface"));
        satisfied->setChecked(characteristic.satisfied);
        auto* text = ui::label(characteristic.text);
        text->setWordWrap(true);
        auto* note = line(characteristic.note);
        note->setPlaceholderText(tr("Observación"));
        h->addWidget(text, 3);
        h->addWidget(satisfied);
        h->addWidget(note, 2);
        v->addWidget(row);
        m_characteristics << CharacteristicRow{characteristic.text, satisfied, note};
    }
    m_generalNotes = new TextArea(4);
    m_generalNotes->setTextSilently(m_record.generalNotes);
    v->addWidget(field(tr("Observaciones generales"), m_generalNotes));
}

void QualityRecordDialog::refreshTotals() {
    int observations = 0;
    int corrections = 0;
    for (const auto& row : m_observations) {
        observations += row.observations->value();
        corrections += row.corrections->value();
    }
    m_totals->setText(tr("Total: %1 observación(es) · %2 corrección(es)").arg(observations).arg(corrections));
}

void QualityRecordDialog::pickLogo() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Membrete del acta"), m_logo->text(),
                                                      tr("Imágenes (*.png *.jpg *.jpeg)"));
    if (!path.isEmpty()) m_logo->setText(path);
}

void QualityRecordDialog::addImage() {
    const QStringList paths = QFileDialog::getOpenFileNames(this, tr("Capturas del acta"), QString(),
                                                            tr("Imágenes (*.png *.jpg *.jpeg)"));
    for (const auto& path : paths) appendImage(path);
}

QString QualityRecordDialog::planRunId() const { return m_cycles ? m_cycles->currentData().toString() : QString(); }

QualityRecord QualityRecordDialog::record() const {
    QualityRecord record = m_record;
    record.system = m_system->text().trimmed();
    record.moduleLink = m_moduleLink->text().trimmed();
    record.server = m_server->text().trimmed();
    record.dbAccess = m_dbAccess->text().trimmed();
    record.dbSchema = m_dbSchema->text().trimmed();
    record.dbUser = m_dbUser->text().trimmed();
    record.appUser = m_appUser->text().trimmed();
    record.tables = m_tables->text().trimmed();
    record.functions = m_functions->text().trimmed();
    record.description = m_description->toPlainText();
    record.developedBy = m_developedBy->text().trimmed();
    record.qaResource = m_qaResource->text().trimmed();
    record.department = m_department->text().trimmed();
    record.revisionNumber = m_revision->value();
    record.from = m_from->date();
    record.to = m_to->date();
    record.logoPath = m_logo->text().trimmed();

    record.observations.clear();
    for (const auto& row : m_observations)
        record.observations << ObservationCount{row.type, row.observations->value(), row.corrections->value()};

    record.caseDesign = m_caseDesign->toPlainText();
    record.execution = m_execution->toPlainText();
    record.bugs = m_bugs->toPlainText();
    record.executionImages = m_images;

    record.characteristics.clear();
    for (const auto& row : m_characteristics)
        record.characteristics << QualityCharacteristic{row.text, row.satisfied->isChecked(), row.note->text().trimmed()};
    record.generalNotes = m_generalNotes->toPlainText();
    return record;
}

} // namespace qaflow
