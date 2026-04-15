#include "agent_manager.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace claude_hub {

std::string AgentManager::spawn(const std::string& cwd, int cols, int rows) {
	std::lock_guard lock(mutex_);

	auto id = generate_id();
	auto agent = std::make_unique<Agent>(id, cwd, cols, rows);

	if (!agent->start())
		return "";

	agents_[id] = std::move(agent);
	order_.push_back(id);
	return id;
}

Agent* AgentManager::get(const std::string& id) {
	std::lock_guard lock(mutex_);
	auto it = agents_.find(id);
	return it != agents_.end() ? it->second.get() : nullptr;
}

bool AgentManager::kill(const std::string& id) {
	std::lock_guard lock(mutex_);
	auto it = agents_.find(id);
	if (it == agents_.end()) return false;

	it->second->kill();
	agents_.erase(it);
	order_.erase(std::remove(order_.begin(), order_.end(), id), order_.end());
	return true;
}

std::vector<std::string> AgentManager::agent_ids() const {
	std::lock_guard lock(mutex_);
	return order_;
}

std::vector<Agent*> AgentManager::all_agents() {
	std::lock_guard lock(mutex_);
	std::vector<Agent*> result;
	for (const auto& id : order_) {
		auto it = agents_.find(id);
		if (it != agents_.end())
			result.push_back(it->second.get());
	}
	return result;
}

size_t AgentManager::count() const {
	std::lock_guard lock(mutex_);
	return agents_.size();
}

void AgentManager::shutdown() {
	std::lock_guard lock(mutex_);
	for (auto& [id, agent] : agents_)
		agent->kill();
	agents_.clear();
	order_.clear();
}

std::string AgentManager::generate_id() {
	std::uniform_int_distribution<> dist(0, 15);
	const char* hex = "0123456789abcdef";
	std::string id;
	id.reserve(8);
	for (int i = 0; i < 8; ++i)
		id += hex[dist(rng_)];
	return id;
}

} // namespace claude_hub
