#include "runtime/modules/window.hpp"
#include "backends/surface/surface.hpp"
#include "runtime/common/runtime.hpp"
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <set>
#include <windows.h>

namespace xv::runtime::modules {
namespace {
std::mutex mutex;
std::set<HWND> title_windows;

// The title's window exists, so that its messages and its window procedure work, and stays
// hidden.
HWND remember(HWND window) {
    if (window) {
        const std::lock_guard lock(mutex);
        title_windows.insert(window);
    }
    return window;
}
// The console has no frame around a title's window: the size the title asks for is the size
// of what it draws to. A top-level window gets no caption or border here either.
DWORD console_style(DWORD style, HWND parent) {
    style &= ~WS_VISIBLE;
    if (!parent && !(style & WS_CHILD))
        style = (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP;
    return style;
}
// A window class belongs to a module, and the title's image is none the host knows: a class
// registered for it is not found again when the window is made. Classes and windows of the
// title are this program's instead.
HINSTANCE host_instance() { return GetModuleHandleW(nullptr); }
ATOM WINAPI register_class_ex_w(const WNDCLASSEXW *description) {
    if (!description)
        return 0;
    auto host = *description;
    host.hInstance = host_instance();
    return RegisterClassExW(&host);
}
ATOM WINAPI register_class_ex_a(const WNDCLASSEXA *description) {
    if (!description)
        return 0;
    auto host = *description;
    host.hInstance = host_instance();
    const ATOM atom = RegisterClassExA(&host);
    const DWORD error = atom ? ERROR_SUCCESS : GetLastError();
    std::fprintf(stderr, "[Runtime] RegisterClassExA: atom=%u error=%lu class=%s\\n",
                 static_cast<unsigned>(atom), static_cast<unsigned long>(error),
                 IS_INTRESOURCE(host.lpszClassName) ? "<atom>" :
                 (host.lpszClassName ? host.lpszClassName : "<null>"));
    std::fflush(stderr);
    return atom;
}
ATOM WINAPI register_class_w(const WNDCLASSW *description) {
    if (!description)
        return 0;
    auto host = *description;
    host.hInstance = host_instance();
    return RegisterClassW(&host);
}
ATOM WINAPI register_class_a(const WNDCLASSA *description) {
    if (!description)
        return 0;
    auto host = *description;
    host.hInstance = host_instance();
    return RegisterClassA(&host);
}
HWND WINAPI create_window_ex_w(DWORD extended, const wchar_t *class_name, const wchar_t *name,
                               DWORD style, int x, int y, int width, int height, HWND parent,
                               HMENU menu, HINSTANCE, void *parameter) {
    const auto window = CreateWindowExW(extended, class_name, name, console_style(style, parent), x,
                                        y, width, height, parent, menu, host_instance(), parameter);
    if (!window)
        std::fprintf(stderr, "[Runtime] CreateWindowExW: error %lu\n", GetLastError());
    return remember(window);
}
HWND WINAPI create_window_ex_a(DWORD extended, const char *class_name, const char *name,
                               DWORD style, int x, int y, int width, int height, HWND parent,
                               HMENU menu, HINSTANCE, void *parameter) {
    const auto window = CreateWindowExA(extended, class_name, name, console_style(style, parent), x,
                                        y, width, height, parent, menu, host_instance(), parameter);
    if (!window)
        std::fprintf(stderr, "[Runtime] CreateWindowExA: error %lu\n", GetLastError());
    return remember(window);
}
BOOL WINAPI show_window(HWND window, int command) {
    {
        const std::lock_guard lock(mutex);
        if (title_windows.contains(window))
            return TRUE; // a title takes what the console answers for success
    }
    return ShowWindow(window, command);
}
} // namespace

surface::TitleWindow &title_window() {
    static surface::TitleWindow window = [] {
        std::error_code error;
        auto name = std::filesystem::current_path(error).filename().wstring();
        return surface::TitleWindow(name.empty() ? L"XValkyrie" : name, 1280, 720);
    }();
    return window;
}

void install_window(Registry &registry) {
    registry.add_api("CreateWindowExW", reinterpret_cast<void *>(&create_window_ex_w));
    registry.add_api("CreateWindowExA", reinterpret_cast<void *>(&create_window_ex_a));
    registry.add_api("ShowWindow", reinterpret_cast<void *>(&show_window));
    registry.add_api("RegisterClassExW", reinterpret_cast<void *>(&register_class_ex_w));
    registry.add_api("RegisterClassExA", reinterpret_cast<void *>(&register_class_ex_a));
    registry.add_api("RegisterClassW", reinterpret_cast<void *>(&register_class_w));
    registry.add_api("RegisterClassA", reinterpret_cast<void *>(&register_class_a));
}
} // namespace xv::runtime::modules
