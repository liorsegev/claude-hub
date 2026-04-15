// Claude-Hub: ImGui + ConPTY multiplexer
// Built up from the minimal version that doesn't freeze.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

#include "conpty.hpp"
#include "config.hpp"

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <cstring>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

// ─── D3D globals ───
static ID3D11Device* g_dev = nullptr;
static ID3D11DeviceContext* g_ctx = nullptr;
static IDXGISwapChain* g_sc = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static UINT g_rw = 0, g_rh = 0;

// ─── Agent ───
struct Agent {
	static constexpr int RING_SIZE = 512 * 1024;

	claude_hub::ConPTY pty;
	char ring[RING_SIZE];
	std::atomic<int> ring_write{0};
	int ring_read = 0;
	std::vector<std::string> lines = {""};
	std::string name;
	std::string local_echo;
	std::atomic<bool> alive{false};

	Agent() { memset(ring, 0, RING_SIZE); }
};

static std::vector<std::unique_ptr<Agent>> g_agents;
static int g_active = -1;

static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
	if (ImGui_ImplWin32_WndProcHandler(h, m, w, l)) return 1;
	if (m == WM_SIZE && w != SIZE_MINIMIZED) { g_rw = LOWORD(l); g_rh = HIWORD(l); return 0; }
	if (m == WM_DESTROY) { PostQuitMessage(0); return 0; }
	return DefWindowProcW(h, m, w, l);
}

static void spawn_agent() {
	auto a = std::make_unique<Agent>();
	a->name = "agent-" + std::to_string(g_agents.size());

	std::string cmd = claude_hub::claude_exe_path();
	const char* home = std::getenv("USERPROFILE");
	std::string cwd = home ? home : "C:\\Users\\liors";

	if (!a->pty.spawn(cmd, cwd, 100, 40)) return;
	a->alive = true;

	Agent* raw = a.get();
	a->pty.start_reader([raw](const char* data, size_t len) {
		int w = raw->ring_write.load(std::memory_order_relaxed);
		for (size_t i = 0; i < len; ++i)
			raw->ring[w++ % Agent::RING_SIZE] = data[i];
		raw->ring_write.store(w, std::memory_order_release);
	});

	g_agents.push_back(std::move(a));
	g_active = (int)g_agents.size() - 1;
}

static void drain_ring(Agent& a, int budget) {
	int rw = a.ring_write.load(std::memory_order_acquire);
	while (a.ring_read < rw && budget > 0) {
		char c = a.ring[a.ring_read % Agent::RING_SIZE];
		a.ring_read++; budget--;
		if (c == '\n') {
			a.lines.push_back("");
			while (a.lines.size() > 500) a.lines.erase(a.lines.begin());
		} else if (c == '\r') {
		} else if (c == '\x1b') {
			if (a.ring_read < rw && a.ring[a.ring_read % Agent::RING_SIZE] == '[') {
				a.ring_read++; budget--;
				while (a.ring_read < rw) {
					char sc = a.ring[a.ring_read % Agent::RING_SIZE];
					a.ring_read++; budget--;
					if (sc >= 0x40) break;
				}
			} else if (a.ring_read < rw) { a.ring_read++; budget--; }
		} else if (c >= 0x20 || (unsigned char)c >= 0x80) {
			a.lines.back() += c;
		}
	}
}

static void render_terminal(Agent& a, bool is_active) {
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.06f, 0.08f, 1));
	ImGui::BeginChild("##term", ImVec2(-1, -1), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);

	ImGuiListClipper clip;
	clip.Begin((int)a.lines.size());
	while (clip.Step()) {
		for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r)
			ImGui::TextUnformatted(a.lines[r].c_str(), a.lines[r].c_str() + a.lines[r].size());
	}
	clip.End();

	if (is_active && !a.local_echo.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 1, 0.5f, 1));
		std::string echo = "> " + a.local_echo + "_";
		ImGui::TextUnformatted(echo.c_str());
		ImGui::PopStyleColor();
	}

	if (is_active && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20)
		ImGui::SetScrollHereY(1.0f);

	ImGui::EndChild();
	ImGui::PopStyleColor();
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	WNDCLASSEXW wc{sizeof(wc), CS_HREDRAW|CS_VREDRAW, wnd_proc, 0, 0,
		GetModuleHandle(nullptr), nullptr, LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr,
		L"ClaudeHub", nullptr};
	RegisterClassExW(&wc);
	HWND hwnd = CreateWindowExW(0, L"ClaudeHub", L"Claude-Hub",
		WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1400, 900,
		nullptr, nullptr, wc.hInstance, nullptr);
	ShowWindow(hwnd, SW_SHOWDEFAULT);

	DXGI_SWAP_CHAIN_DESC sd{};
	sd.BufferCount = 2; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.OutputWindow = hwnd;
	sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	D3D_FEATURE_LEVEL fl;
	D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
		nullptr, 0, D3D11_SDK_VERSION, &sd, &g_sc, &g_dev, &fl, &g_ctx);
	ID3D11Texture2D* bb; g_sc->GetBuffer(0, IID_PPV_ARGS(&bb));
	g_dev->CreateRenderTargetView(bb, nullptr, &g_rtv); bb->Release();

	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGuiIO& io = ImGui::GetIO();

	static const ImWchar ranges[] = {
		0x0020,0x00FF, 0x2500,0x257F, 0x2580,0x259F, 0x25A0,0x25FF,
		0x2000,0x206F, 0x2190,0x21FF, 0xE000,0xF8FF, 0};
	ImFontConfig fc; fc.OversampleH = 2;
	const char* fonts[] = {"C:\\Windows\\Fonts\\CascadiaMono.ttf", "C:\\Windows\\Fonts\\consola.ttf"};
	for (auto* f : fonts) { if (io.Fonts->AddFontFromFileTTF(f, 15.0f, &fc, ranges)) break; }

	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX11_Init(g_dev, g_ctx);

	// Spawn first agent
	spawn_agent();

	MSG msg{};
	while (msg.message != WM_QUIT) {
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg); DispatchMessage(&msg); continue;
		}

		if (g_rw > 0 && g_rh > 0) {
			if (g_rtv) g_rtv->Release(); g_rtv = nullptr;
			g_sc->ResizeBuffers(0, g_rw, g_rh, DXGI_FORMAT_UNKNOWN, 0);
			g_rw = g_rh = 0;
			ID3D11Texture2D* b; g_sc->GetBuffer(0, IID_PPV_ARGS(&b));
			g_dev->CreateRenderTargetView(b, nullptr, &g_rtv); b->Release();
		}

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		// Drain ALL agents (1KB each per frame)
		for (auto& a : g_agents) drain_ring(*a, 1024);

		// ─── Keyboard ───
		bool ctrl = io.KeyCtrl;
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) spawn_agent();
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_RightBracket, false) && g_agents.size() >= 2)
			g_active = (g_active + 1) % (int)g_agents.size();
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_W, false) && g_active >= 0) {
			g_agents[g_active]->pty.kill();
			g_agents.erase(g_agents.begin() + g_active);
			if (g_agents.empty()) g_active = -1;
			else g_active = g_active % (int)g_agents.size();
		}

		// Forward input to active agent
		if (g_active >= 0 && !ctrl && !ImGui::IsAnyItemActive()) {
			auto& a = *g_agents[g_active];
			for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
				ImWchar ch = io.InputQueueCharacters[i];
				if (ch < 0x80) {
					char cc = (char)ch;
					a.pty.write(&cc, 1);
					a.local_echo += cc;
				}
			}
			io.InputQueueCharacters.resize(0);

			if (ImGui::IsKeyPressed(ImGuiKey_Enter, true)) { a.pty.write("\r"); a.local_echo.clear(); }
			if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true)) {
				a.pty.write("\x7f", 1);
				if (!a.local_echo.empty()) a.local_echo.pop_back();
			}
			if (ImGui::IsKeyPressed(ImGuiKey_Tab, true)) a.pty.write("\t");
			if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true)) a.pty.write("\x1b[A");
			if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true)) a.pty.write("\x1b[B");
			if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) a.pty.write("\x1b[C");
			if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) a.pty.write("\x1b[D");
			if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) a.pty.write("\x1b");
		}
		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && g_active >= 0) {
			g_agents[g_active]->pty.write("\x03", 1);
			g_agents[g_active]->local_echo.clear();
		}

		// ─── Layout ───
		ImGuiViewport* vp = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(vp->WorkPos);
		ImGui::SetNextWindowSize(vp->WorkSize);
		ImGui::Begin("##Main", nullptr,
			ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_MenuBar);

		// Menu bar with agent tabs
		if (ImGui::BeginMenuBar()) {
			if (ImGui::MenuItem("+ New (Ctrl+N)")) spawn_agent();
			ImGui::Separator();
			for (int i = 0; i < (int)g_agents.size(); ++i) {
				bool selected = (i == g_active);
				std::string label = g_agents[i]->name;
				if (selected) label = "* " + label;
				if (ImGui::MenuItem(label.c_str(), nullptr, selected))
					g_active = i;
			}
			ImGui::EndMenuBar();
		}

		if (g_agents.empty()) {
			ImGui::Text("No agents. Press Ctrl+N to start.");
		} else {
			ImVec2 avail = ImGui::GetContentRegionAvail();
			float main_w = avail.x * 0.7f;
			float side_w = avail.x * 0.3f - 8;

			// Main panel
			ImGui::BeginChild("MainPanel", ImVec2(main_w, avail.y), true);
			if (g_active >= 0) {
				ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "Active: %s", g_agents[g_active]->name.c_str());
				ImGui::Separator();
				render_terminal(*g_agents[g_active], true);
			}
			ImGui::EndChild();

			ImGui::SameLine();

			// Side panels
			ImGui::BeginChild("SidePanels", ImVec2(side_w, avail.y), false);
			int side_count = 0;
			for (int i = 0; i < (int)g_agents.size(); ++i) if (i != g_active) side_count++;
			float panel_h = side_count > 0 ? (ImGui::GetContentRegionAvail().y / side_count) : avail.y;

			for (int i = 0; i < (int)g_agents.size(); ++i) {
				if (i == g_active) continue;
				auto& a = *g_agents[i];
				std::string id = "##side" + std::to_string(i);
				ImGui::BeginChild(id.c_str(), ImVec2(-1, panel_h - 2), true);

				if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0))
					g_active = i;

				ImGui::TextColored(ImVec4(0.4f, 0.8f, 1, 1), "%s", a.name.c_str());
				ImGui::Separator();

				// Show last few lines as preview
				int preview_start = std::max(0, (int)a.lines.size() - 8);
				for (int r = preview_start; r < (int)a.lines.size(); ++r)
					ImGui::TextUnformatted(a.lines[r].c_str(), a.lines[r].c_str() + a.lines[r].size());

				ImGui::EndChild();
			}
			ImGui::EndChild();
		}

		ImGui::End();

		ImGui::Render();
		const float cc[] = {0.1f, 0.1f, 0.12f, 1};
		g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
		g_ctx->ClearRenderTargetView(g_rtv, cc);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		g_sc->Present(1, 0);
	}

	for (auto& a : g_agents) a->pty.kill();
	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
	if (g_rtv) g_rtv->Release();
	if (g_sc) g_sc->Release();
	if (g_ctx) g_ctx->Release();
	if (g_dev) g_dev->Release();
	return 0;
}
