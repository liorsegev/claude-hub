#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "IActivityProbe.hpp"
#include "Logger.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <set>
#include <string>

namespace ch {

// Owns a single Claude CLI session: the embedded WT child window,
// the Claude process handle, and a lazy activity probe.
class Agent {
public:
	Agent(std::string name,
	      HWND wt_window,
	      HANDLE wt_process,
	      unsigned int claude_pid,
	      std::string cwd,
	      std::set<std::string> jsonl_snapshot,
	      std::chrono::steady_clock::time_point spawn_time);

	~Agent();

	Agent(const Agent&) = delete;
	Agent& operator=(const Agent&) = delete;
	Agent(Agent&&) = delete;
	Agent& operator=(Agent&&) = delete;

	// Accessors
	const std::string& name() const { return name_; }
	HWND window() const { return window_; }
	unsigned int claude_pid() const { return claude_pid_; }
	const std::string& cwd() const { return cwd_; }
	const std::set<std::string>& jsonl_snapshot() const { return jsonl_snapshot_; }
	std::chrono::steady_clock::time_point spawn_time() const { return spawn_time_; }
	bool waiting() const { return waiting_; }
	bool has_probe() const { return static_cast<bool>(probe_); }
	const IActivityProbe* probe() const { return probe_.get(); }
	IActivityProbe* probe() { return probe_.get(); }
	const std::filesystem::path& jsonl_path() const { return jsonl_path_; }
	const std::string& title() const { return title_; }
	void set_title(std::string t) { title_ = std::move(t); }

	// Window operations
	void reparent_as_child(HWND parent);
	void show();
	void hide();
	void move_to(int x, int y, int w, int h);
	void focus();

	// Lifecycle
	bool is_alive() const;

	// Attach a JSONL watcher once the conversation file has been discovered.
	void attach_jsonl(std::filesystem::path jsonl_path, Logger* log);

	// Set the computed waiting flag from AgentManager.
	void set_waiting(bool w) { waiting_ = w; }

private:
	std::string name_;
	HWND window_;
	HANDLE process_;
	unsigned int claude_pid_;
	std::string cwd_;
	std::set<std::string> jsonl_snapshot_;
	std::chrono::steady_clock::time_point spawn_time_;

	std::filesystem::path jsonl_path_;
	std::unique_ptr<IActivityProbe> probe_;
	std::string title_;

	bool waiting_ = false;
};

}
