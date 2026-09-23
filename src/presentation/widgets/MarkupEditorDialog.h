#pragma once

#include <QDialog>

class QPlainTextEdit;
class QTextBrowser;

namespace qaflow {

/// Editor de un campo de texto con el marcado wiki de Jira, que es el que Zephyr enseña con formato
/// en los pasos: barra con negrita, cursiva, subrayado, tachado, monoespaciado, color, listas, tablas
/// y enlaces, y vista previa al lado. Lo que se guarda es el marcado, tal cual se publica.
///
/// Los botones envuelven la selección (o insertan la marca con un texto de ejemplo seleccionado);
/// «Tabla» convierte en tabla lo seleccionado si viene de una hoja de cálculo (separado por
/// tabuladores) y si no, pregunta filas y columnas.
class MarkupEditorDialog : public QDialog {
    Q_OBJECT
public:
    MarkupEditorDialog(const QString& title, const QString& text, QWidget* parent = nullptr);

    QString text() const;

    // Acciones de la barra (públicas para las pruebas)
    /// Envuelve la selección con `open`/`close`; sin selección, inserta `placeholder` seleccionado.
    void wrap(const QString& open, const QString& close, const QString& placeholder);
    /// Antepone `marker` a cada línea seleccionada (o a la línea del cursor).
    void prefixLines(const QString& marker);
    /// Inserta una tabla: la selección separada por tabuladores, o una vacía de `rows` × `cols`.
    void insertTable(int rows, int cols);

private:
    /// Inserta `block` (una tabla) en sus propias líneas, sustituyendo la selección.
    void insertBlock(const QString& block);
    void askTable();
    void askLink();
    void pickColor();
    void updatePreview();

    QPlainTextEdit* m_edit;
    QTextBrowser* m_preview;
};

} // namespace qaflow
