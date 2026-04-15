#include "app.hpp"
#include "config.hpp"

#include <d3d11.h>
#include <dxgi.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#include <cstdlib>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace claude_hub {

// ─── Window procedure (forwards to App) ───

static App* g_app = nullptr;

static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
	if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return 1;
	if (m == WM_SIZE && w != SIZE_MINIMIZED && g_app)
		g_app->on_resize(LOWORD(l), HIWORD(l));
	if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
	return DefWindowProcW(h, m, w, l);
}

// ─── App lifecycle ───

App::App() { g_app = this; }

App::~App() {
	cleanup();
	g_app = nullptr;
}

int App::run() {
	if (!init_window()) return 1;
	if (!init_d3d()) return 1;
	init_imgui();

	// Spawn first agent
	const char* home = std::getenv("USERPROFILE");
	std::string cwd = home ? home : "C:\\Users\\liors";
	agents_.spawn(cwd, 100, 40);

	MSG msg{};
	while (msg.message != WM_QUIT) {
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			continue;
		}

		handle_resize();

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		agents_.drain_all();

		bool spawn = input_.process(agents_);
		if (spawn) agents_.spawn(cwd, 100, 40);

		renderer_.render(agents_);

		ImGui::Render();
		const float clear[] = {0.1f, 0.1f, 0.12f, 1};
		context_->OMSetRenderTargets(1, &rtv_, nullptr);
		context_->ClearRenderTargetView(rtv_, clear);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		swap_chain_->Present(1, 0);
	}

	agents_.shutdown();
	return 0;
}

// ─── Initialization ───

bool App::init_window() {
	WNDCLASSEXW wc{};
	wc.cbSize = sizeof(wc);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = wnd_proc;
	wc.hInstance = GetModuleHandle(nullptr);
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.lpszClassName = L"ClaudeHubWindow";
	RegisterClassExW(&wc);

	hwnd_ = CreateWindowExW(
		0, L"ClaudeHubWindow", L"Claude-Hub",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, 1400, 900,
		nullptr, nullptr, wc.hInstance, nullptr);

	if (!hwnd_) return false;
	ShowWindow(hwnd_, SW_SHOWDEFAULT);
	UpdateWindow(hwnd_);
	return true;
}

bool App::init_d3d() {
	DXGI_SWAP_CHAIN_DESC sd{};
	sd.BufferCount = 2;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = hwnd_;
	sd.SampleDesc.Count = 1;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	D3D_FEATURE_LEVEL fl;
	HRESULT hr = D3D11CreateDeviceAndSwapChain(
		nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
		nullptr, 0, D3D11_SDK_VERSION,
		&sd, &swap_chain_, &device_, &fl, &context_);

	if (FAILED(hr)) return false;

	ID3D11Texture2D* bb;
	swap_chain_->GetBuffer(0, IID_PPV_ARGS(&bb));
	device_->CreateRenderTargetView(bb, nullptr, &rtv_);
	bb->Release();
	return true;
}

void App::init_imgui() {
	ImGui::CreateContext();
	ImGui::StyleColorsDark();

	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 4.0f;
	style.FrameRounding = 2.0f;

	ImGuiIO& io = ImGui::GetIO();
	static const ImWchar glyph_ranges[] = {
		0x0020, 0x00FF,   // Basic Latin + Supplement
		0x2000, 0x206F,   // General Punctuation
		0x2190, 0x21FF,   // Arrows
		0x2500, 0x257F,   // Box Drawing
		0x2580, 0x259F,   // Block Elements
		0x25A0, 0x25FF,   // Geometric Shapes
		0xE000, 0xF8FF,   // Private Use Area (Powerline)
		0,
	};

	ImFontConfig fc;
	fc.OversampleH = 2;
	const char* font_paths[] = {
		"C:\\Windows\\Fonts\\CascadiaMono.ttf",
		"C:\\Windows\\Fonts\\consola.ttf",
	};
	for (const char* path : font_paths) {
		if (io.Fonts->AddFontFromFileTTF(path, 15.0f, &fc, glyph_ranges))
			break;
	}

	ImGui_ImplWin32_Init(hwnd_);
	ImGui_ImplDX11_Init(device_, context_);
}

void App::handle_resize() {
	if (resize_w_ == 0 || resize_h_ == 0) return;

	if (rtv_) { rtv_->Release(); rtv_ = nullptr; }
	swap_chain_->ResizeBuffers(0, resize_w_, resize_h_, DXGI_FORMAT_UNKNOWN, 0);
	resize_w_ = resize_h_ = 0;

	ID3D11Texture2D* bb;
	swap_chain_->GetBuffer(0, IID_PPV_ARGS(&bb));
	device_->CreateRenderTargetView(bb, nullptr, &rtv_);
	bb->Release();
}

void App::cleanup() {
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	if (rtv_) rtv_->Release();
	if (swap_chain_) swap_chain_->Release();
	if (context_) context_->Release();
	if (device_) device_->Release();
	if (hwnd_) DestroyWindow(hwnd_);
}

} // namespace claude_hub
