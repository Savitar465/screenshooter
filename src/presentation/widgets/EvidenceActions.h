#pragma once

#include "core/models/TestCase.h"

#include <QString>
#include <QStringList>

class QWidget;

namespace qaflow {

class TestCaseStore;
class EvidenceService;
class ShotCard;

/// Acciones sobre evidencias compartidas por las tres pantallas que las muestran (Casos,
/// Ejecución, Reportar bug): abrir a tamaño completo, anotar, copiar, mostrar en la carpeta y
/// elegir ficheros para adjuntar. Las vistas sólo conectan sus tarjetas con `wireCard()`.
namespace evidence {

/// Visor con todas las evidencias del caso, abierto en `shotId`.
void openViewer(QWidget* parent, TestCaseStore& cases, EvidenceService& service, const QString& caseId, int shotId);
/// Editor de anotaciones; al guardar sustituye el fichero. Devuelve true si se guardó.
bool annotate(QWidget* parent, TestCaseStore& cases, EvidenceService& service, const QString& caseId, int shotId);
void copyToClipboard(EvidenceService& service, TestCaseStore& cases, const QString& caseId, int shotId);
void showInFolder(TestCaseStore& cases, const QString& caseId, int shotId);
/// Diálogo «Adjuntar archivos» (logs, vídeos, imágenes…). Devuelve las rutas elegidas.
QStringList pickFiles(QWidget* parent);

/// Conecta las señales de una tarjeta con las acciones anteriores (paso, mover y eliminar
/// siguen siendo cosa de la vista).
void wireCard(ShotCard* card, QWidget* parent, TestCaseStore& cases, EvidenceService& service, const QString& caseId);

} // namespace evidence
} // namespace qaflow
