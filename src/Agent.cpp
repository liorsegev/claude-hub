#include "Agent.hpp"
#include "JsonlActivityProbe.hpp"

namespace ch {

Agent::Agent(std::string name,
             HWND wt_window,
             HANDLE wt_process,
             unsigned int claude_pid,
             std::string cwd,
             std::set<std::string> jsonl_snapshot,
             std::chrono::steady_clock::time_point spawn_time)
	: name_(std::move(name))
	, window_(wt_window)
	, process_(wt_process)
	, claude_pid_(claude_pid)
	, cwd_(std::move(cwd))
	, jsonl_snapshot_(std::move(jsonl_snapshot))
	, spawn_time_(spawn_time) {}

Agent::~Agent() {
	if (window_ && IsWindow(window_)) {
		SetParent(window_, nullptr);
		PostMessage(window_, WM_CLOSE, 0, 0);
	}
	if (process_) CloseHandle(process_);
}

void Agent::reparent_as_child(HWND parent) {
	LONG style = GetWindowLong(window_, GWL_STYLE);
	style &= ~(WS_CAPTION | WS_THICKFRAME | WS_POPUP | WS_SYSMENU);
	style |= WS_CHILD;
	SetWindowLong(window_, GWL_STYLE, style);
	SetParent(window_, parent);
}

void Agent::show() {
	if (window_ && IsWindow(window_)) ShowWindow(window_, SW_SHOW);
}

void Agent::hide() {
	if (window_ && IsWindow(window_)) ShowWindow(window_, SW_HIDE);
}

void Agent::move_to(int x, int y, int w, int h) {
	if (window_ && IsWindow(window_)) MoveWindow(window_, x, y, w, h, TRUE);
}

void Agent::focus() {
	if (window_ && IsWindow(window_)) SetFocus(window_);
}

bool Agent::is_alive() const {
	if (process_ && WaitForSingleObject(process_, 0) == WAIT_OBJECT_0) return false;
	if (window_ && !IsWindow(window_)) return false;
	return true;
}

void Agent::attach_jsonl(std::filesystem::path jsonl_path, Logger* log) {
	jsonl_path_ = std::move(jsonl_path);
	probe_ = std::make_unique<JsonlActivityProbe>(
		jsonl_path_, name_, log, std::chrono::steady_clock::now());
}

}
