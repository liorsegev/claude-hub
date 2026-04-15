#include "app.hpp"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	claude_hub::App app;
	return app.run();
}
