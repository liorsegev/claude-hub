#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <d3d11.h>

namespace ch {

// Owns the D3D11 device/context/swapchain/RTV tied to a single HWND.
class D3D11Context {
public:
	explicit D3D11Context(HWND hwnd);
	~D3D11Context();

	D3D11Context(const D3D11Context&) = delete;
	D3D11Context& operator=(const D3D11Context&) = delete;

	ID3D11Device* device() const { return device_; }
	ID3D11DeviceContext* context() const { return context_; }

	void queue_resize(unsigned int w, unsigned int h);
	void apply_pending_resize();

	void clear(const float rgba[4]);
	void present();

private:
	void create_render_target();
	void release_render_target();

	HWND hwnd_ = nullptr;
	ID3D11Device* device_ = nullptr;
	ID3D11DeviceContext* context_ = nullptr;
	IDXGISwapChain* swap_chain_ = nullptr;
	ID3D11RenderTargetView* render_target_ = nullptr;
	unsigned int pending_w_ = 0;
	unsigned int pending_h_ = 0;
};

}
