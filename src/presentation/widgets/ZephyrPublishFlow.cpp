#include "ZephyrPublishFlow.h"

#include "application/TestPublishService.h"
#include "presentation/theme/Theme.h"

#include <QMessageBox>
#include <QPointer>

namespace qaflow {

namespace {
QString when(const QDateTime& dt) { return dt.isValid() ? dt.toString(QStringLiteral("dd/MM/yyyy HH:mm")) : QStringLiteral("—"); }
} // namespace

void ZephyrPublishFlow::run(QWidget* parent, TestPublishService& service, const PlanReport& report, bool update, const Toast& toast) {
    if (report.executed <= 0) {
        toast(tr("El ciclo no tiene ninguna ejecución que publicar"), theme::Amber);
        return;
    }
    const PlanRun& plan = report.plan;
    const QStringList nuevos = service.casesNeedingTest(report);
    QString aviso;
    if (update) {
        aviso = tr("Se actualizará el ciclo %1 de Zephyr con las %2 ejecuciones del informe: el veredicto de cada caso y de cada paso, y las evidencias que aún no estén subidas.")
                    .arg(plan.zephyrCycleId).arg(report.executed);
        if (nuevos.isEmpty()) aviso += tr("\n\nCada ejecución va sobre el Test que ya tiene.");
        else aviso += tr("\n\n%1 ejecuciones se quedaron sin Test la vez anterior y estrenarán el suyo: %2.").arg(nuevos.size()).arg(nuevos.join(QStringLiteral(", ")));
    } else {
        aviso = tr("Se creará el ciclo «%1» en Zephyr con %2 ejecuciones.").arg(service.requestFor(report).cycleName).arg(report.executed);
        if (!nuevos.isEmpty())
            aviso += tr("\n\nAntes se crearán en Jira %1 Tests, uno por ejecución, a partir de los casos: %2.\nCada informe enlaza sus propios Tests; los de otros ciclos no se tocan.")
                         .arg(nuevos.size()).arg(nuevos.join(QStringLiteral(", ")));
        // Publicar dos veces no actualiza el ciclo anterior: crea otro. Mejor decirlo antes.
        if (plan.isPublished())
            aviso += tr("\n\nOJO: estos resultados ya se publicaron el %1 (ciclo %2). Se creará un ciclo nuevo, no se actualiza aquél; para eso está «Actualizar en Zephyr».")
                         .arg(when(plan.publishedAt), plan.zephyrCycleId);
    }
    const QString title = update ? tr("Actualizar en Zephyr") : tr("Publicar en Zephyr");
    if (QMessageBox::question(parent, title, aviso, QMessageBox::Ok | QMessageBox::Cancel) != QMessageBox::Ok) return;

    toast(update ? tr("Actualizando en Zephyr…") : tr("Publicando en Zephyr…"), theme::Cyan);
    QPointer<QWidget> owner(parent);
    auto onDone = [owner, toast, update](const PublishResult& r) {
        if (!r.ok) {
            QString error = (update ? tr("No se pudo actualizar el ciclo en Zephyr · %1") : tr("No se pudo publicar en Zephyr · %1")).arg(r.error);
            if (r.retryable) error += tr(" · vuelve a intentarlo");
            toast(error, theme::Red);
            return;
        }
        QString msg = (update ? tr("Ciclo %1 actualizado en Zephyr · %2 ejecuciones, %3 pasos, %4 evidencias")
                              : tr("Ciclo %1 publicado en Zephyr · %2 ejecuciones, %3 pasos, %4 evidencias"))
                          .arg(r.cycleId).arg(r.executions).arg(r.steps).arg(r.attachments);
        if (r.testsCreated > 0) msg += tr(" · %1 Tests creados").arg(r.testsCreated);
        if (!r.skipped.isEmpty()) msg += tr(" · %1 sin publicar").arg(r.skipped.size());
        toast(msg, r.skipped.isEmpty() ? theme::Green : theme::Amber);
        // Un contador no dice qué arreglar: lo que se quedó fuera va con su motivo, uno por línea.
        if (!r.skipped.isEmpty() && owner) {
            QMessageBox box(QMessageBox::Warning, tr("Publicado con salvedades"),
                            (update ? tr("El ciclo se actualizó en Zephyr, pero %1 cosas se quedaron fuera.")
                                    : tr("El ciclo se creó en Zephyr, pero %1 cosas se quedaron fuera.")).arg(r.skipped.size()),
                            QMessageBox::Ok, owner);
            box.setDetailedText(r.skipped.join(QLatin1Char('\n')));
            box.exec();
        }
    };
    if (update) service.update(report, onDone);
    else service.publish(report, onDone);
}

} // namespace qaflow
