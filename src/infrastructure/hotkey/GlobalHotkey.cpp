#include "GlobalHotkey.h"

#include "infrastructure/hotkey/PortalShortcuts.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeySequence>

#if defined(Q_OS_WIN)
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <Carbon/Carbon.h>
#elif defined(QAFLOW_HOTKEY_X11)
#include <QtGui/qguiapplication_platform.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <xcb/xcb.h>
#undef Bool
#undef None
#undef Status
#undef KeyPress
#undef KeyRelease
#undef FocusIn
#undef FocusOut
#undef FontChange
#undef Expose
#undef CursorShape
#undef Unsorted
#endif

namespace qaflow {

namespace {


/// Primera combinación de la secuencia ("Ctrl+Shift+S" → modificadores + tecla).
bool parseSequence(const QString& sequence, Qt::KeyboardModifiers* mods, Qt::Key* key) {
    const QKeySequence seq(sequence.trimmed(), QKeySequence::PortableText);
    if (seq.isEmpty()) return false;
    const QKeyCombination kc = seq[0];
    *mods = kc.keyboardModifiers();
    *key = kc.key();
    return *key != Qt::Key_unknown && *key != 0;
}

#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
bool isWaylandSession() {
    return QGuiApplication::platformName().contains(QStringLiteral("wayland"), Qt::CaseInsensitive)
        || qEnvironmentVariable("XDG_SESSION_TYPE").compare(QStringLiteral("wayland"), Qt::CaseInsensitive) == 0;
}
#endif

#if defined(Q_OS_WIN)
bool toVirtualKey(Qt::Key key, unsigned* vk) {
    if (key >= Qt::Key_A && key <= Qt::Key_Z) { *vk = 'A' + (key - Qt::Key_A); return true; }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) { *vk = '0' + (key - Qt::Key_0); return true; }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) { *vk = VK_F1 + (key - Qt::Key_F1); return true; }
    switch (key) {
        case Qt::Key_Space: *vk = VK_SPACE; return true;
        case Qt::Key_Print: *vk = VK_SNAPSHOT; return true;
        case Qt::Key_Insert: *vk = VK_INSERT; return true;
        case Qt::Key_Delete: *vk = VK_DELETE; return true;
        case Qt::Key_Home: *vk = VK_HOME; return true;
        case Qt::Key_End: *vk = VK_END; return true;
        case Qt::Key_PageUp: *vk = VK_PRIOR; return true;
        case Qt::Key_PageDown: *vk = VK_NEXT; return true;
        case Qt::Key_Left: *vk = VK_LEFT; return true;
        case Qt::Key_Right: *vk = VK_RIGHT; return true;
        case Qt::Key_Up: *vk = VK_UP; return true;
        case Qt::Key_Down: *vk = VK_DOWN; return true;
        case Qt::Key_Escape: *vk = VK_ESCAPE; return true;
        case Qt::Key_Tab: *vk = VK_TAB; return true;
        case Qt::Key_Return: case Qt::Key_Enter: *vk = VK_RETURN; return true;
        case Qt::Key_Pause: *vk = VK_PAUSE; return true;
        case Qt::Key_ScrollLock: *vk = VK_SCROLL; return true;
        default: return false;
    }
}
#elif defined(Q_OS_MACOS)
bool toMacKeyCode(Qt::Key key, unsigned* code) {
    static const struct { Qt::Key key; unsigned code; } table[] = {
        {Qt::Key_A, 0x00}, {Qt::Key_S, 0x01}, {Qt::Key_D, 0x02}, {Qt::Key_F, 0x03}, {Qt::Key_H, 0x04}, {Qt::Key_G, 0x05},
        {Qt::Key_Z, 0x06}, {Qt::Key_X, 0x07}, {Qt::Key_C, 0x08}, {Qt::Key_V, 0x09}, {Qt::Key_B, 0x0B}, {Qt::Key_Q, 0x0C},
        {Qt::Key_W, 0x0D}, {Qt::Key_E, 0x0E}, {Qt::Key_R, 0x0F}, {Qt::Key_Y, 0x10}, {Qt::Key_T, 0x11}, {Qt::Key_1, 0x12},
        {Qt::Key_2, 0x13}, {Qt::Key_3, 0x14}, {Qt::Key_4, 0x15}, {Qt::Key_6, 0x16}, {Qt::Key_5, 0x17}, {Qt::Key_9, 0x19},
        {Qt::Key_7, 0x1A}, {Qt::Key_8, 0x1C}, {Qt::Key_0, 0x1D}, {Qt::Key_O, 0x1F}, {Qt::Key_U, 0x20}, {Qt::Key_I, 0x22},
        {Qt::Key_P, 0x23}, {Qt::Key_L, 0x25}, {Qt::Key_J, 0x26}, {Qt::Key_K, 0x28}, {Qt::Key_N, 0x2D}, {Qt::Key_M, 0x2E},
        {Qt::Key_F1, 0x7A}, {Qt::Key_F2, 0x78}, {Qt::Key_F3, 0x63}, {Qt::Key_F4, 0x76}, {Qt::Key_F5, 0x60}, {Qt::Key_F6, 0x61},
        {Qt::Key_F7, 0x62}, {Qt::Key_F8, 0x64}, {Qt::Key_F9, 0x65}, {Qt::Key_F10, 0x6D}, {Qt::Key_F11, 0x67}, {Qt::Key_F12, 0x6F},
        {Qt::Key_Space, 0x31}, {Qt::Key_Return, 0x24}, {Qt::Key_Escape, 0x35}, {Qt::Key_Tab, 0x30}, {Qt::Key_Delete, 0x75},
        {Qt::Key_Home, 0x73}, {Qt::Key_End, 0x77}, {Qt::Key_PageUp, 0x74}, {Qt::Key_PageDown, 0x79}, {Qt::Key_Left, 0x7B},
        {Qt::Key_Right, 0x7C}, {Qt::Key_Down, 0x7D}, {Qt::Key_Up, 0x7E},
    };
    for (const auto& e : table) if (e.key == key) { *code = e.code; return true; }
    return false;
}

OSStatus hotkeyHandler(EventHandlerCallRef, EventRef event, void* userData) {
    EventHotKeyID hk{};
    GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof(hk), nullptr, &hk);
    static_cast<GlobalHotkey*>(userData)->activate(static_cast<int>(hk.id));
    return noErr;
}
#elif defined(QAFLOW_HOTKEY_X11)
KeySym toKeysym(Qt::Key key) {
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return XK_a + (key - Qt::Key_A);
    if (key >= Qt::Key_0 && key <= Qt::Key_9) return XK_0 + (key - Qt::Key_0);
    if (key >= Qt::Key_F1 && key <= Qt::Key_F35) return XK_F1 + (key - Qt::Key_F1);
    switch (key) {
        case Qt::Key_Space: return XK_space;
        case Qt::Key_Print: return XK_Print;
        case Qt::Key_Insert: return XK_Insert;
        case Qt::Key_Delete: return XK_Delete;
        case Qt::Key_Home: return XK_Home;
        case Qt::Key_End: return XK_End;
        case Qt::Key_PageUp: return XK_Page_Up;
        case Qt::Key_PageDown: return XK_Page_Down;
        case Qt::Key_Left: return XK_Left;
        case Qt::Key_Right: return XK_Right;
        case Qt::Key_Up: return XK_Up;
        case Qt::Key_Down: return XK_Down;
        case Qt::Key_Escape: return XK_Escape;
        case Qt::Key_Tab: return XK_Tab;
        case Qt::Key_Return: return XK_Return;
        case Qt::Key_Enter: return XK_KP_Enter;
        case Qt::Key_Pause: return XK_Pause;
        case Qt::Key_ScrollLock: return XK_Scroll_Lock;
        default: return NoSymbol;
    }
}

Display* x11Display() {
    auto* x11 = qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QX11Application>() : nullptr;
    return x11 ? x11->display() : nullptr;
}

// XGrabKey falla de forma asíncrona (BadAccess si otra app ya tiene la tecla): se captura con un
// manejador temporal y XSync.
int g_grabError = 0;
int grabErrorHandler(Display*, XErrorEvent* e) { g_grabError = e->error_code; return 0; }
constexpr unsigned kIgnoredMasks[] = {0, Mod2Mask, LockMask, Mod2Mask | LockMask};   // Bloq Num, Bloq Mayús
#endif

} // namespace

// ---- Común -----------------------------------------------------------------------------------

GlobalHotkey::GlobalHotkey(QObject* parent) : QObject(parent) {
#if defined(Q_OS_WIN)
    m_status = QCoreApplication::translate("infrastructure", "Atajo global del sistema (Windows)");
#elif defined(Q_OS_MACOS)
    m_status = QCoreApplication::translate("infrastructure", "Atajo global del sistema (macOS)");
#elif defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
    if (isWaylandSession()) {
#ifdef QAFLOW_HAS_DBUS
        m_portal = new PortalShortcuts(this);
        m_status = m_portal->isAvailable() ? QCoreApplication::translate("infrastructure", "Atajo global mediante el portal de Wayland")
                                           : QCoreApplication::translate("infrastructure", "Wayland sin portal GlobalShortcuts: el atajo sólo funciona con QAflow en primer plano");
#else
        m_status = QCoreApplication::translate("infrastructure", "Wayland: el atajo sólo funciona con QAflow en primer plano (compilado sin QtDBus)");
#endif
    } else {
#ifdef QAFLOW_HOTKEY_X11
        m_status = QCoreApplication::translate("infrastructure", "Atajo global del sistema (X11)");
#else
        m_status = QCoreApplication::translate("infrastructure", "Compilado sin soporte X11: el atajo sólo funciona con QAflow en primer plano");
#endif
    }
#else
    m_status = QCoreApplication::translate("infrastructure", "Atajo global no disponible en esta plataforma");
#endif
    m_platformStatus = m_status;
}

GlobalHotkey::~GlobalHotkey() {
    for (auto& b : m_bindings) unregisterNative(b);
#if defined(Q_OS_MACOS)
    if (m_platform) RemoveEventHandler(static_cast<EventHandlerRef>(m_platform));
#endif
    if (m_filterInstalled && qGuiApp) qGuiApp->removeNativeEventFilter(this);
}

void GlobalHotkey::installFilter() {
    if (m_filterInstalled || !qGuiApp) return;
    qGuiApp->installNativeEventFilter(this);
    m_filterInstalled = true;
}

bool GlobalHotkey::bind(const QString& id, const QString& sequence, std::function<void()> onActivated) {
    unbind(id);
    Binding b;
    b.id = id;
    b.sequence = sequence;
    b.callback = std::move(onActivated);
    b.nativeId = m_nextId++;
    const bool ok = registerNative(b);
    m_bindings.append(b);
    return ok;
}

void GlobalHotkey::unbind(const QString& id) {
    for (int i = m_bindings.size() - 1; i >= 0; --i) {
        if (m_bindings[i].id != id) continue;
        unregisterNative(m_bindings[i]);
        m_bindings.removeAt(i);
    }
}

bool GlobalHotkey::isBound(const QString& id) const {
    for (const auto& b : m_bindings) if (b.id == id) return b.bound;
    return false;
}

void GlobalHotkey::activate(int nativeId) {
    for (const auto& b : m_bindings)
        if (b.nativeId == nativeId && b.bound && b.callback) { b.callback(); return; }
}

// ---- Plataformas -----------------------------------------------------------------------------

bool GlobalHotkey::registerNative(Binding& b) {
    Qt::KeyboardModifiers mods;
    Qt::Key key;
    if (!parseSequence(b.sequence, &mods, &key)) {
        m_status = QCoreApplication::translate("infrastructure", "Atajo «%1» no válido").arg(b.sequence);
        return false;
    }
#if defined(Q_OS_WIN)
    unsigned vk = 0;
    if (!toVirtualKey(key, &vk)) { m_status = QCoreApplication::translate("infrastructure", "La tecla de «%1» no admite atajo global").arg(b.sequence); return false; }
    b.mods = (mods & Qt::ControlModifier ? MOD_CONTROL : 0) | (mods & Qt::ShiftModifier ? MOD_SHIFT : 0)
           | (mods & Qt::AltModifier ? MOD_ALT : 0) | (mods & Qt::MetaModifier ? MOD_WIN : 0) | MOD_NOREPEAT;
    b.key = vk;
    installFilter();
    b.bound = RegisterHotKey(nullptr, b.nativeId, b.mods, b.key) != 0;
    m_status = b.bound ? m_platformStatus
                       : QCoreApplication::translate("infrastructure", "Windows rechazó «%1» (otra aplicación ya lo usa)").arg(b.sequence);
    return b.bound;
#elif defined(Q_OS_MACOS)
    unsigned code = 0;
    if (!toMacKeyCode(key, &code)) { m_status = QCoreApplication::translate("infrastructure", "La tecla de «%1» no admite atajo global").arg(b.sequence); return false; }
    if (!m_platform) {
        EventTypeSpec spec{kEventClassKeyboard, kEventHotKeyPressed};
        EventHandlerRef ref = nullptr;
        InstallApplicationEventHandler(NewEventHandlerUPP(hotkeyHandler), 1, &spec, this, &ref);
        m_platform = ref;
    }
    // En Qt/macOS, Ctrl es ⌘ y Meta es la tecla Control.
    b.mods = (mods & Qt::ControlModifier ? cmdKey : 0) | (mods & Qt::ShiftModifier ? shiftKey : 0)
           | (mods & Qt::AltModifier ? optionKey : 0) | (mods & Qt::MetaModifier ? controlKey : 0);
    b.key = code;
    EventHotKeyID hk{'QAfl', static_cast<UInt32>(b.nativeId)};
    EventHotKeyRef ref = nullptr;
    b.bound = RegisterEventHotKey(b.key, b.mods, hk, GetApplicationEventTarget(), 0, &ref) == noErr;
    b.handle = ref;
    m_status = b.bound ? m_platformStatus
                       : QCoreApplication::translate("infrastructure", "macOS rechazó «%1»").arg(b.sequence);
    return b.bound;
#elif defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
    if (m_portal) {
        const int nativeId = b.nativeId;
        b.bound = m_portal->bind(b.id, b.sequence, [this, nativeId]() { activate(nativeId); });
        m_status = b.bound ? QCoreApplication::translate("infrastructure", "Atajos pedidos al portal de Wayland (el escritorio puede pedir confirmación)")
                           : QCoreApplication::translate("infrastructure", "Wayland sin portal GlobalShortcuts: el atajo sólo funciona con QAflow en primer plano");
        return b.bound;
    }
#ifdef QAFLOW_HOTKEY_X11
    Display* dpy = x11Display();
    if (!dpy) { m_status = QCoreApplication::translate("infrastructure", "Sin conexión X11: el atajo sólo funciona con QAflow en primer plano"); return false; }
    const KeySym sym = toKeysym(key);
    if (sym == NoSymbol) { m_status = QCoreApplication::translate("infrastructure", "La tecla de «%1» no admite atajo global").arg(b.sequence); return false; }
    const KeyCode code = XKeysymToKeycode(dpy, sym);
    if (!code) { m_status = QCoreApplication::translate("infrastructure", "La tecla de «%1» no existe en este teclado").arg(b.sequence); return false; }
    b.mods = (mods & Qt::ControlModifier ? ControlMask : 0) | (mods & Qt::ShiftModifier ? ShiftMask : 0)
           | (mods & Qt::AltModifier ? Mod1Mask : 0) | (mods & Qt::MetaModifier ? Mod4Mask : 0);
    b.key = code;
    installFilter();
    g_grabError = 0;
    auto* previous = XSetErrorHandler(grabErrorHandler);
    const Window root = DefaultRootWindow(dpy);
    for (unsigned extra : kIgnoredMasks) XGrabKey(dpy, code, b.mods | extra, root, False, GrabModeAsync, GrabModeAsync);
    XSync(dpy, False);
    XSetErrorHandler(previous);
    b.bound = g_grabError == 0;
    if (!b.bound) for (unsigned extra : kIgnoredMasks) XUngrabKey(dpy, code, b.mods | extra, root);
    m_status = b.bound ? m_platformStatus
                       : QCoreApplication::translate("infrastructure", "X11 rechazó «%1» (otra aplicación ya lo usa)").arg(b.sequence);
    return b.bound;
#else
    return false;
#endif
#else
    Q_UNUSED(mods); Q_UNUSED(key);
    return false;
#endif
}

void GlobalHotkey::unregisterNative(Binding& b) {
    if (!b.bound) return;
    b.bound = false;
#if defined(Q_OS_WIN)
    UnregisterHotKey(nullptr, b.nativeId);
#elif defined(Q_OS_MACOS)
    if (b.handle) UnregisterEventHotKey(static_cast<EventHotKeyRef>(b.handle));
    b.handle = nullptr;
#elif defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
    if (m_portal) { m_portal->unbind(b.id); return; }
#ifdef QAFLOW_HOTKEY_X11
    if (Display* dpy = x11Display()) {
        for (unsigned extra : kIgnoredMasks) XUngrabKey(dpy, static_cast<KeyCode>(b.key), b.mods | extra, DefaultRootWindow(dpy));
        XFlush(dpy);
    }
#endif
#endif
}

bool GlobalHotkey::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) {
    Q_UNUSED(result);
#if defined(Q_OS_WIN)
    if (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG") return false;
    auto* msg = static_cast<MSG*>(message);
    if (msg->message != WM_HOTKEY) return false;
    activate(static_cast<int>(msg->wParam));
    return true;
#elif defined(QAFLOW_HOTKEY_X11)
    if (eventType != "xcb_generic_event_t") return false;
    auto* ev = static_cast<xcb_generic_event_t*>(message);
    if ((ev->response_type & ~0x80) != XCB_KEY_PRESS) return false;
    auto* kp = reinterpret_cast<xcb_key_press_event_t*>(ev);
    const unsigned state = kp->state & (ShiftMask | ControlMask | Mod1Mask | Mod4Mask);
    for (const auto& b : m_bindings) {
        if (!b.bound || b.key != kp->detail || b.mods != state) continue;
        activate(b.nativeId);
        return true;
    }
    return false;
#else
    Q_UNUSED(eventType); Q_UNUSED(message);
    return false;
#endif
}

} // namespace qaflow
