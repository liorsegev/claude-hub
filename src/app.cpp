#include "app.hpp"
#include "config.hpp"

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
#include <cstdlib>
#include <cstdio>
#include <thread>
#include <atomic>
#include <vector>
#include <string>

namespace claude_hub {

// ─── Agent ───

struct PtyAgent {
	HPCON hpc = nullptr;
	HANDLE pipe_in = INVALID_HANDLE_VALUE;
	HANDLE pipe_out = INVALID_HANDLE_VALUE;
	PROCESS_INFORMATION pi{};
	std::thread output_thread;
	std::atomic<bool> alive{false};
	std::atomic<bool> forwarding{false};
	std::string name;
	std::atomic<std::chrono::steady_clock::time_point> last_output_time{
		std::chrono::steady_clock::now()
	};
	TerminalBuffer buffer;
	PtyAgent() : buffer(80, 40) {}
};

static std::vector<std::unique_ptr<PtyAgent>> g_agents;
static int g_active = -1;
static std::atomic<bool> g_running{true};
static HANDLE g_stdout = INVALID_HANDLE_VALUE;
static HANDLE g_stdin = INVALID_HANDLE_VALUE;
static DWORD g_orig_in_mode = 0;
static int g_term_cols = 120;
static int g_term_rows = 40;
static HANDLE g_wake_event = nullptr;

static std::thread g_input_thread;
static std::atomic<bool> g_input_active{false};

enum class Action { NONE, SPAWN, NEXT, KILL, QUIT };
static std::atomic<Action> g_action{Action::NONE};

// Forward declarations
static void activate_agent(int idx);
static bool create_pty_agent(PtyAgent& agent, int cols, int rows);
static void destroy_pty_agent(PtyAgent& agent);

// ─── ConPTY ───

static bool create_pty_agent(PtyAgent& agent, int cols, int rows) {
	HANDLE pipe_pty_in, pipe_pty_out;
	if (!CreatePipe(&pipe_pty_in, &agent.pipe_in, nullptr, 0)) return false;
	if (!CreatePipe(&agent.pipe_out, &pipe_pty_out, nullptr, 0)) {
		CloseHandle(pipe_pty_in); CloseHandle(agent.pipe_in); return false;
	}
	COORD size{(SHORT)cols, (SHORT)rows};
	HRESULT hr = CreatePseudoConsole(size, pipe_pty_in, pipe_pty_out, 0, &agent.hpc);
	CloseHandle(pipe_pty_in); CloseHandle(pipe_pty_out);
	if (FAILED(hr)) return false;

	STARTUPINFOEXW si{}; si.StartupInfo.cb = sizeof(si);
	SIZE_T attr_size = 0;
	InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
	std::vector<BYTE> buf(attr_size);
	si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(buf.data());
	InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attr_size);
	UpdateProcThreadAttribute(si.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
		agent.hpc, sizeof(agent.hpc), nullptr, nullptr);

	std::string cmd_s = claude_exe_path();
	std::wstring cmd(cmd_s.begin(), cmd_s.end());
	BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
		EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr, &si.StartupInfo, &agent.pi);
	DeleteProcThreadAttributeList(si.lpAttributeList);
	if (!ok) return false;

	agent.alive = true;
	agent.buffer.resize(cols, rows);

	agent.output_thread = std::thread([&agent]() {
		char rbuf[16384];
		while (agent.alive) {
			DWORD n = 0;
			if (!ReadFile(agent.pipe_out, rbuf, sizeof(rbuf), &n, nullptr) || n == 0) {
				agent.alive = false; break;
			}
			agent.buffer.feed(rbuf, n);
			agent.last_output_time = std::chrono::steady_clock::now();
			if (agent.forwarding) {
				DWORD w;
				WriteConsoleA(g_stdout, rbuf, n, &w, nullptr);
			}
		}
		SetEvent(g_wake_event);
	});
	return true;
}

static void destroy_pty_agent(PtyAgent& agent) {
	agent.alive = false; agent.forwarding = false;
	if (agent.pi.hProcess) {
		TerminateProcess(agent.pi.hProcess, 0);
		WaitForSingleObject(agent.pi.hProcess, 2000);
		CloseHandle(agent.pi.hProcess); CloseHandle(agent.pi.hThread);
		agent.pi = {};
	}
	if (agent.hpc) { ClosePseudoConsole(agent.hpc); agent.hpc = nullptr; }
	if (agent.pipe_in != INVALID_HANDLE_VALUE) { CloseHandle(agent.pipe_in); agent.pipe_in = INVALID_HANDLE_VALUE; }
	if (agent.pipe_out != INVALID_HANDLE_VALUE) { CloseHandle(agent.pipe_out); agent.pipe_out = INVALID_HANDLE_VALUE; }
	if (agent.output_thread.joinable()) agent.output_thread.join();
}

// ─── Input forwarding ───

static void start_input(PtyAgent& agent) {
	g_input_active = true;
	g_input_thread = std::thread([&agent]() {
		char buf[256];
		while (g_input_active && agent.alive) {
			DWORD n = 0;
			if (!ReadFile(g_stdin, buf, sizeof(buf), &n, nullptr) || n == 0 || !g_input_active) break;
			DWORD w;
			WriteFile(agent.pipe_in, buf, n, &w, nullptr);
		}
	});
}

static void stop_input() {
	g_input_active = false;
	if (g_input_thread.joinable()) {
		INPUT_RECORD ir{}; ir.EventType = KEY_EVENT;
		ir.Event.KeyEvent.bKeyDown = TRUE; ir.Event.KeyEvent.wRepeatCount = 1;
		DWORD w; WriteConsoleInput(g_stdin, &ir, 1, &w);
		g_input_thread.join();
	}
}

// ─── Status bar ───
// Written to the last row of the visible viewport using WriteConsoleOutputCharacterA

static void draw_status_bar() {
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(g_stdout, &csbi);
	SHORT status_row = csbi.srWindow.Bottom;

	std::string bar = " ";
	for (int i = 0; i < (int)g_agents.size(); ++i) {
		auto& a = *g_agents[i];
		bool waiting = false;
		auto elapsed = std::chrono::steady_clock::now() - a.last_output_time.load();
		if (std::chrono::duration<double>(elapsed).count() > INPUT_QUIESCENCE_SECONDS && a.alive)
			waiting = true;

		if (i == g_active)
			bar += "[*" + a.name;
		else
			bar += "[ " + a.name;

		if (waiting) bar += " WAITING!";
		else if (!a.alive) bar += " exited";
		bar += " ] ";
	}
	bar += "| Ctrl+N:new Ctrl+]:switch Ctrl+W:kill Ctrl+Q:quit";

	// Pad to full width
	while ((int)bar.size() < g_term_cols) bar += ' ';
	bar.resize(g_term_cols);

	// Write using direct console API (not VT — won't interfere with Claude)
	COORD pos{0, status_row};
	DWORD w;
	WriteConsoleOutputCharacterA(g_stdout, bar.c_str(), (DWORD)bar.size(), pos, &w);

	// Highlight: reverse video for status bar
	std::vector<WORD> attrs(bar.size(), 0x70); // Black on white (reverse)
	// Highlight WAITING agents in yellow
	// Highlight active agent marker
	WriteConsoleOutputAttribute(g_stdout, attrs.data(), (DWORD)attrs.size(), pos, &w);
}

static void status_thread() {
	while (g_running) {
		Sleep(500);
		if (!g_running) break;
		if (!g_agents.empty()) draw_status_bar();
	}
}

// ─── Hotkey thread ───

static void hotkey_thread() {
	RegisterHotKey(nullptr, 1, MOD_CONTROL | MOD_NOREPEAT, 'N');
	RegisterHotKey(nullptr, 2, MOD_CONTROL | MOD_NOREPEAT, VK_OEM_6);
	RegisterHotKey(nullptr, 3, MOD_CONTROL | MOD_NOREPEAT, 'W');
	RegisterHotKey(nullptr, 4, MOD_CONTROL | MOD_NOREPEAT, 'Q');

	MSG msg;
	while (g_running && GetMessage(&msg, nullptr, 0, 0)) {
		if (msg.message != WM_HOTKEY) continue;
		switch (msg.wParam) {
		case 1: g_action = Action::SPAWN; SetEvent(g_wake_event); break;
		case 2: // NEXT — switch without killing
			if (g_agents.size() >= 2) {
				int next = (g_active + 1) % (int)g_agents.size();
				activate_agent(next);
			}
			break;
		case 3: g_action = Action::KILL; SetEvent(g_wake_event); break;
		case 4: g_action = Action::QUIT; SetEvent(g_wake_event); break;
		}
	}
	for (int i = 1; i <= 4; ++i) UnregisterHotKey(nullptr, i);
}

// ─── Activate ───

static void activate_agent(int idx) {
	if (g_active >= 0 && g_active < (int)g_agents.size())
		g_agents[g_active]->forwarding = false;
	stop_input();

	g_active = idx;
	auto& agent = *g_agents[idx];
	agent.forwarding = true;

	// Replay buffer
	auto lines = agent.buffer.text_lines();
	DWORD w;
	WriteConsoleA(g_stdout, "\x1b[2J\x1b[H", 7, &w, nullptr);
	for (auto& line : lines) {
		WriteConsoleA(g_stdout, line.c_str(), (DWORD)line.size(), &w, nullptr);
		WriteConsoleA(g_stdout, "\r\n", 2, &w, nullptr);
	}

	start_input(agent);
	draw_status_bar();
}

// ─── App ───

App::App() : screen_(ftxui::ScreenInteractive::Fullscreen()) {}
App::~App() {
	g_running = false;
	if (g_wake_event) SetEvent(g_wake_event);
	stop_input();
	for (auto& a : g_agents) destroy_pty_agent(*a);
	if (g_wake_event) CloseHandle(g_wake_event);
}

void App::run() {
	g_stdout = GetStdHandle(STD_OUTPUT_HANDLE);
	g_stdin = GetStdHandle(STD_INPUT_HANDLE);
	GetConsoleMode(g_stdin, &g_orig_in_mode);
	SetConsoleMode(g_stdin, ENABLE_VIRTUAL_TERMINAL_INPUT);
	SetConsoleMode(g_stdout, ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

	g_wake_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);

	CONSOLE_SCREEN_BUFFER_INFO csbi;
	GetConsoleScreenBufferInfo(g_stdout, &csbi);
	g_term_cols = csbi.srWindow.Right - csbi.srWindow.Left + 1;
	g_term_rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;

	std::thread(hotkey_thread).detach();
	std::thread(status_thread).detach();

	// Spawn first agent (full terminal width, minus 1 row for status bar)
	auto agent = std::make_unique<PtyAgent>();
	agent->name = "agent-0";
	if (create_pty_agent(*agent, g_term_cols, g_term_rows - 1)) {
		g_agents.push_back(std::move(agent));
		activate_agent(0);
	} else {
		printf("Failed to spawn Claude.\n");
		return;
	}

	while (g_running) {
		WaitForSingleObject(g_wake_event, INFINITE);

		Action act = g_action.exchange(Action::NONE);
		switch (act) {
		case Action::SPAWN: {
			if (g_active >= 0) g_agents[g_active]->forwarding = false;
			stop_input();
			auto a = std::make_unique<PtyAgent>();
			a->name = "agent-" + std::to_string(g_agents.size());
			if (create_pty_agent(*a, g_term_cols, g_term_rows - 1)) {
				g_agents.push_back(std::move(a));
				activate_agent((int)g_agents.size() - 1);
			}
			break;
		}
		case Action::KILL: {
			stop_input();
			destroy_pty_agent(*g_agents[g_active]);
			g_agents.erase(g_agents.begin() + g_active);
			if (!g_agents.empty()) {
				g_active = g_active % (int)g_agents.size();
				activate_agent(g_active);
			} else {
				g_active = -1; g_running = false;
			}
			break;
		}
		case Action::QUIT: g_running = false; break;
		default: break;
		}
	}

	SetConsoleMode(g_stdin, g_orig_in_mode);
	g_running = false;
	stop_input();
	for (auto& a : g_agents) destroy_pty_agent(*a);
	g_agents.clear();
}

// Stubs
void App::spawn_agent() {}
void App::switch_to_agent(const std::string&) {}
void App::kill_active_agent() {}
void App::next_agent() {}
void App::detection_loop() {}
void App::handle_key_event(const KEY_EVENT_RECORD&) {}
ftxui::Component App::build_ui() { return nullptr; }
ftxui::Element App::render_main_panel() { return ftxui::text(""); }
ftxui::Element App::render_side_panel(Agent*) { return ftxui::text(""); }
ftxui::Element App::render_status_bar() { return ftxui::text(""); }
ftxui::Element App::render_side_bar() { return ftxui::text(""); }
ftxui::Element App::cells_to_element(Agent*) { return ftxui::text(""); }
ftxui::Element App::render_row(const std::vector<Cell>&, int) { return ftxui::text(""); }
ftxui::Color App::cell_fg_color(const Cell&) { return ftxui::Color(); }
ftxui::Color App::cell_bg_color(const Cell&) { return ftxui::Color(); }
std::vector<Agent*> App::get_side_agents() { return {}; }
bool App::handle_key(ftxui::Event) { return false; }
bool App::handle_spawn_dialog_key(ftxui::Event) { return false; }

} // namespace claude_hub
