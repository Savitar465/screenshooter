#include "SeedData.h"

namespace qaflow::seed {

namespace {
TestCase make(const char* id, const char* title, const char* suite, Priority prio, CaseStatus status,
              RunOutcome outcome, int daysAgo, const char* pre, std::initializer_list<std::pair<const char*, const char*>> steps) {
    TestCase c;
    c.id = QString::fromUtf8(id);
    c.title = QString::fromUtf8(title);
    c.suite = QString::fromUtf8(suite);
    c.priority = prio;
    c.status = status;
    c.preconditions = QString::fromUtf8(pre);
    if (outcome != RunOutcome::None) c.lastRun = LastRun{outcome, QDateTime::currentDateTime().addDays(-daysAgo)};
    for (const auto& [a, e] : steps) c.steps.append(TestStep{QString::fromUtf8(a), QString::fromUtf8(e)});
    return c;
}
} // namespace

QStringList defaultSuites() {
    return {QStringLiteral("Autenticación"), QStringLiteral("Checkout"), QStringLiteral("Perfil"), QStringLiteral("Notificaciones")};
}

QStringList defaultPlanIds() {
    return {QStringLiteral("TC-101"), QStringLiteral("TC-102"), QStringLiteral("TC-104"), QStringLiteral("TC-105")};
}

QString defaultSelection(const QList<TestCase>& cases) {
    for (const auto& c : cases) if (c.id == QStringLiteral("TC-104")) return c.id;
    return cases.isEmpty() ? QString() : cases.first().id;
}

QList<TestCase> sampleCases() {
    QList<TestCase> cases = {
        make("TC-101", "Inicio de sesión con credenciales válidas", "Autenticación", Priority::Alta, CaseStatus::Listo, RunOutcome::Passed, 2,
             "Usuario registrado y verificado. Navegador sin sesión activa.", {
                 {"Abrir la pantalla de inicio de sesión", "Se muestran los campos correo y contraseña"},
                 {"Introducir correo y contraseña válidos y pulsar Entrar", "Redirige al panel principal en menos de 2 s"},
                 {"Recargar la página", "La sesión se mantiene"}}),
        make("TC-102", "Bloqueo tras 5 intentos fallidos", "Autenticación", Priority::Alta, CaseStatus::Listo, RunOutcome::Failed, 2,
             "Usuario registrado.", {
                 {"Introducir contraseña incorrecta 5 veces", "Mensaje de cuenta bloqueada temporalmente"},
                 {"Intentar con la contraseña correcta", "Sigue bloqueado durante 15 min"}}),
        make("TC-103", "Recuperar contraseña por correo", "Autenticación", Priority::Media, CaseStatus::Borrador, RunOutcome::None, 0, "", {
                 {"Pulsar \"¿Olvidaste tu contraseña?\"", "Se muestra formulario de correo"}}),
        make("TC-104", "Pago con tarjeta y cupón de descuento aplicado", "Checkout", Priority::Alta, CaseStatus::Listo, RunOutcome::Passed, 1,
             "Carrito con 2 productos. Cupón QA10 activo (10 %). Tarjeta de prueba 4242.", {
                 {"Ir al carrito y pulsar \"Finalizar compra\"", "Se muestra el resumen con 2 líneas y subtotal correcto"},
                 {"Introducir el cupón QA10 y aplicar", "El total baja un 10 % y aparece la línea \"Descuento\""},
                 {"Completar datos de tarjeta 4242 4242 4242 4242 y pagar", "Pantalla de confirmación con número de pedido"},
                 {"Abrir el correo de confirmación", "Importe coincide con el total con descuento"}}),
        make("TC-105", "Envío gratuito por encima de 50 €", "Checkout", Priority::Media, CaseStatus::Listo, RunOutcome::Passed, 5,
             "Carrito con importe > 50 €.", {
                 {"Revisar el resumen del pedido", "Línea de envío muestra 0,00 €"},
                 {"Reducir el carrito por debajo de 50 €", "Se cobra envío estándar 4,95 €"}}),
        make("TC-106", "Cambiar avatar de perfil", "Perfil", Priority::Baja, CaseStatus::Listo, RunOutcome::None, 0, "Sesión iniciada.", {
                 {"Subir imagen PNG de 2 MB", "Vista previa y guardado correcto"},
                 {"Subir imagen de 12 MB", "Error claro de tamaño máximo"}}),
        make("TC-107", "Preferencias de notificaciones por correo", "Notificaciones", Priority::Baja, CaseStatus::Borrador, RunOutcome::None, 0, "", {
                 {"Desactivar \"Novedades\" y guardar", "Preferencia persistida tras recargar"}}),
    };
    // Metadatos de ejemplo: etiquetas, componente e historia enlazada.
    for (auto& c : cases) {
        if (c.id == QStringLiteral("TC-101")) { c.tags = {QStringLiteral("smoke"), QStringLiteral("regresión")}; c.component = QStringLiteral("Login"); c.jiraKey = QStringLiteral("SHOP-3"); }
        if (c.id == QStringLiteral("TC-104")) { c.tags = {QStringLiteral("regresión"), QStringLiteral("pagos")}; c.component = QStringLiteral("Carrito"); c.jiraKey = QStringLiteral("SHOP-12"); }
        if (c.id == QStringLiteral("TC-105")) { c.tags = {QStringLiteral("regresión")}; c.component = QStringLiteral("Carrito"); }
    }
    return cases;
}

} // namespace qaflow::seed
