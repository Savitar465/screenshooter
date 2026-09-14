#include "GesreqParser.h"

#include <QCoreApplication>
#include <QHash>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace qaflow::gesreq {

namespace {
const QRegularExpression::PatternOptions kOptions =
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption;

/// Sin comentarios, scripts ni estilos. Los scripts de GESREQ traen plantillas con marcado
/// (<script type="text/js-tmpl">) que no es contenido de la página.
QString withoutComments(QString html) {
    static const QRegularExpression comments(QStringLiteral("<!--.*?-->"), kOptions);
    static const QRegularExpression scripts(QStringLiteral("<(script|style)\\b.*?</\\1\\s*>"), kOptions);
    html.remove(comments);
    html.remove(scripts);
    return html;
}

/// Sin las ventanas modales (`div.modalWindow`) que la ficha lleva dentro con el historial de cada
/// control: repiten etiquetas de los datos vigentes ("Observaciones", "Archivo") con valores de rondas
/// anteriores. Los div se anidan, así que se cuentan hasta cerrar el de la ventana.
QString withoutModals(const QString& html) {
    static const QRegularExpression modal(QStringLiteral("<div\\b[^>]*class\\s*=\\s*\"[^\"]*modalWindow[^\"]*\"[^>]*>"), kOptions);
    static const QRegularExpression div(QStringLiteral("<(/?)div\\b[^>]*>"), kOptions);
    QString out;
    qsizetype from = 0;
    for (QRegularExpressionMatch open = modal.match(html); open.hasMatch(); open = modal.match(html, from)) {
        out += html.mid(from, open.capturedStart() - from);
        int depth = 1;
        qsizetype end = html.size();   // una ventana sin cerrar se lleva el resto de la página
        for (auto it = div.globalMatch(html, open.capturedEnd()); it.hasNext();) {
            const QRegularExpressionMatch tag = it.next();
            depth += tag.captured(1).isEmpty() ? 1 : -1;
            if (depth == 0) { end = tag.capturedEnd(); break; }
        }
        from = end;
    }
    out += html.mid(from);
    return out;
}

QString decodeEntities(const QString& text) {
    if (!text.contains(QLatin1Char('&'))) return text;
    static const QHash<QString, QString> named{
        {QStringLiteral("amp"), QStringLiteral("&")},     {QStringLiteral("lt"), QStringLiteral("<")},
        {QStringLiteral("gt"), QStringLiteral(">")},      {QStringLiteral("quot"), QStringLiteral("\"")},
        {QStringLiteral("apos"), QStringLiteral("'")},    {QStringLiteral("nbsp"), QStringLiteral(" ")},
        {QStringLiteral("aacute"), QStringLiteral("á")},  {QStringLiteral("Aacute"), QStringLiteral("Á")},
        {QStringLiteral("eacute"), QStringLiteral("é")},  {QStringLiteral("Eacute"), QStringLiteral("É")},
        {QStringLiteral("iacute"), QStringLiteral("í")},  {QStringLiteral("Iacute"), QStringLiteral("Í")},
        {QStringLiteral("oacute"), QStringLiteral("ó")},  {QStringLiteral("Oacute"), QStringLiteral("Ó")},
        {QStringLiteral("uacute"), QStringLiteral("ú")},  {QStringLiteral("Uacute"), QStringLiteral("Ú")},
        {QStringLiteral("ntilde"), QStringLiteral("ñ")},  {QStringLiteral("Ntilde"), QStringLiteral("Ñ")},
        {QStringLiteral("uuml"), QStringLiteral("ü")},    {QStringLiteral("Uuml"), QStringLiteral("Ü")},
        {QStringLiteral("iexcl"), QStringLiteral("¡")},   {QStringLiteral("iquest"), QStringLiteral("¿")},
        {QStringLiteral("ordm"), QStringLiteral("º")},    {QStringLiteral("ordf"), QStringLiteral("ª")},
        {QStringLiteral("deg"), QStringLiteral("°")},     {QStringLiteral("laquo"), QStringLiteral("«")},
        {QStringLiteral("raquo"), QStringLiteral("»")},   {QStringLiteral("ndash"), QStringLiteral("–")},
        {QStringLiteral("mdash"), QStringLiteral("—")},   {QStringLiteral("hellip"), QStringLiteral("…")},
        {QStringLiteral("bull"), QStringLiteral("•")},    {QStringLiteral("middot"), QStringLiteral("·")},
        {QStringLiteral("ldquo"), QStringLiteral("“")},   {QStringLiteral("rdquo"), QStringLiteral("”")},
        {QStringLiteral("lsquo"), QStringLiteral("‘")},   {QStringLiteral("rsquo"), QStringLiteral("’")},
        {QStringLiteral("euro"), QStringLiteral("€")},
    };
    // El punto y coma es opcional: GESREQ escribe "&nbsp" a secas en algunas páginas.
    static const QRegularExpression entity(QStringLiteral("&(#[0-9]+|#[xX][0-9a-fA-F]+|[a-zA-Z][a-zA-Z0-9]*);?"));
    QString out;
    qsizetype from = 0;
    for (auto it = entity.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const QString name = m.captured(1);
        QString replacement;
        if (name.startsWith(QLatin1Char('#'))) {
            bool ok = false;
            const bool hex = name.size() > 1 && name.at(1).toLower() == QLatin1Char('x');
            const uint code = hex ? name.mid(2).toUInt(&ok, 16) : name.mid(1).toUInt(&ok, 10);
            if (ok && code > 0 && code <= 0x10FFFF) {
                const char32_t c = code;
                replacement = QString::fromUcs4(&c, 1);
            }
        } else {
            replacement = named.value(name);
        }
        if (replacement.isEmpty()) continue;   // desconocida ("&bandera=1" en un enlace): se deja tal cual
        out += text.mid(from, m.capturedStart() - from);
        out += replacement;
        from = m.capturedEnd();
    }
    out += text.mid(from);
    return out;
}

/// Clave con la que se comparan cabeceras y etiquetas: sin tildes, en minúsculas y con los espacios
/// normalizados, para que "Descripción  Corta" y "descripcion corta" sean la misma columna.
QString headerKey(const QString& html) {
    const QString decomposed = toPlainText(html).normalized(QString::NormalizationForm_D);
    QString out;
    out.reserve(decomposed.size());
    for (const QChar c : decomposed)
        if (c.category() != QChar::Mark_NonSpacing) out += c;
    return out.simplified().toLower();
}

QString fieldLabel(const QString& html) {
    QString label = toPlainText(html).simplified();
    while (label.endsWith(QLatin1Char(':'))) label = label.chopped(1).trimmed();
    return label;
}

/// Pares <th>etiqueta</th><td>valor</td> de un fragmento de la ficha. Los <th> con colspan que hacen de
/// cabecera ("Sistema TRANSITOS") no van seguidos de un <td> y se quedan fuera. Los enlaces de descarga
/// pasan a `attachments` y el valor del campo es el nombre de sus ficheros.
QList<RequirementField> fieldsIn(const QString& html, const QString& section, const RequirementSourceSettings& s,
                                 QList<RequirementAttachment>& attachments) {
    // Etiqueta y valor no pueden cruzar otra celda del mismo tipo: sin eso, un <th> de cabecera se
    // uniría con la etiqueta de la fila siguiente.
    static const QRegularExpression pair(QStringLiteral("<th\\b[^>]*>((?:(?!</?th\\b).)*)</th\\s*>\\s*"
                                                        "<td\\b[^>]*>((?:(?!</?td\\b).)*)</td\\s*>"),
                                         kOptions);
    static const QRegularExpression download(QStringLiteral("href\\s*=\\s*\"([^\"]*docDownload\\.do[^\"]*)\""), kOptions);
    QList<RequirementField> fields;
    for (auto it = pair.globalMatch(html); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        RequirementField field{fieldLabel(m.captured(1)), QString()};
        const QString cell = m.captured(2);
        QStringList files;
        for (auto links = download.globalMatch(cell); links.hasNext();) {
            const QString href = decodeEntities(links.next().captured(1)).trimmed();
            const QUrl url(href);
            const QString doc = QUrlQuery(url.query()).queryItemValue(QStringLiteral("doc"), QUrl::FullyDecoded);
            RequirementAttachment a;
            a.label = section.isEmpty() ? field.label : section + QStringLiteral(" · ") + field.label;
            a.fileName = (doc.isEmpty() ? url.path() : doc).section(QLatin1Char('/'), -1);
            a.url = s.resolve(href);
            files << a.fileName;
            const bool known = std::any_of(attachments.cbegin(), attachments.cend(),
                                           [&a](const RequirementAttachment& other) { return other.url == a.url; });
            if (!known) attachments << a;
        }
        field.value = files.isEmpty() ? toPlainText(cell) : files.join(QStringLiteral(", "));
        fields << field;
    }
    return fields;
}
} // namespace

QString detailPath(const QString& id) {
    return QStringLiteral("publico.do?id=%1&bandera=1").arg(id.trimmed());
}

bool isLoginPage(const QString& html) {
    static const QRegularExpression form(QStringLiteral("<form\\b[^>]*name\\s*=\\s*\"AuthForm\"|<input\\b[^>]*name\\s*=\\s*\"clave\""), kOptions);
    return form.match(withoutComments(html)).hasMatch();
}

bool isAuthenticatedPage(const QString& html) {
    static const QRegularExpression logout(QStringLiteral("href\\s*=\\s*\"[^\"]*logout\\.do"), kOptions);
    return logout.match(withoutComments(html)).hasMatch();
}

QString userName(const QString& html) {
    static const QRegularExpression user(QStringLiteral("<h4\\b[^>]*class\\s*=\\s*\"[^\"]*user-details[^\"]*\"[^>]*>(.*?)(?:<br\\b|</h4\\s*>)"), kOptions);
    return toPlainText(user.match(withoutComments(html)).captured(1)).simplified();
}

QString loginMessage(const QString& html) {
    // El formulario no tiene un sitio fijo para el motivo: puede venir en una llamada a Anb.message con
    // texto literal en el script de arranque o en un aviso de la página.
    static const QRegularExpression scriptCall(QStringLiteral("Anb\\.message\\.\\w+\\(\\s*([\"'])(.+?)\\1"), kOptions);
    const QRegularExpressionMatch call = scriptCall.match(html);
    if (call.hasMatch()) return toPlainText(call.captured(2)).simplified();
    static const QRegularExpression notice(QStringLiteral("<(div|span|p|label)\\b[^>]*class\\s*=\\s*\"[^\"]*(?:alert|error|anb-message-body)[^\"]*\"[^>]*>(.*?)</\\1\\s*>"),
                                           kOptions);
    for (auto it = notice.globalMatch(withoutComments(html)); it.hasNext();) {
        const QString text = toPlainText(it.next().captured(2)).simplified();
        if (!text.isEmpty()) return text;
    }
    return {};
}

InboxPage parseInbox(const QString& html, const RequirementSourceSettings& s) {
    InboxPage page;
    static const QRegularExpression tableRe(QStringLiteral("<table\\b[^>]*\\bid\\s*=\\s*\"main-table\"[^>]*>(.*?)</table\\s*>"), kOptions);
    const QRegularExpressionMatch table = tableRe.match(withoutComments(html));
    if (!table.hasMatch()) {
        page.error = QCoreApplication::translate("infrastructure", "no está la tabla de requerimientos");
        return page;
    }
    const QString content = table.captured(1);
    static const QRegularExpression tbody(QStringLiteral("<tbody\\b"), kOptions);
    const QRegularExpressionMatch bodyStart = tbody.match(content);
    const QString head = bodyStart.hasMatch() ? content.left(bodyStart.capturedStart()) : content;
    const QString body = bodyStart.hasMatch() ? content.mid(bodyStart.capturedStart()) : content;

    static const QRegularExpression headerRe(QStringLiteral("<th\\b[^>]*>(.*?)</th\\s*>"), kOptions);
    QStringList headers;
    for (auto it = headerRe.globalMatch(head); it.hasNext();) headers << headerKey(it.next().captured(1));
    auto column = [&headers](const char* key) { return int(headers.indexOf(QString::fromLatin1(key))); };
    const int colId = column("requerimiento");
    const int colSystem = column("sistema");
    const int colSummary = column("descripcion corta");
    const int colState = column("estado");
    const QList<QPair<int, QString>> required{{colId, QStringLiteral("Requerimiento")},
                                              {colSystem, QStringLiteral("Sistema")},
                                              {colSummary, QStringLiteral("Descripción Corta")},
                                              {colState, QStringLiteral("Estado")}};
    for (const auto& [index, name] : required) {
        if (index >= 0) continue;
        page.error = QCoreApplication::translate("infrastructure", "falta la columna «%1»").arg(name);
        return page;
    }
    const int colRequested = column("fecha solicitud");
    const int colFrom = column("fecha asignacion desde");
    const int colUntil = column("fecha asignacion hasta");
    const int colUnit = column("unidad solicitante");
    const int colRequester = column("funcionario solicitante");
    const int colUser = column("usuario");
    const int colPriority = column("prioridad");

    static const QRegularExpression rowRe(QStringLiteral("<tr\\b[^>]*>(.*?)</tr\\s*>"), kOptions);
    static const QRegularExpression cellRe(QStringLiteral("<td\\b[^>]*>(.*?)</td\\s*>"), kOptions);
    static const QRegularExpression stateRe(QStringLiteral("<span\\b[^>]*class\\s*=\\s*\"[^\"]*\\blabel\\b[^\"]*\"[^>]*>(.*?)</span\\s*>"), kOptions);
    static const QRegularExpression number(QStringLiteral("^\\d+$"));
    int rowNumber = 0;
    for (auto rows = rowRe.globalMatch(body); rows.hasNext();) {
        const QString row = rows.next().captured(1);
        QStringList cells;
        for (auto it = cellRe.globalMatch(row); it.hasNext();) cells << it.next().captured(1);
        if (cells.isEmpty()) continue;
        ++rowNumber;
        if (cells.size() < headers.size()) {
            page.error = QCoreApplication::translate("infrastructure", "la fila %1 tiene %2 columnas y la cabecera %3")
                                 .arg(rowNumber).arg(cells.size()).arg(headers.size());
            return page;
        }
        auto text = [&cells](int index) { return index < 0 ? QString() : toPlainText(cells[index]).simplified(); };
        ExternalRequirement r;
        r.id = text(colId);
        if (!number.match(r.id).hasMatch()) {
            page.error = QCoreApplication::translate("infrastructure", "la fila %1 no empieza por un número de requerimiento («%2»)")
                                 .arg(rowNumber).arg(r.id);
            return page;
        }
        r.system = text(colSystem);
        const auto [code, name] = splitSystem(r.system);
        r.systemCode = code;
        r.systemName = name;
        r.summary = text(colSummary);
        r.requestedOn = parseDate(text(colRequested));
        r.assignedFrom = parseDate(text(colFrom));
        r.assignedUntil = parseDate(text(colUntil));
        r.requestingUnit = text(colUnit);
        r.requester = text(colRequester);
        r.user = text(colUser);
        r.priority = text(colPriority);
        // Cada estado es una etiqueta propia dentro de la celda; leída como texto se pegarían en uno solo.
        for (auto it = stateRe.globalMatch(cells[colState]); it.hasNext();) {
            const QString state = toPlainText(it.next().captured(1)).simplified();
            if (!state.isEmpty()) r.states << state;
        }
        if (r.states.isEmpty() && !text(colState).isEmpty()) r.states << text(colState);
        r.detailUrl = s.resolve(detailPath(r.id));
        page.requirements << r;
    }
    page.ok = true;
    return page;
}

DetailPage parseDetail(const QString& html, const QString& expectedId, const RequirementSourceSettings& s) {
    DetailPage page;
    const QString clean = withoutModals(withoutComments(html));
    static const QRegularExpression marker(QStringLiteral("GREQ:\\s*Datos\\s+del\\s+Requerimiento\\s*(\\d+)"), kOptions);
    const QRegularExpressionMatch found = marker.match(clean);
    if (!found.hasMatch()) {
        // Sin sesión, o con un número que no existe, GESREQ contesta 200 con la cáscara de la ficha: el
        // título y los estilos, sin datos y con un script que lleva a error2.jsp.
        static const QRegularExpression shell(QStringLiteral("<title>\\s*detalle\\s+Requerimiento"), kOptions);
        if (shell.match(clean).hasMatch()) {
            page.kind = DetailPage::Kind::Empty;
            return page;
        }
        page.error = QCoreApplication::translate("infrastructure", "la página no es la ficha de un requerimiento");
        return page;
    }
    RequirementDetail& d = page.detail;
    d.id = found.captured(1);
    if (!expectedId.trimmed().isEmpty() && d.id != expectedId.trimmed()) {
        page.error = QCoreApplication::translate("infrastructure", "llegó la ficha del requerimiento %1 en lugar de la del %2")
                             .arg(d.id, expectedId.trimmed());
        return page;
    }
    d.url = s.resolve(detailPath(d.id));

    const QString body = clean.mid(found.capturedEnd());
    static const QRegularExpression heading(QStringLiteral("<h5\\b[^>]*>(.*?)</h5\\s*>"), kOptions);
    QList<QRegularExpressionMatch> headings;
    for (auto it = heading.globalMatch(body); it.hasNext();) headings << it.next();

    // Datos generales: la tabla que va antes del primer bloque con título.
    static const QRegularExpression systemLabel(QStringLiteral("^Sistema\\s+(.+)$"), QRegularExpression::CaseInsensitiveOption);
    const QString general = headings.isEmpty() ? body : body.left(headings.first().capturedStart());
    bool hasState = false;
    for (RequirementField f : fieldsIn(general, QString(), s, d.attachments)) {
        // "Sistema SUMA TRANSITO" lleva el código en la etiqueta y el alcance del requerimiento en el valor.
        const QRegularExpressionMatch system = systemLabel.match(f.label);
        if (system.hasMatch()) {
            d.systemCode = system.captured(1).simplified();
            d.description = f.value;
            f = RequirementField{QStringLiteral("Sistema"), d.systemCode};
        }
        const QString key = headerKey(f.label);
        if (key == QLatin1String("tipo solicitud")) d.requestType = f.value;
        else if (key == QLatin1String("referencia")) d.reference = f.value;
        else if (key == QLatin1String("unidad solicitante")) d.requestingUnit = f.value;
        else if (key == QLatin1String("funcionario solicitante")) d.requester = f.value;
        else if (key == QLatin1String("prioridad")) d.priority = f.value;
        else if (key == QLatin1String("estado")) { d.state = f.value; hasState = true; }
        d.fields << f;
    }
    // Con sesión, un número que no existe trae el título de la ficha con ese número y nada más, ni una
    // tabla. Con tablas pero sin el estado, lo que ha cambiado es la página.
    static const QRegularExpression anyTable(QStringLiteral("<table\\b"), kOptions);
    if (d.fields.isEmpty() && headings.isEmpty() && !anyTable.match(body).hasMatch()) {
        page.kind = DetailPage::Kind::Missing;
        return page;
    }
    if (!hasState) {
        page.error = QCoreApplication::translate("infrastructure", "falta el campo «Estado» en los datos del requerimiento %1").arg(d.id);
        return page;
    }

    for (qsizetype i = 0; i < headings.size(); ++i) {
        const qsizetype from = headings[i].capturedEnd();
        const qsizetype to = i + 1 < headings.size() ? headings[i + 1].capturedStart() : body.size();
        RequirementSection section;
        section.title = toPlainText(headings[i].captured(1)).simplified();
        section.fields = fieldsIn(body.mid(from, to - from), section.title, s, d.attachments);
        // Un bloque que sólo trae la cabecera del sistema ("Control de Calidad") no aporta datos.
        if (!section.fields.isEmpty()) d.sections << section;
    }
    page.kind = DetailPage::Kind::Detail;
    return page;
}

SystemsPage parseSystems(const QString& html) {
    SystemsPage page;
    static const QRegularExpression select(QStringLiteral("<select\\b[^>]*\\bname\\s*=\\s*\"sistema\"[^>]*>(.*?)</select\\s*>"), kOptions);
    const QRegularExpressionMatch found = select.match(withoutComments(html));
    if (!found.hasMatch()) {
        page.error = QCoreApplication::translate("infrastructure", "no está la lista de sistemas");
        return page;
    }
    static const QRegularExpression option(QStringLiteral("<option\\b([^>]*)>(.*?)</option\\s*>"), kOptions);
    static const QRegularExpression value(QStringLiteral("\\bvalue\\s*=\\s*\"([^\"]*)\""), kOptions);
    for (auto it = option.globalMatch(found.captured(1)); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const QString code = decodeEntities(value.match(m.captured(1)).captured(1)).simplified();
        if (code.isEmpty()) continue;   // la opción en blanco, que en el formulario significa «todos»
        // "SUMA TRANSITO - TRANSITOS": el nombre es lo que sigue al código, y puede llevar sus propios guiones.
        const QString text = toPlainText(m.captured(2)).simplified();
        const QString prefix = code + QStringLiteral(" - ");
        page.systems << RequirementSystem{code, text.startsWith(prefix, Qt::CaseInsensitive) ? text.mid(prefix.size()).trimmed() : text};
    }
    page.ok = true;
    return page;
}

QString toPlainText(const QString& html) {
    static const QRegularExpression space(QStringLiteral("\\s+"));
    static const QRegularExpression lineBreak(QStringLiteral("<br\\b[^>]*>"), kOptions);
    static const QRegularExpression bullet(QStringLiteral("<li\\b[^>]*>"), kOptions);
    static const QRegularExpression block(QStringLiteral("</?(?:p|div|ul|ol|table|thead|tbody|tr|h[1-6])\\b[^>]*>|</li\\s*>"), kOptions);
    static const QRegularExpression cellEnd(QStringLiteral("</t[dh]\\s*>"), kOptions);
    static const QRegularExpression tag(QStringLiteral("<[^>]*>"));
    QString text = withoutComments(html);
    // Los saltos del código fuente no son saltos del texto: en HTML valen un espacio.
    text.replace(space, QStringLiteral(" "));
    text.replace(lineBreak, QStringLiteral("\n"));
    text.replace(bullet, QStringLiteral("\n•\n"));
    text.replace(block, QStringLiteral("\n"));
    text.replace(cellEnd, QStringLiteral(" "));
    text.remove(tag);
    text = decodeEntities(text);

    QStringList lines;
    bool bulletPending = false;   // la viñeta se queda sola en su línea cuando el <li> abre con un <p>
    for (const QString& raw : text.split(QLatin1Char('\n'))) {
        const QString line = raw.simplified();
        if (line.isEmpty()) continue;
        if (line == QStringLiteral("•")) { bulletPending = true; continue; }
        lines << (bulletPending ? QStringLiteral("• ") + line : line);
        bulletPending = false;
    }
    return lines.join(QLatin1Char('\n'));
}

QDate parseDate(const QString& text) {
    const QString t = text.simplified();
    QDate date = QDate::fromString(t, QStringLiteral("dd/MM/yyyy"));
    if (!date.isValid()) date = QDate::fromString(t, QStringLiteral("d/M/yyyy"));
    return date;
}

} // namespace qaflow::gesreq
