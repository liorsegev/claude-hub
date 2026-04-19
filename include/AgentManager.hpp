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
#include "WaitingFlagWatcher.hpp"

#include <memory>
#include <vector>

namespace ch {

// Owns all agents and the operations that span them: spawning, killing,
// switching, ticking, repositioning.
class AgentManager {
public:
	AgentManager(HWND container_window, Logger& log);

	void spawn();
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
	void reap_dead();
	void discover_jsonls();
	void sync_pid_state();
	void update_waiting();

	HWND container_;
	Logger& log_;
	WaitingFlagWatcher flag_watcher_;
	std::vector<std::unique_ptr<Agent>> agents_;
	int active_ = -1;
	int next_id_ = 0;
};

}
