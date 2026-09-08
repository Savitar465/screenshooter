#include "Theme.h"

#include <QFile>
#include <QGuiApplication>
#include <QPalette>
#include <QRegularExpression>
#include <QStyleHints>

namespace qaflow::theme {

namespace {

Palette g_current;

Palette makeDark() {
    Palette p;
    p.bg = QStringLiteral("#0e1116"); p.panel = QStringLiteral("#161b22"); p.elevated = QStringLiteral("#1c232d");
    p.field = QStringLiteral("#0e1116"); p.border = QStringLiteral("#2a3441");
    p.text = QStringLiteral("#e6edf3"); p.textSoft = QStringLiteral("#d0d8e0"); p.muted = QStringLiteral("#9aa7b4");
    p.disabled = QStringLiteral("#5c6876"); p.onAccent = QStringLiteral("#0e1116");
    p.blue = QStringLiteral("#6ea8fe"); p.blueHover = QStringLiteral("#9db8f0");
    p.green = QStringLiteral("#10b981"); p.greenHover = QStringLiteral("#34d399");
    p.red = QStringLiteral("#ef4444"); p.redHover = QStringLiteral("#f87171"); p.redSoft = QStringLiteral("#ff8f8f");
    p.amber = QStringLiteral("#f59e0b"); p.amberSoft = QStringLiteral("#fbbf24");
    p.cyan = theme::Cyan; p.violet = QStringLiteral("#8b5cf6");
    p.gradientTop = QStringLiteral("#1a2130"); p.scrollHover = QStringLiteral("#3a4656");
    p.panelTranslucent = QStringLiteral("rgba(22,27,34,153)");
    return p;
}

Palette makeLight() {
    Palette p;
    p.bg = QStringLiteral("#f3f5f8"); p.panel = QStringLiteral("#ffffff"); p.elevated = QStringLiteral("#eaeef3");
    p.field = QStringLiteral("#ffffff"); p.border = QStringLiteral("#d3dae3");
    p.text = QStringLiteral("#17202b"); p.textSoft = QStringLiteral("#2f3b49"); p.muted = QStringLiteral("#5f6b7a");
    p.disabled = QStringLiteral("#a3adb9"); p.onAccent = QStringLiteral("#ffffff");
    p.blue = QStringLiteral("#2563eb"); p.blueHover = QStringLiteral("#1d4ed8");
    p.green = QStringLiteral("#059669"); p.greenHover = QStringLiteral("#047857");
    p.red = QStringLiteral("#dc2626"); p.redHover = QStringLiteral("#b91c1c"); p.redSoft = QStringLiteral("#b91c1c");
    p.amber = QStringLiteral("#d97706"); p.amberSoft = QStringLiteral("#b45309");
    p.cyan = QStringLiteral("#0891b2"); p.violet = QStringLiteral("#7c3aed");
    p.gradientTop = QStringLiteral("#e4ebf5"); p.scrollHover = QStringLiteral("#b8c2cf");
    p.panelTranslucent = QStringLiteral("rgba(255,255,255,200)");
    return p;
}

/// Tokens del QSS → valor en la paleta.
QString tokenValue(const Palette& p, const QString& name) {
    static const QHash<QString, QString Palette::*> map{
        {QStringLiteral("bg"), &Palette::bg}, {QStringLiteral("panel"), &Palette::panel}, {QStringLiteral("elevated"), &Palette::elevated},
        {QStringLiteral("field"), &Palette::field}, {QStringLiteral("border"), &Palette::border}, {QStringLiteral("text"), &Palette::text},
        {QStringLiteral("text-soft"), &Palette::textSoft}, {QStringLiteral("muted"), &Palette::muted}, {QStringLiteral("disabled"), &Palette::disabled},
        {QStringLiteral("on-accent"), &Palette::onAccent}, {QStringLiteral("blue"), &Palette::blue}, {QStringLiteral("blue-hover"), &Palette::blueHover},
        {QStringLiteral("green"), &Palette::green}, {QStringLiteral("green-hover"), &Palette::greenHover}, {QStringLiteral("red"), &Palette::red},
        {QStringLiteral("red-hover"), &Palette::redHover}, {QStringLiteral("red-soft"), &Palette::redSoft}, {QStringLiteral("amber"), &Palette::amber},
        {QStringLiteral("amber-soft"), &Palette::amberSoft}, {QStringLiteral("cyan"), &Palette::cyan}, {QStringLiteral("violet"), &Palette::violet},
        {QStringLiteral("gradient-top"), &Palette::gradientTop}, {QStringLiteral("scroll-hover"), &Palette::scrollHover},
        {QStringLiteral("panel-translucent"), &Palette::panelTranslucent},
    };
    const auto it = map.constFind(name);
    return it == map.cend() ? QStringLiteral("magenta") : p.*(it.value());
}

} // namespace

const Palette& darkPalette() { static const Palette p = makeDark(); return p; }
const Palette& lightPalette() { static const Palette p = makeLight(); return p; }

bool systemPrefersDark() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Light) return false;
    if (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark) return true;
#endif
    // Sin información del sistema: se deduce de la paleta que trae la plataforma.
    return QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
}

const Palette& paletteFor(AppTheme t) {
    switch (t) {
        case AppTheme::Dark: return darkPalette();
        case AppTheme::Light: return lightPalette();
        case AppTheme::System: return systemPrefersDark() ? darkPalette() : lightPalette();
    }
    return darkPalette();
}

void apply(const Palette& p) {
    g_current = p;
    Bg = p.bg; Panel = p.panel; Elevated = p.elevated; Field = p.field; Border = p.border;
    Text = p.text; TextSoft = p.textSoft; Muted = p.muted; Disabled = p.disabled; OnAccent = p.onAccent;
    Blue = p.blue; Green = p.green; Red = p.red; RedSoft = p.redSoft; Amber = p.amber; AmberSoft = p.amberSoft;
    Cyan = p.cyan; Violet = p.violet;
}

const Palette& current() {
    if (g_current.bg.isEmpty()) apply(darkPalette());
    return g_current;
}

bool isDark() { return current().bg == darkPalette().bg; }

QString tint(const QString& color, int alpha) {
    const QColor c(color);
    return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(alpha);
}

QString stylesheet() {
    const Palette& p = current();
    QFile f(QStringLiteral(":/styles/app.qss"));
    if (!f.open(QFile::ReadOnly)) return {};
    QString qss = QString::fromUtf8(f.readAll());
    // @tint(nombre,alpha) → rgba(); después @nombre → color.
    static const QRegularExpression tintRe(QStringLiteral("@tint\\(([a-z-]+),\\s*(\\d+)\\)"));
    QString out;
    qsizetype last = 0;
    for (auto it = tintRe.globalMatch(qss); it.hasNext();) {
        const auto m = it.next();
        out += qss.mid(last, m.capturedStart() - last);
        out += tint(tokenValue(p, m.captured(1)), m.captured(2).toInt());
        last = m.capturedEnd();
    }
    out += qss.mid(last);
    static const QRegularExpression tokenRe(QStringLiteral("@([a-z][a-z-]*)"));
    QString result;
    last = 0;
    for (auto it = tokenRe.globalMatch(out); it.hasNext();) {
        const auto m = it.next();
        result += out.mid(last, m.capturedStart() - last);
        result += tokenValue(p, m.captured(1));
        last = m.capturedEnd();
    }
    result += out.mid(last);
    return result;
}

Pill priorityPill(const QString& priority) {
    if (priority == QStringLiteral("Alta")) return {tint(Red, 38), RedSoft};
    if (priority == QStringLiteral("Media")) return {tint(Amber, 38), AmberSoft};
    return {tint(Muted, 38), Muted};
}

} // namespace qaflow::theme
