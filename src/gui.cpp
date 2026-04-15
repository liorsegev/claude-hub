#include "gui.hpp"
#include "config.hpp"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <d3d11.h>
#include <dxgi.h>

#include <cstdio>
#include <algorithm>

// Forward declaration from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace claude_hub {

// ─── ANSI 16-color palette → ImGui colors ───

static const ImVec4 ANSI_COLORS[] = {
	{0.0f, 0.0f, 0.0f, 1.0f},       // 0 Black
	{0.8f, 0.2f, 0.2f, 1.0f},       // 1 Red
	{0.2f, 0.8f, 0.2f, 1.0f},       // 2 Green
	{0.8f, 0.8f, 0.2f, 1.0f},       // 3 Yellow
	{0.3f, 0.3f, 0.9f, 1.0f},       // 4 Blue
	{0.8f, 0.3f, 0.8f, 1.0f},       // 5 Magenta
	{0.3f, 0.8f, 0.8f, 1.0f},       // 6 Cyan
	{0.75f, 0.75f, 0.75f, 1.0f},    // 7 White
	{0.5f, 0.5f, 0.5f, 1.0f},       // 8 Bright Black
	{1.0f, 0.3f, 0.3f, 1.0f},       // 9 Bright Red
	{0.3f, 1.0f, 0.3f, 1.0f},       // 10 Bright Green
	{1.0f, 1.0f, 0.3f, 1.0f},       // 11 Bright Yellow
	{0.4f, 0.4f, 1.0f, 1.0f},       // 12 Bright Blue
	{1.0f, 0.4f, 1.0f, 1.0f},       // 13 Bright Magenta
	{0.4f, 1.0f, 1.0f, 1.0f},       // 14 Bright Cyan
	{1.0f, 1.0f, 1.0f, 1.0f},       // 15 Bright White
};

static ImVec4 term_color_to_imgui(const TermColor& tc, bool is_fg) {
	switch (tc.type) {
	case TermColor::Type::DEFAULT:
		return is_fg ? ImVec4(0.9f, 0.9f, 0.9f, 1.0f) : ImVec4(0.1f, 0.1f, 0.12f, 1.0f);
	case TermColor::Type::INDEXED: {
		if (tc.index < 16) return ANSI_COLORS[tc.index];
		if (tc.index < 232) {
			int idx = tc.index - 16;
			int r = idx / 36, g = (idx % 36) / 6, b = idx % 6;
			return ImVec4(r / 5.0f, g / 5.0f, b / 5.0f, 1.0f);
		}
		float v = (tc.index - 232) / 23.0f;
		return ImVec4(v, v, v, 1.0f);
	}
	case TermColor::Type::RGB:
		return ImVec4(tc.r / 255.0f, tc.g / 255.0f, tc.b / 255.0f, 1.0f);
	}
	return ImVec4(0.9f, 0.9f, 0.9f, 1.0f);
}

// ─── Window proc ───

static LRESULT CALLBACK wnd_proc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
		return 1;

	Gui* gui = reinterpret_cast<Gui*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

	switch (msg) {
	case WM_SIZE:
		if (wParam != SIZE_MINIMIZED && gui) {
			gui->resize_width_ = LOWORD(lParam);
			gui->resize_height_ = HIWORD(lParam);
		}
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// ─── Gui lifecycle ───

Gui::Gui() {}

Gui::~Gui() {
	for (auto& a : agents_) {
		a->alive = false;
		a->pty.kill();
	}
	cleanup_d3d();
	if (hwnd_) DestroyWindow(hwnd_);
}

bool Gui::init_window() {
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
		nullptr, nullptr, wc.hInstance, nullptr
	);

	if (!hwnd_) return false;
	SetWindowLongPtr(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
	ShowWindow(hwnd_, SW_SHOWDEFAULT);
	UpdateWindow(hwnd_);
	return true;
}

bool Gui::init_d3d() {
	DXGI_SWAP_CHAIN_DESC sd{};
	sd.BufferCount = 2;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = hwnd_;
	sd.SampleDesc.Count = 1;
	sd.Windowed = TRUE;
	sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

	D3D_FEATURE_LEVEL feature_level;
	HRESULT hr = D3D11CreateDeviceAndSwapChain(
		nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
		nullptr, 0, D3D11_SDK_VERSION,
		&sd, &swap_chain_, &device_, &feature_level, &context_
	);
	if (FAILED(hr)) return false;

	ID3D11Texture2D* back_buffer = nullptr;
	swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back_buffer));
	device_->CreateRenderTargetView(back_buffer, nullptr, &rtv_);
	back_buffer->Release();

	return true;
}

void Gui::cleanup_d3d() {
	if (rtv_) { rtv_->Release(); rtv_ = nullptr; }
	if (swap_chain_) { swap_chain_->Release(); swap_chain_ = nullptr; }
	if (context_) { context_->Release(); context_ = nullptr; }
	if (device_) { device_->Release(); device_ = nullptr; }
}

int Gui::run() {
	if (!init_window()) return 1;
	if (!init_d3d()) return 1;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	// Dark theme
	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 4.0f;
	style.FrameRounding = 2.0f;

	// Load Consolas with wide Unicode coverage (box-drawing, symbols, etc.)
	ImFontConfig font_cfg;
	font_cfg.OversampleH = 2;
	font_cfg.OversampleV = 1;

	// Glyph ranges: Basic Latin + Latin Extended + Box Drawing + Block Elements + Arrows + Math + Misc Symbols
	static const ImWchar glyph_ranges[] = {
		0x0020, 0x00FF, // Basic Latin + Latin Supplement
		0x0100, 0x024F, // Latin Extended-A + B
		0x2000, 0x206F, // General Punctuation
		0x2190, 0x21FF, // Arrows
		0x2200, 0x22FF, // Mathematical Operators
		0x2300, 0x23FF, // Miscellaneous Technical
		0x2500, 0x257F, // Box Drawing
		0x2580, 0x259F, // Block Elements
		0x25A0, 0x25FF, // Geometric Shapes
		0x2600, 0x26FF, // Miscellaneous Symbols
		0x2700, 0x27BF, // Dingbats
		0x2800, 0x28FF, // Braille Patterns
		0xE000, 0xF8FF, // Private Use Area (powerline symbols etc.)
		0,
	};

	// Try Cascadia Mono first (newer, better), then Consolas, then default
	ImFont* font = nullptr;
	const char* font_paths[] = {
		"C:\\Windows\\Fonts\\CascadiaMono.ttf",
		"C:\\Windows\\Fonts\\consola.ttf",
		"C:\\Windows\\Fonts\\lucon.ttf", // Lucida Console
	};
	for (const char* path : font_paths) {
		font = io.Fonts->AddFontFromFileTTF(path, 15.0f, &font_cfg, glyph_ranges);
		if (font) break;
	}
	if (!font) {
		io.Fonts->AddFontDefault();
	}

	ImGui_ImplWin32_Init(hwnd_);
	ImGui_ImplDX11_Init(device_, context_);

	// Main loop
	MSG msg{};
	while (msg.message != WM_QUIT) {
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			continue;
		}

		// Handle resize
		if (resize_width_ > 0 && resize_height_ > 0) {
			if (rtv_) { rtv_->Release(); rtv_ = nullptr; }
			swap_chain_->ResizeBuffers(0, resize_width_, resize_height_, DXGI_FORMAT_UNKNOWN, 0);
			resize_width_ = resize_height_ = 0;
			ID3D11Texture2D* bb = nullptr;
			swap_chain_->GetBuffer(0, IID_PPV_ARGS(&bb));
			device_->CreateRenderTargetView(bb, nullptr, &rtv_);
			bb->Release();
		}

		ImGui_ImplDX11_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		render_frame();

		ImGui::Render();
		const float clear_color[] = {0.1f, 0.1f, 0.12f, 1.0f};
		context_->OMSetRenderTargets(1, &rtv_, nullptr);
		context_->ClearRenderTargetView(rtv_, clear_color);
		ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
		swap_chain_->Present(1, 0); // VSync
	}

	ImGui_ImplDX11_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	return 0;
}

// ─── Frame rendering ───

void Gui::render_frame() {
	// Full-screen dockspace
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(viewport->WorkPos);
	ImGui::SetNextWindowSize(viewport->WorkSize);
	ImGui::Begin("##Main", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_MenuBar);

	// Menu bar
	if (ImGui::BeginMenuBar()) {
		if (ImGui::MenuItem("New Agent (Ctrl+N)")) spawn_agent();
		if (active_ >= 0 && ImGui::MenuItem("Kill Agent (Ctrl+W)")) kill_agent(active_);

		ImGui::Separator();
		for (int i = 0; i < (int)agents_.size(); ++i) {
			auto& a = *agents_[i];
			bool waiting = false;
			if (a.alive) {
				auto elapsed = std::chrono::steady_clock::now() - a.last_output_time.load();
				waiting = std::chrono::duration<double>(elapsed).count() > INPUT_QUIESCENCE_SECONDS;
			}

			std::string label = a.name;
			if (i == active_) label = "* " + label;
			if (waiting) label += " [WAITING]";
			if (!a.alive) label += " (dead)";

			bool selected = (i == active_);
			if (waiting && !selected) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 0, 1));
			if (ImGui::MenuItem(label.c_str(), nullptr, selected)) switch_agent(i);
			if (waiting && !selected) ImGui::PopStyleColor();
		}
		ImGui::EndMenuBar();
	}

	// Keyboard: forward ALL input to active agent, intercept our shortcuts
	ImGuiIO& frame_io = ImGui::GetIO();
	bool ctrl = frame_io.KeyCtrl;

	// Our shortcuts (Ctrl+N/W/])
	if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) { spawn_agent(); }
	else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_W, false) && active_ >= 0) { kill_agent(active_); }
	else if (ctrl && ImGui::IsKeyPressed(ImGuiKey_RightBracket, false) && agents_.size() >= 2) {
		switch_agent((active_ + 1) % (int)agents_.size());
	}
	// Forward everything else to active agent via input queue (never blocks render thread)
	else if (active_ >= 0 && active_ < (int)agents_.size() && agents_[active_]->alive
		&& !ImGui::IsAnyItemActive()) {
		auto& agent = *agents_[active_];

		auto queue_input = [&agent](const std::string& data) {
			std::lock_guard<std::mutex> lock(agent.input_mutex);
			agent.input_queue.push_back(data);
		};

		// Forward typed characters + local echo
		for (int i = 0; i < frame_io.InputQueueCharacters.Size; ++i) {
			ImWchar ch = frame_io.InputQueueCharacters[i];
			if (ch < 0x80) {
				queue_input(std::string(1, (char)ch));
				agent.local_echo += (char)ch; // Immediate echo
			} else {
				std::string utf8;
				if (ch < 0x800) {
					utf8 += (char)(0xC0 | (ch >> 6));
					utf8 += (char)(0x80 | (ch & 0x3F));
				} else {
					utf8 += (char)(0xE0 | (ch >> 12));
					utf8 += (char)(0x80 | ((ch >> 6) & 0x3F));
					utf8 += (char)(0x80 | (ch & 0x3F));
				}
				queue_input(utf8);
				agent.local_echo += utf8;
			}
		}
		frame_io.InputQueueCharacters.resize(0);

		// Special keys
		struct { ImGuiKey key; const char* seq; const char* echo; } specials[] = {
			{ImGuiKey_Enter,      "\r",      nullptr},  // Enter clears echo
			{ImGuiKey_Tab,        "\t",      nullptr},
			{ImGuiKey_Backspace,  "\x7f",    nullptr},  // Backspace handled below
			{ImGuiKey_Delete,     "\x1b[3~", nullptr},
			{ImGuiKey_UpArrow,    "\x1b[A",  nullptr},
			{ImGuiKey_DownArrow,  "\x1b[B",  nullptr},
			{ImGuiKey_RightArrow, "\x1b[C",  nullptr},
			{ImGuiKey_LeftArrow,  "\x1b[D",  nullptr},
			{ImGuiKey_Home,       "\x1b[H",  nullptr},
			{ImGuiKey_End,        "\x1b[F",  nullptr},
			{ImGuiKey_PageUp,     "\x1b[5~", nullptr},
			{ImGuiKey_PageDown,   "\x1b[6~", nullptr},
			{ImGuiKey_Escape,     "\x1b",    nullptr},
		};
		for (auto& s : specials) {
			if (ImGui::IsKeyPressed(s.key, true)) {
				queue_input(s.seq);
			}
		}

		// Enter clears local echo (command submitted)
		if (ImGui::IsKeyPressed(ImGuiKey_Enter, false))
			agent.local_echo.clear();

		// Backspace removes last char from echo
		if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true) && !agent.local_echo.empty())
			agent.local_echo.pop_back();

		if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
			queue_input(std::string(1, '\x03'));
			agent.local_echo.clear();
		}
	}

	if (agents_.empty()) {
		ImGui::Text("No agents. Press Ctrl+N or click 'New Agent' to start.");
		ImGui::End();
		return;
	}

	// Get available space
	ImVec2 avail = ImGui::GetContentRegionAvail();
	float main_w = avail.x * 0.7f;
	float side_w = avail.x * 0.3f;
	float full_h = avail.y;

	// Main panel — fixed size, fills left 70%
	if (active_ >= 0 && active_ < (int)agents_.size()) {
		ImGui::BeginChild("MainPanel", ImVec2(main_w, full_h), true);
		render_terminal(agents_[active_]->name.c_str(), *agents_[active_], true, false);
		ImGui::EndChild();
	}

	ImGui::SameLine();

	// Side panels — fixed height, fills right 30%
	ImGui::BeginChild("SidePanels", ImVec2(side_w, full_h), false);

	// Count non-active agents
	int side_count = 0;
	for (int i = 0; i < (int)agents_.size(); ++i)
		if (i != active_) ++side_count;

	float panel_h = side_count > 0 ? (ImGui::GetContentRegionAvail().y / side_count) : full_h;

	for (int i = 0; i < (int)agents_.size(); ++i) {
		if (i == active_) continue;
		auto& a = *agents_[i];

		bool waiting = false;
		if (a.alive) {
			auto elapsed = std::chrono::steady_clock::now() - a.last_output_time.load();
			waiting = std::chrono::duration<double>(elapsed).count() > INPUT_QUIESCENCE_SECONDS;
		}

		if (waiting) {
			ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 0, 1));
			ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 2.0f);
		}

		std::string id = "##side_" + std::to_string(i);
		ImGui::BeginChild(id.c_str(), ImVec2(-1, panel_h - 2), true);

		if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0))
			switch_agent(i);

		render_terminal(a.name.c_str(), a, false, true);
		ImGui::EndChild();

		if (waiting) {
			ImGui::PopStyleVar();
			ImGui::PopStyleColor();
		}
	}
	ImGui::EndChild();

	ImGui::End();
}

// ─── Terminal rendering ───

void Gui::render_terminal(const char* title, GuiAgent& agent, bool is_active, bool compact) {
	// Title bar
	bool waiting = false;
	if (agent.alive) {
		auto elapsed = std::chrono::steady_clock::now() - agent.last_output_time.load();
		waiting = std::chrono::duration<double>(elapsed).count() > INPUT_QUIESCENCE_SECONDS;
	}

	if (compact) {
		if (waiting)
			ImGui::TextColored(ImVec4(1, 1, 0, 1), "%s [WAITING]", title);
		else
			ImGui::TextColored(ImVec4(0.4f, 0.8f, 1, 1), "%s", title);
	} else {
		if (waiting)
			ImGui::TextColored(ImVec4(1, 1, 0, 1), "Active: %s [WAITING FOR INPUT]", title);
		else
			ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "Active: %s", title);
	}
	ImGui::Separator();

	// Drain ring buffer — max 1KB per frame to keep UI responsive
	int w = agent.ring_write.load(std::memory_order_acquire);
	int budget = 2048;
	while (agent.ring_read < w && budget > 0) {
		char c = agent.ring[agent.ring_read % GuiAgent::RING_SIZE];
		agent.ring_read++;
		budget--;

		if (c == '\n') {
			agent.display_lines.push_back("");
			// Cap at 500 lines — remove from front
			while (agent.display_lines.size() > 500)
				agent.display_lines.erase(agent.display_lines.begin());
		} else if (c == '\r') {
			// ignore
		} else if (c == '\x1b') {
			if (agent.ring_read < w) {
				char next = agent.ring[agent.ring_read % GuiAgent::RING_SIZE];
				if (next == '[') {
					agent.ring_read++; budget--;
					while (agent.ring_read < w) {
						char sc = agent.ring[agent.ring_read % GuiAgent::RING_SIZE];
						agent.ring_read++; budget--;
						if (sc >= 0x40) break;
					}
				} else {
					agent.ring_read++; budget--;
				}
			}
		} else if (c >= 0x20 || (unsigned char)c >= 0x80) {
			if (agent.display_lines.empty()) agent.display_lines.push_back("");
			agent.display_lines.back() += c;
		}
	}

	const auto& lines = agent.display_lines;

	// Terminal content area — fills remaining space, scrollable
	float remaining_h = ImGui::GetContentRegionAvail().y;
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.06f, 0.08f, 1.0f));
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
	ImGui::BeginChild("##term", ImVec2(-1, remaining_h), false,
		ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysVerticalScrollbar);

	// Render lines with clipping (ImGui only renders visible items)
	ImGuiListClipper clipper;
	clipper.Begin((int)lines.size());
	while (clipper.Step()) {
		for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
			ImGui::TextUnformatted(lines[row].c_str(), lines[row].c_str() + lines[row].size());
		}
	}
	clipper.End();

	// Show local echo line if user is typing (before ConPTY echoes back)
	if (is_active && !agent.local_echo.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 1.0f, 0.5f, 1.0f));
		std::string echo_line = "> " + agent.local_echo + "_";
		ImGui::TextUnformatted(echo_line.c_str());
		ImGui::PopStyleColor();
	}

	// Auto-scroll
	if (is_active && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20)
		ImGui::SetScrollHereY(1.0f);

	ImGui::EndChild();
	ImGui::PopStyleColor(2);
}

// ─── Agent management ───

void Gui::spawn_agent() {
	// Calculate terminal size from window dimensions and font size
	// Main panel is 70% of window width, font is ~8px wide, ~15px tall
	RECT rect;
	GetClientRect(hwnd_, &rect);
	int win_w = rect.right - rect.left;
	int win_h = rect.bottom - rect.top;
	int cols = (int)(win_w * 0.7f / 8.0f);  // ~8px per char
	int rows = (int)(win_h / 16.0f);         // ~16px per line
	cols = std::max(40, std::min(cols, 250));
	rows = std::max(10, std::min(rows, 80));

	auto agent = std::make_unique<GuiAgent>(cols, rows);
	agent->name = "agent-" + std::to_string(agents_.size());

	std::string cmd = claude_exe_path();
	std::string cwd;
	const char* home = std::getenv("USERPROFILE");
	cwd = home ? home : "C:\\Users\\liors";

	if (!agent->pty.spawn(cmd, cwd, cols, rows))
		return;

	agent->alive = true;

	// Start reader thread
	GuiAgent* raw = agent.get();
	agent->pty.start_reader([raw](const char* data, size_t len) {
		raw->last_output_time = std::chrono::steady_clock::now();

		// Just copy raw bytes into ring buffer. ZERO allocations.
		int w = raw->ring_write.load(std::memory_order_relaxed);
		for (size_t i = 0; i < len; ++i) {
			raw->ring[w % GuiAgent::RING_SIZE] = data[i];
			w++;
		}
		raw->ring_write.store(w, std::memory_order_release);
	});

	// Start input writer thread — pops from queue, writes to ConPTY pipe
	// This ensures WriteFile never blocks the ImGui render thread
	agent->input_thread = std::thread([raw]() {
		while (raw->alive) {
			std::string data;
			{
				std::lock_guard<std::mutex> lock(raw->input_mutex);
				if (!raw->input_queue.empty()) {
					data = std::move(raw->input_queue.front());
					raw->input_queue.erase(raw->input_queue.begin());
				}
			}
			if (!data.empty()) {
				raw->pty.write(data);
			} else {
				Sleep(5); // Brief sleep when queue is empty
			}
		}
	});

	agents_.push_back(std::move(agent));
	active_ = (int)agents_.size() - 1;
}

void Gui::switch_agent(int idx) {
	if (idx < 0 || idx >= (int)agents_.size()) return;
	active_ = idx;
}

void Gui::kill_agent(int idx) {
	if (idx < 0 || idx >= (int)agents_.size()) return;
	agents_[idx]->alive = false;
	agents_[idx]->pty.kill();
	if (agents_[idx]->input_thread.joinable()) agents_[idx]->input_thread.join();
	agents_.erase(agents_.begin() + idx);
	if (agents_.empty()) {
		active_ = -1;
	} else {
		active_ = std::min(active_, (int)agents_.size() - 1);
	}
}

} // namespace claude_hub
