#pragma once

#include "agent_manager.hpp"
#include "renderer.hpp"
#include "input_handler.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11RenderTargetView;

namespace claude_hub {

/// Top-level application: owns window, D3D11, ImGui context, and orchestrates subsystems.
class App {
public:
	App();
	~App();

	int run();

	// Called by wndproc on resize
	void on_resize(unsigned int w, unsigned int h) { resize_w_ = w; resize_h_ = h; }

private:
	bool init_window();
	bool init_d3d();
	void init_imgui();
	void handle_resize();
	void cleanup();

	HWND hwnd_ = nullptr;
	ID3D11Device* device_ = nullptr;
	ID3D11DeviceContext* context_ = nullptr;
	IDXGISwapChain* swap_chain_ = nullptr;
	ID3D11RenderTargetView* rtv_ = nullptr;
	unsigned int resize_w_ = 0;
	unsigned int resize_h_ = 0;

	AgentManager agents_;
	Renderer renderer_;
	InputHandler input_;
};

} // namespace claude_hub
