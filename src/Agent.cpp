#include "Agent.hpp"
#include "JsonlActivityProbe.hpp"

namespace ch {

Agent::Agent(AgentKind kind,
             std::string name,
             HWND wt_window,
             HANDLE wt_process,
             unsigned int claude_pid,
             std::string cwd,
             std::set<std::string> jsonl_snapshot,
             std::chrono::steady_clock::time_point spawn_time)
	: kind_(kind)
	, name_(std::move(name))
	, window_(wt_window)
	, process_(wt_process)
	, claude_pid_(claude_pid)
	, cwd_(std::move(cwd))
	, jsonl_snapshot_(std::move(jsonl_snapshot))
	, spawn_time_(spawn_time) {}

Agent::~Agent() {
	if (process_) CloseHandle(process_);
}

void Agent::reparent_as_child(HWND parent) {
	LONG style = GetWindowLongW(window_, GWL_STYLE);
	style = (style & ~(WS_POPUP | WS_CAPTION | WS_THICKFRAME)) | WS_CHILD;
	SetWindowLongW(window_, GWL_STYLE, style);
	SetParent(window_, parent);
}

void Agent::show()  { ShowWindow(window_, SW_SHOW); }
void Agent::hide()  { ShowWindow(window_, SW_HIDE); }
void Agent::focus() { SetFocus(window_); }

void Agent::move_to(int x, int y, int w, int h) {
	MoveWindow(window_, x, y, w, h, TRUE);
}

bool Agent::is_alive() const {
	if (!process_) return false;
	return WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
}

void Agent::attach_jsonl(std::filesystem::path jsonl_path, Logger* log) {
	jsonl_path_ = jsonl_path;
	probe_ = std::make_unique<JsonlActivityProbe>(
		std::move(jsonl_path), name_, log);
}

}
