#pragma once

#include "agent.hpp"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <random>

namespace claude_hub {

/// Owns and manages all Claude agent instances.
class AgentManager {
public:
	AgentManager() = default;

	/// Spawn a new Claude agent. Returns the agent ID, or empty on failure.
	std::string spawn(const std::string& cwd, int cols, int rows);

	/// Get an agent by ID. Returns nullptr if not found.
	Agent* get(const std::string& id);

	/// Kill and remove an agent.
	bool kill(const std::string& id);

	/// Get all agent IDs in insertion order.
	std::vector<std::string> agent_ids() const;

	/// Get all agents.
	std::vector<Agent*> all_agents();

	/// Number of agents.
	size_t count() const;

	/// Kill all agents.
	void shutdown();

private:
	std::string generate_id();

	std::unordered_map<std::string, std::unique_ptr<Agent>> agents_;
	std::vector<std::string> order_; // Insertion order
	mutable std::mutex mutex_;
	std::mt19937 rng_{std::random_device{}()};
};

} // namespace claude_hub
