#include "ChoiceDialog.h"

#include "presentation/theme/Theme.h"
#include "presentation/widgets/Ui.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>

#include <algorithm>

namespace qaflow {

namespace {
/// Texto comparable: sin tildes, en minúsculas y con los espacios normalizados.
QString folded(const QString& text) {
    const QString decomposed = text.normalized(QString::NormalizationForm_D);
    QString out;
    out.reserve(decomposed.size());
    for (const QChar c : decomposed)
        if (c.category() != QChar::Mark_NonSpacing) out += c;
    return out.simplified().toLower();
}
} // namespace

ChoiceDialog::ChoiceDialog(const QString& title, const QString& loadingText, Loader loader, const QString& current, QWidget* parent)
    : QDialog(parent), m_load(std::move(loader)), m_current(current.simplified()), m_loadingText(loadingText) {
    setObjectName(QStringLiteral("choiceDialog"));
    setWindowTitle(title);
    setWindowIcon(ui::appIcon());
    setMinimumSize(480, 440);

    auto* v = ui::vbox(this, 18, 10);
    v->addWidget(ui::label(title, "h2"));
    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("choiceSearch"));
    m_search->setPlaceholderText(tr("Buscar por código o nombre…"));
    m_search->setClearButtonEnabled(true);
    m_search->installEventFilter(this);
    v->addWidget(m_search);
    m_list = new QListWidget;
    m_list->setObjectName(QStringLiteral("choiceList"));
    m_list->setUniformItemSizes(true);
    v->addWidget(m_list, 1);

    auto* statusRow = new QWidget;
    auto* sh = ui::hbox(statusRow, 0, 8);
    m_status = ui::label(QString(), "muted-sm");
    m_status->setObjectName(QStringLiteral("choiceStatus"));
    m_status->setWordWrap(true);
    sh->addWidget(m_status, 1);
    m_retry = ui::button(tr("Reintentar"), "outline");
    m_retry->setObjectName(QStringLiteral("choiceRetry"));
    sh->addWidget(m_retry, 0, Qt::AlignTop);
    v->addWidget(statusRow);

    auto* buttons = new QWidget;
    auto* bh = ui::hbox(buttons, 0, 8);
    bh->addStretch(1);
    auto* cancel = ui::button(tr("Cancelar"), "outline");
    m_choose = ui::button(tr("Elegir"), "primary");
    m_choose->setObjectName(QStringLiteral("choiceAccept"));
    // Intro en la búsqueda elige lo que está marcado.
    m_choose->setDefault(true);
    bh->addWidget(cancel);
    bh->addWidget(m_choose);
    v->addWidget(buttons);

    connect(m_search, &QLineEdit::textChanged, this, &ChoiceDialog::showChoices);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &ChoiceDialog::accept);
    connect(m_retry, &QPushButton::clicked, this, &ChoiceDialog::load);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_choose, &QPushButton::clicked, this, &ChoiceDialog::accept);
    load();
    m_search->setFocus();
}

bool ChoiceDialog::matches(const Choice& choice, const QString& query) {
    const QString haystack = folded(choice.value + QLatin1Char(' ') + choice.title);
    const QStringList words = folded(query).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return std::all_of(words.cbegin(), words.cend(), [&haystack](const QString& word) { return haystack.contains(word); });
}

void ChoiceDialog::load() {
    const int generation = ++m_generation;
    m_loaded = false;
    m_choices.clear();
    m_list->clear();
    m_retry->hide();
    m_choose->setEnabled(false);
    m_status->setText(m_loadingText);
    m_status->setStyleSheet(QString());
    QPointer<ChoiceDialog> self(this);
    m_load([self, generation](const QList<Choice>& choices, const QString& error) {
        // Mientras llegaba la respuesta pudo cerrarse la ventana o repetirse la consulta.
        if (!self || generation != self->m_generation) return;
        if (!error.isEmpty()) {
            self->m_status->setText(error);
            self->m_status->setStyleSheet(QStringLiteral("color:%1;").arg(theme::Red));
            self->m_retry->show();
            return;
        }
        self->m_choices = choices;
        self->m_loaded = true;
        self->showChoices();
    });
}

void ChoiceDialog::showChoices() {
    if (!m_loaded) return;   // consultando, o con el error a la vista
    const QString query = m_search->text();
    const QListWidgetItem* marked = m_list->currentItem();
    const QString keep = marked ? marked->data(Qt::UserRole).toString() : m_current;
    m_list->clear();
    int shown = 0;
    int keepRow = -1;
    for (const auto& c : m_choices) {
        if (!matches(c, query)) continue;
        QString text = c.title.isEmpty() || c.title.compare(c.value, Qt::CaseInsensitive) == 0 ? c.value : c.value + QStringLiteral("  —  ") + c.title;
        if (!c.hint.isEmpty()) text += QStringLiteral("   · ") + c.hint;
        auto* item = new QListWidgetItem(text, m_list);
        item->setData(Qt::UserRole, c.value);
        item->setToolTip(text);
        if (keepRow < 0 && c.value.compare(keep, Qt::CaseInsensitive) == 0) keepRow = shown;
        ++shown;
    }
    // Lo que ya estaba marcado lo sigue estando; si la búsqueda lo deja fuera, el primero, para elegir con Intro.
    if (shown > 0) {
        m_list->setCurrentRow(keepRow >= 0 ? keepRow : 0);
        m_list->scrollToItem(m_list->currentItem(), QAbstractItemView::PositionAtCenter);
    }
    m_choose->setEnabled(shown > 0);
    m_status->setStyleSheet(QString());
    if (m_choices.isEmpty()) m_status->setText(tr("No hay nada que elegir"));
    else if (query.trimmed().isEmpty()) m_status->setText(tr("%1 disponibles").arg(m_choices.size()));
    else m_status->setText(tr("%1 de %2").arg(shown).arg(m_choices.size()));
}

void ChoiceDialog::accept() {
    const QListWidgetItem* item = m_list->currentItem();
    if (!item) return;
    emit chosen(item->data(Qt::UserRole).toString());
    QDialog::accept();
}

bool ChoiceDialog::eventFilter(QObject* watched, QEvent* event) {
    // Flechas y avance de página en la búsqueda mueven la selección de la lista sin soltar el teclado.
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Up || key == Qt::Key_Down || key == Qt::Key_PageUp || key == Qt::Key_PageDown) {
            QCoreApplication::sendEvent(m_list, event);
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

} // namespace qaflow
