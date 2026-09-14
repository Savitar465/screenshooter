#pragma once

#include <QDialog>
#include <QList>
#include <QString>

#include <functional>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace qaflow {

/// Una opción del selector: lo que se guarda al elegirla, su nombre y un dato secundario.
struct Choice {
    QString value;   // SHOP, SUMA TRANSITO
    QString title;   // Tienda online, TRANSITOS
    QString hint;    // "en tu bandeja"; vacío si no hay nada que destacar
};

/// Selector con búsqueda sobre una lista que se consulta a un sistema externo (los proyectos de Jira, el
/// catálogo de sistemas de GESREQ). La consulta se lanza al abrir y se puede repetir si falla; la búsqueda
/// filtra en local por código y nombre, palabra a palabra y sin tildes, y las flechas mueven la selección
/// sin salir del campo de búsqueda. Al elegir emite `chosen` y se cierra.
class ChoiceDialog : public QDialog {
    Q_OBJECT
public:
    /// Con qué responde la consulta: la lista, o un error para mostrar (con la lista vacía).
    using Loaded = std::function<void(const QList<Choice>& choices, const QString& error)>;
    using Loader = std::function<void(const Loaded& done)>;

    /// `current` queda marcado si está en la lista; `loadingText` se enseña mientras llega la respuesta.
    ChoiceDialog(const QString& title, const QString& loadingText, Loader loader, const QString& current, QWidget* parent = nullptr);

    /// ¿Encaja la opción con lo buscado? Cada palabra tiene que aparecer en el código o en el nombre.
    static bool matches(const Choice& choice, const QString& query);

    void accept() override;

signals:
    void chosen(const QString& value);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void load();
    void showChoices();

    Loader m_load;
    QString m_current;
    QString m_loadingText;
    QList<Choice> m_choices;
    bool m_loaded = false;
    int m_generation = 0;   // la respuesta de una consulta anterior a la última se descarta
    QLineEdit* m_search;
    QListWidget* m_list;
    QLabel* m_status;
    QPushButton* m_retry;
    QPushButton* m_choose;
};

} // namespace qaflow
