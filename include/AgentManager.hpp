#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "Agent.hpp"
#include "Logger.hpp"
#include "SpawnConfig.hpp"
#include "WaitingFlagWatcher.hpp"

#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace ch {

// Owns all agents and the operations that span them: spawning, killing,
// switching, ticking, repositioning.
class AgentManager {
public:
	AgentManager(HWND container_window, Logger& log);

	void spawn(const SpawnConfig& cfg);
	void kill(int index);
	void switch_to(int index);

	// Called periodically from the frame loop:
	//   - reap dead agents
	//   - FIFO-match newly-created JSONLs to unclaimed agents
	//   - poll each agent's activity probe
	//   - recompute waiting state
	void tick();

	// Reposition the currently-visible agent to fill the container.
	void reposition_active();

	int active_index() const { return active_; }
	const std::vector<std::unique_ptr<Agent>>& agents() const { return agents_; }
	size_t size() const { return agents_.size(); }

private:
	// Two-stage background spawn: the window stage finishes quickly (wt
	// spawn + window lookup) and lets the UI dock the terminal immediately;
	// the probe stage finishes later (pid.json claim + jsonl snapshot) and
	// populates the Agent's claude_pid / cwd for waiting detection.
	struct WindowStage {
		std::string unique_name;
		HWND window = nullptr;
		HANDLE process_handle = nullptr;
	};
	struct ProbeStage {
		unsigned int claude_pid = 0;
		std::string cwd;
		std::string session_id;  // from pid.json; used to attach the JSONL directly
		std::set<std::string> jsonl_snapshot;
	};
	struct PendingSpawn {
		AgentKind kind = AgentKind::Claude;
		std::string cwd_hint;  // cwd passed to wt.exe; used when no pid.json
		std::chrono::steady_clock::time_point spawn_time;
		std::future<WindowStage> window_future;
		std::future<ProbeStage> probe_future;
		int agent_index = -1;  // filled after window stage is committed
	};

	void reap_dead();
	void discover_jsonls();
	void sync_pid_state();
	void update_waiting();
	void poll_pending_spawns();
	int commit_window_stage(WindowStage w,
	                        AgentKind kind,
	                        const std::string& cwd_hint,
	                        std::chrono::steady_clock::time_point spawn_time);
	void commit_probe_stage(int agent_index, ProbeStage p);

	HWND container_;
	Logger& log_;
	WaitingFlagWatcher flag_watcher_;
	std::vector<std::unique_ptr<Agent>> agents_;
	std::vector<PendingSpawn> pending_spawns_;
	// Serializes the pid.json claim step across concurrent async spawns, and
	// guards `claimed_pids_` which the workers read/write.
	std::mutex claim_mutex_;
	std::set<unsigned int> claimed_pids_;
	int active_ = -1;
	int next_id_ = 0;
};

}
