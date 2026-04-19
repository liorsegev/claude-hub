#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "App.hpp"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	ch::App app;
	return app.run();
}
