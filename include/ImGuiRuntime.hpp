#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace ch {

// RAII wrapper over ImGui's Win32 + DX11 backend lifecycle.
// MUST be destroyed BEFORE the D3D11Context it was built against.
class ImGuiRuntime {
public:
	ImGuiRuntime(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context);
	~ImGuiRuntime();

	ImGuiRuntime(const ImGuiRuntime&) = delete;
	ImGuiRuntime& operator=(const ImGuiRuntime&) = delete;

	void begin_frame();
	void render_draw_data();
};

}
