#pragma once

#include <QFrame>
#include <QHBoxLayout>
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

void clearLayout(QLayout* layout);
QString elide(const QString& s, int max);

} // namespace qaflow::ui
