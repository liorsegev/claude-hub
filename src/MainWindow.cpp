#include "MainWindow.hpp"

#include <imgui.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace ch {

namespace {
constexpr const wchar_t* WINDOW_CLASS = L"ClaudeHub";
}

MainWindow::MainWindow(const wchar_t* title, int width, int height, IWindowHandler& handler)
	: handler_(handler) {
	WNDCLASSEXW wc{
		sizeof(wc), 0, wnd_proc_thunk, 0, 0,
		GetModuleHandle(nullptr), nullptr,
		LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr,
		WINDOW_CLASS, nullptr};
	RegisterClassExW(&wc);

	hwnd_ = CreateWindowExW(
		0, WINDOW_CLASS, title,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, width, height,
		nullptr, nullptr, wc.hInstance, this);
}

MainWindow::~MainWindow() {
	if (hwnd_) DestroyWindow(hwnd_);
	UnregisterClassW(WINDOW_CLASS, GetModuleHandle(nullptr));
}

void MainWindow::show() {
	ShowWindow(hwnd_, SW_SHOWDEFAULT);
}

RECT MainWindow::client_rect() const {
	RECT r{};
	GetClientRect(hwnd_, &r);
	return r;
}

LRESULT CALLBACK MainWindow::wnd_proc_thunk(HWND h, UINT m, WPARAM w, LPARAM l) {
	MainWindow* self = nullptr;
	if (m == WM_NCCREATE) {
		auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
		self = static_cast<MainWindow*>(cs->lpCreateParams);
		SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
		self->hwnd_ = h;
	} else {
		self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
	}
	if (!self) return DefWindowProcW(h, m, w, l);
	return self->dispatch(m, w, l);
}

LRESULT MainWindow::dispatch(UINT m, WPARAM w, LPARAM l) {
	if (ImGui_ImplWin32_WndProcHandler(hwnd_, m, w, l)) return 1;
	if (m == WM_SIZE && w != SIZE_MINIMIZED) {
		handler_.on_resize(LOWORD(l), HIWORD(l));
		return 0;
	}
	if (m == WM_DESTROY) {
		handler_.on_quit();
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hwnd_, m, w, l);
}

}
