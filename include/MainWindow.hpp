#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace ch {

// Callback interface for App to receive window events without MainWindow
// needing to know about App's concrete type.
class IWindowHandler {
public:
	virtual ~IWindowHandler() = default;
	virtual void on_resize(unsigned int w, unsigned int h) = 0;
	virtual void on_quit() = 0;
};

class MainWindow {
public:
	MainWindow(const wchar_t* title, int width, int height, IWindowHandler& handler);
	~MainWindow();

	MainWindow(const MainWindow&) = delete;
	MainWindow& operator=(const MainWindow&) = delete;

	HWND hwnd() const { return hwnd_; }
	void show();
	RECT client_rect() const;

private:
	static LRESULT CALLBACK wnd_proc_thunk(HWND, UINT, WPARAM, LPARAM);
	LRESULT dispatch(UINT m, WPARAM w, LPARAM l);

	HWND hwnd_ = nullptr;
	IWindowHandler& handler_;
};

}
