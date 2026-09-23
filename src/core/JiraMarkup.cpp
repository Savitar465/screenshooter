#include "JiraMarkup.h"

#include <QCoreApplication>
#include <QList>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <functional>

namespace qaflow::jira {

namespace {

// Los trozos que ya son HTML (código, enlaces, escapes) se apartan tras un marcador de uso privado
// para que el resto del marcado no los toque, y se reponen al final.
constexpr QChar kOpen(0xE000);
constexpr QChar kClose(0xE001);

struct Inline {
    QStringList held;

    QString hold(const QString& html) {
        held << html;
        return kOpen + QString::number(held.size() - 1) + kClose;
    }

    QString restore(QString s) const {
        static const QRegularExpression re(QStringLiteral("\\x{E000}(\\d+)\\x{E001}"));
        // Un trozo apartado puede contener otro (un enlace con código): se repite hasta que no quede ninguno.
        for (int guard = 0; guard < 4 && s.contains(kOpen); ++guard) {
            QString out;
            qsizetype last = 0;
            for (auto it = re.globalMatch(s); it.hasNext();) {
                const auto m = it.next();
                out += s.mid(last, m.capturedStart() - last);
                const int i = m.captured(1).toInt();
                out += i < held.size() ? held[i] : QString();
                last = m.capturedEnd();
            }
            s = out + s.mid(last);
        }
        return s;
    }
};

QString replaceAll(const QString& s, const QRegularExpression& re, const std::function<QString(const QRegularExpressionMatch&)>& with) {
    QString out;
    qsizetype last = 0;
    for (auto it = re.globalMatch(s); it.hasNext();) {
        const auto m = it.next();
        out += s.mid(last, m.capturedStart() - last);
        out += with(m);
        last = m.capturedEnd();
    }
    return out + s.mid(last);
}

bool safeUrl(const QString& url) {
    static const QRegularExpression re(QStringLiteral("^(https?|ftp|mailto|file):"), QRegularExpression::CaseInsensitiveOption);
    return re.match(url.trimmed()).hasMatch();
}

QString inlineHtml(const QString& text, const MarkupColors& colors) {
    Inline in;
    QString s = text;

    static const QRegularExpression mono(QStringLiteral("\\{\\{(.+?)\\}\\}"));
    s = replaceAll(s, mono, [&](const QRegularExpressionMatch& m) {
        return in.hold(QStringLiteral("<code style=\"background:%1;\">%2</code>").arg(colors.codeBg, m.captured(1).toHtmlEscaped()));
    });

    static const QRegularExpression link(QStringLiteral("\\[([^\\]|]*)\\|([^\\]]+)\\]|\\[([^\\]|\\s]+)\\]"));
    s = replaceAll(s, link, [&](const QRegularExpressionMatch& m) {
        const bool titled = m.capturedLength(2) > 0;
        const QString url = (titled ? m.captured(2) : m.captured(3)).trimmed();
        const QString title = titled && !m.captured(1).trimmed().isEmpty() ? m.captured(1) : url;
        if (!safeUrl(url)) return in.hold(m.captured(0).toHtmlEscaped());
        return in.hold(QStringLiteral("<a href=\"%1\" style=\"color:%2;\">%3</a>").arg(url.toHtmlEscaped(), colors.link, title.toHtmlEscaped()));
    });

    static const QRegularExpression br(QStringLiteral("\\\\\\\\"));
    s = replaceAll(s, br, [&](const QRegularExpressionMatch&) { return in.hold(QStringLiteral("<br/>")); });
    static const QRegularExpression escaped(QStringLiteral("\\\\(.)"));
    s = replaceAll(s, escaped, [&](const QRegularExpressionMatch& m) { return in.hold(m.captured(1).toHtmlEscaped()); });

    s = s.toHtmlEscaped();

    static const QRegularExpression color(QStringLiteral("\\{color:(#[0-9a-fA-F]{3,6}|[a-zA-Z]+)\\}(.*?)\\{color\\}"));
    s = replaceAll(s, color, [](const QRegularExpressionMatch& m) {
        return QStringLiteral("<span style=\"color:%1;\">%2</span>").arg(m.captured(1), m.captured(2));
    });

    // Marcas de énfasis: pegadas al texto que encierran y sin letras a los lados, como en Jira
    // ("a - b - c" no es tachado; "*negrita*" sí).
    struct Mark { const char* marker; const char* open; const char* close; };
    static const Mark marks[] = {
        {"*", "<b>", "</b>"}, {"_", "<i>", "</i>"}, {"+", "<u>", "</u>"}, {"-", "<s>", "</s>"},
        {"^", "<sup>", "</sup>"}, {"~", "<sub>", "</sub>"}, {"??", "<i>— ", "</i>"},
    };
    for (const auto& mk : marks) {
        const QString m = QRegularExpression::escape(QString::fromLatin1(mk.marker));
        const QRegularExpression re(QStringLiteral("(?<![\\w])%1(?=\\S)(.+?)(?<=\\S)%1(?![\\w])").arg(m));
        s = replaceAll(s, re, [&](const QRegularExpressionMatch& match) {
            return QString::fromLatin1(mk.open) + match.captured(1) + QString::fromLatin1(mk.close);
        });
    }
    return in.restore(s);
}

struct Cell {
    bool header = false;
    QString text;
};

/// Celdas de una fila de tabla. Las barras dentro de un enlace, de {{código}} o escapadas no separan.
QList<Cell> parseRow(const QString& line) {
    QList<Cell> cells;
    const QString s = line.trimmed();
    int bracket = 0;
    bool mono = false;
    for (qsizetype i = 0; i < s.size(); ++i) {
        const QChar c = s[i];
        if (c == QLatin1Char('\\') && i + 1 < s.size()) {
            if (!cells.isEmpty()) cells.last().text += s.mid(i, 2);
            ++i;
            continue;
        }
        if (s.mid(i, 2) == QLatin1String("{{")) mono = true;
        else if (s.mid(i, 2) == QLatin1String("}}")) mono = false;
        if (c == QLatin1Char('[')) ++bracket;
        else if (c == QLatin1Char(']') && bracket > 0) --bracket;
        if (c == QLatin1Char('|') && bracket == 0 && !mono) {
            const bool header = s.mid(i, 2) == QLatin1String("||");
            cells.append(Cell{header, {}});
            if (header) ++i;
            continue;
        }
        if (!cells.isEmpty()) cells.last().text += c;
    }
    // La barra que cierra la fila abre una celda vacía que no existe.
    if (!cells.isEmpty() && cells.last().text.trimmed().isEmpty()) cells.removeLast();
    return cells;
}

class Renderer {
public:
    explicit Renderer(const MarkupColors& colors) : m_colors(colors) {}

    QString render(const QString& markup) {
        QString src = markup;
        src.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        const QStringList lines = src.split(QLatin1Char('\n'));

        static const QRegularExpression block(QStringLiteral("^\\s*\\{(code|noformat)(?::[^}]*)?\\}"));
        static const QRegularExpression heading(QStringLiteral("^\\s*h([1-6])\\.\\s+(.*)$"));
        static const QRegularExpression quote(QStringLiteral("^\\s*bq\\.\\s+(.*)$"));
        static const QRegularExpression rule(QStringLiteral("^\\s*-{4,}\\s*$"));
        static const QRegularExpression item(QStringLiteral("^\\s*([*#]+|-)\\s+(.*)$"));
        static const QRegularExpression row(QStringLiteral("^\\s*\\|"));

        for (qsizetype i = 0; i < lines.size(); ++i) {
            const QString& line = lines[i];
            if (const auto m = block.match(line); m.hasMatch()) {
                closeAll();
                const QString closing = QStringLiteral("{%1}").arg(m.captured(1));
                QString rest = line.mid(m.capturedEnd());
                QStringList body;
                while (true) {
                    const qsizetype end = rest.indexOf(closing);
                    if (end >= 0) { body << rest.left(end); break; }
                    body << rest;
                    if (++i >= lines.size()) break;
                    rest = lines[i];
                }
                if (!body.isEmpty() && body.first().trimmed().isEmpty()) body.removeFirst();
                if (!body.isEmpty() && body.last().trimmed().isEmpty()) body.removeLast();
                m_out += QStringLiteral("<pre style=\"background:%1;\">%2</pre>").arg(m_colors.codeBg, body.join(QLatin1Char('\n')).toHtmlEscaped());
                continue;
            }
            if (const auto m = heading.match(line); m.hasMatch()) {
                closeAll();
                m_out += QStringLiteral("<h%1>%2</h%1>").arg(m.captured(1), inlineHtml(m.captured(2), m_colors));
                continue;
            }
            if (const auto m = quote.match(line); m.hasMatch()) {
                closeAll();
                m_out += QStringLiteral("<blockquote>%1</blockquote>").arg(inlineHtml(m.captured(1), m_colors));
                continue;
            }
            if (rule.match(line).hasMatch()) {
                closeAll();
                m_out += QStringLiteral("<hr/>");
                continue;
            }
            if (const auto m = item.match(line); m.hasMatch()) {
                flushParagraph();
                closeTable();
                listItem(m.captured(1) == QLatin1String("-") ? QStringLiteral("*") : m.captured(1), m.captured(2));
                continue;
            }
            if (row.match(line).hasMatch()) {
                flushParagraph();
                closeList(0);
                tableRow(parseRow(line));
                continue;
            }
            if (line.trimmed().isEmpty()) {
                closeAll();
                continue;
            }
            closeList(0);
            closeTable();
            m_paragraph << inlineHtml(line, m_colors);
        }
        closeAll();
        return QStringLiteral("<div style=\"color:%1;\">%2</div>").arg(m_colors.text, m_out);
    }

private:
    void listItem(const QString& markers, const QString& text) {
        // Se cierran los niveles que no coinciden con los marcadores y se abren los que faltan.
        int common = 0;
        while (common < m_lists.size() && common < markers.size() && m_lists[common] == markers[common]) ++common;
        closeList(common);
        while (m_lists.size() < markers.size()) {
            const QChar kind = markers[m_lists.size()];
            m_out += kind == QLatin1Char('#') ? QStringLiteral("<ol>") : QStringLiteral("<ul>");
            m_lists << kind;
        }
        m_out += QStringLiteral("<li>%1</li>").arg(inlineHtml(text, m_colors));
    }

    void closeList(int keep) {
        while (m_lists.size() > keep) {
            m_out += m_lists.takeLast() == QLatin1Char('#') ? QStringLiteral("</ol>") : QStringLiteral("</ul>");
        }
    }

    void tableRow(const QList<Cell>& cells) {
        if (!m_table) {
            m_out += QStringLiteral("<table border=\"1\" cellspacing=\"0\" cellpadding=\"5\" style=\"border-collapse:collapse;border-color:%1;\">")
                         .arg(m_colors.border);
            m_table = true;
        }
        m_out += QStringLiteral("<tr>");
        for (const Cell& c : cells) {
            const QString body = inlineHtml(c.text.trimmed(), m_colors);
            if (c.header) m_out += QStringLiteral("<th style=\"background:%1;\">%2</th>").arg(m_colors.headerBg, body);
            else m_out += QStringLiteral("<td>%1</td>").arg(body);
        }
        m_out += QStringLiteral("</tr>");
    }

    void closeTable() {
        if (!m_table) return;
        m_out += QStringLiteral("</table>");
        m_table = false;
    }

    void flushParagraph() {
        if (m_paragraph.isEmpty()) return;
        m_out += QStringLiteral("<p>%1</p>").arg(m_paragraph.join(QStringLiteral("<br/>")));
        m_paragraph.clear();
    }

    void closeAll() {
        flushParagraph();
        closeList(0);
        closeTable();
    }

    MarkupColors m_colors;
    QString m_out;
    QStringList m_paragraph;
    QList<QChar> m_lists;
    bool m_table = false;
};

QString cellText(QString text) {
    text = text.trimmed();
    text.replace(QLatin1Char('|'), QStringLiteral("\\|"));
    return text.isEmpty() ? QStringLiteral(" ") : text;
}

QString rowText(const QStringList& cells, bool header) {
    const QString sep = header ? QStringLiteral("||") : QStringLiteral("|");
    QStringList out;
    for (const auto& c : cells) out << cellText(c);
    return sep + out.join(sep) + sep;
}

} // namespace

QString toHtml(const QString& markup, const MarkupColors& colors) {
    return Renderer(colors).render(markup);
}

QString tableFromTsv(const QString& tsv, bool header) {
    QString src = tsv;
    src.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    QStringList lines = src.split(QLatin1Char('\n'));
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty()) lines.removeLast();
    QList<QStringList> rows;
    qsizetype cols = 0;
    for (const auto& l : lines) {
        rows << l.split(QLatin1Char('\t'));
        cols = std::max(cols, rows.last().size());
    }
    QStringList out;
    for (qsizetype r = 0; r < rows.size(); ++r) {
        QStringList cells = rows[r];
        while (cells.size() < cols) cells << QString();
        out << rowText(cells, header && r == 0);
    }
    return out.join(QLatin1Char('\n'));
}

QString emptyTable(int rows, int cols) {
    rows = std::max(1, rows);
    cols = std::max(1, cols);
    QStringList head;
    for (int c = 1; c <= cols; ++c) head << QCoreApplication::translate("core", "Columna %1").arg(c);
    QStringList out{rowText(head, true)};
    for (int r = 0; r < rows; ++r) out << rowText(QStringList(cols, QString()), false);
    return out.join(QLatin1Char('\n'));
}

} // namespace qaflow::jira
