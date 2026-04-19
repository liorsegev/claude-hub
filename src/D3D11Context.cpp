#include "D3D11Context.hpp"

#include <dxgi.h>

namespace ch {

D3D11Context::D3D11Context(HWND hwnd) : hwnd_(hwnd) {
	DXGI_SWAP_CHAIN_DESC sd{};
	sd.BufferCount = 2;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = hwnd_;
	sd.SampleDesc.Count = 1;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	D3D_FEATURE_LEVEL fl;
	D3D11CreateDeviceAndSwapChain(
		nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
		nullptr, 0, D3D11_SDK_VERSION, &sd,
		&swap_chain_, &device_, &fl, &context_);

	create_render_target();
}

D3D11Context::~D3D11Context() {
	release_render_target();
	if (swap_chain_) swap_chain_->Release();
	if (context_) context_->Release();
	if (device_) device_->Release();
}

void D3D11Context::create_render_target() {
	ID3D11Texture2D* back = nullptr;
	if (FAILED(swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back)))) return;
	device_->CreateRenderTargetView(back, nullptr, &render_target_);
	back->Release();
}

void D3D11Context::release_render_target() {
	if (render_target_) {
		render_target_->Release();
		render_target_ = nullptr;
	}
}

void D3D11Context::queue_resize(unsigned int w, unsigned int h) {
	pending_w_ = w;
	pending_h_ = h;
}

void D3D11Context::apply_pending_resize() {
	if (pending_w_ == 0 || pending_h_ == 0) return;
	release_render_target();
	swap_chain_->ResizeBuffers(0, pending_w_, pending_h_, DXGI_FORMAT_UNKNOWN, 0);
	pending_w_ = pending_h_ = 0;
	create_render_target();
}

void D3D11Context::clear(const float rgba[4]) {
	context_->OMSetRenderTargets(1, &render_target_, nullptr);
	context_->ClearRenderTargetView(render_target_, rgba);
}

void D3D11Context::present() {
	swap_chain_->Present(1, 0);
}

}
