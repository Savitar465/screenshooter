#pragma once

#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QWidget>

/// Fábricas pequeñas para construir la UI con los "roles" definidos en app.qss.
namespace qaflow::ui {

void setRole(QWidget* w, const char* role);
void setFlag(QWidget* w, const char* name, bool value);   // propiedad dinámica + repolish
void repolish(QWidget* w);

QLabel* label(const QString& text, const char* role = nullptr, QWidget* parent = nullptr);
QLabel* pill(const QString& text, const QString& bg, const QString& fg, QWidget* parent = nullptr);
QPushButton* button(const QString& text, const char* role, QWidget* parent = nullptr);
QFrame* card(const char* role = "card", QWidget* parent = nullptr);
QFrame* accentBar(const QString& color, QWidget* parent = nullptr);
QFrame* dot(const QString& color, int size, QWidget* parent = nullptr);
QWidget* hstretch(QWidget* parent = nullptr);

QVBoxLayout* vbox(QWidget* parent, int margin = 0, int spacing = 0);
QHBoxLayout* hbox(QWidget* parent, int margin = 0, int spacing = 0);

/// QScrollArea vertical con un contenedor interno y su layout listo para usar.
QScrollArea* scrollArea(QWidget** content, QVBoxLayout** layout, QWidget* parent = nullptr);
/// Mete un widget ya construido en un scroll vertical: mientras cabe pide el tamaño entero de su
/// contenido, y si no (una ventana baja o el tamaño de la interfaz aumentado) se desplaza en vez de
/// cortarse por abajo. A lo ancho no se desplaza salvo que se active su barra horizontal.
QScrollArea* scrollable(QWidget* content, QWidget* parent = nullptr);
/// Monta un diálogo en dos partes: devuelve el layout del formulario, dentro de un `scrollable`, y en
/// `footer` el de debajo, para los botones, que quedan siempre a la vista. `margin` y `spacing` son los
/// que tendría el layout del diálogo entero.
QVBoxLayout* scrollingBody(QWidget* dialog, int margin, int spacing, QVBoxLayout** footer);

/// Botón pequeño con el icono de copiar: pone `text` en el portapapeles y lo confirma con un tooltip.
/// `size` es el lado del botón; el texto se cambia después con `setCopyText`.
QPushButton* copyButton(const QString& text, int size = 28);
void setCopyText(QPushButton* button, const QString& text);

void clearLayout(QLayout* layout);
QString elide(const QString& s, int max);

/// Icono de la aplicación (ventana y bandeja), dibujado al vuelo para no depender del plugin SVG.
QIcon appIcon();

} // namespace qaflow::ui
