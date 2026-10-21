#pragma once

#include <QPlainTextEdit>

class QPushButton;

namespace qaflow {

/// QPlainTextEdit con altura fija en líneas y señal de cambio de texto (equivalente a <textarea rows=N>).
class TextArea : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit TextArea(int rows, QWidget* parent = nullptr);
    void setTextSilently(const QString& text);

    /// La altura sigue al texto que tiene, ya partido en líneas al ancho del campo: de `minRows` a
    /// `maxRows` líneas; con más, se desplaza por dentro. Un paso o un título largo se ve entero sin
    /// que los cortos ocupen de más.
    void setAutoHeight(int minRows, int maxRows);
    /// Un solo párrafo (un título): Intro pasa al campo siguiente en vez de partir la línea y los
    /// saltos de línea pegados se vuelven espacios. El texto sí se parte en líneas a lo ancho.
    void setSingleParagraph(bool single);
    /// El botón del editor con formato sólo se ve mientras se escribe en el campo: en una lista de
    /// campos (los pasos) un lápiz en cada uno es ruido.
    void setMarkupButtonOnFocus(bool onFocus);

    /// Añade un botón (y una entrada en el menú contextual) que abre el campo en el editor con formato
    /// de Jira (`MarkupEditorDialog`), con `title` como título de la ventana.
    void enableMarkupEditor(const QString& title);
    /// Abre el editor con formato; si se acepta con otro texto, lo sustituye y emite `edited`.
    void openMarkupEditor();

    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void edited(const QString& text);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void insertFromMimeData(const QMimeData* source) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void changeEvent(QEvent* event) override;
    bool viewportEvent(QEvent* event) override;

private:
    void placeMarkupButton();
    void updateMarkupButton();
    void fitHeight();
    void refit();
    /// Lo que ocupa el texto, en píxeles, con el campo de ese ancho.
    int textHeight(int width) const;
    int lineHeight() const;

    bool m_silent = false;
    int m_minRows = 0;   // 0 = altura fija (la del constructor)
    int m_maxRows = 0;
    QSize m_chrome{30, 16};   // lo que mide el campo además de su viewport (ver fitHeight)
    int m_lastHeight = -1;
    bool m_single = false;
    bool m_markupOnFocus = false;
    QString m_markupTitle;
    QPushButton* m_markupButton = nullptr;
};

} // namespace qaflow
