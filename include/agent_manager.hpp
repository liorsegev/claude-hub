#pragma once

#include "agent.hpp"
#include <vector>
#include <memory>
#include <string>

namespace claude_hub {

/// Manages the lifecycle of all Claude agents.
class AgentManager {
public:
	/// Spawn a new agent. Returns the index, or -1 on failure.
	int spawn(const std::string& cwd, int cols, int rows);

	/// Kill and remove an agent by index.
	void kill(int idx);

	/// Drain output for all agents (call once per frame).
	void drain_all();

	/// Shutdown all agents.
	void shutdown();

	Agent* get(int idx);
	int count() const { return static_cast<int>(agents_.size()); }
	int active() const { return active_; }
	void set_active(int idx);
	void cycle_next();

private:
	std::vector<std::unique_ptr<Agent>> agents_;
	int active_ = -1;
	int next_id_ = 0;
};

} // namespace claude_hub
