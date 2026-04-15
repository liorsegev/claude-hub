#include "agent_manager.hpp"

namespace claude_hub {

int AgentManager::spawn(const std::string& cwd, int cols, int rows) {
	auto agent = std::make_unique<Agent>("agent-" + std::to_string(next_id_++));
	if (!agent->spawn(cwd, cols, rows))
		return -1;

	agents_.push_back(std::move(agent));
	active_ = static_cast<int>(agents_.size()) - 1;
	return active_;
}

void AgentManager::kill(int idx) {
	if (idx < 0 || idx >= count()) return;

	agents_[idx]->kill();
	agents_.erase(agents_.begin() + idx);

	if (agents_.empty())
		active_ = -1;
	else
		active_ = active_ % count();
}

void AgentManager::drain_all() {
	for (auto& a : agents_)
		a->drain_output();
}

void AgentManager::shutdown() {
	for (auto& a : agents_)
		a->kill();
	agents_.clear();
	active_ = -1;
}

Agent* AgentManager::get(int idx) {
	if (idx < 0 || idx >= count()) return nullptr;
	return agents_[idx].get();
}

void AgentManager::set_active(int idx) {
	if (idx >= 0 && idx < count())
		active_ = idx;
}

void AgentManager::cycle_next() {
	if (count() >= 2)
		active_ = (active_ + 1) % count();
}

} // namespace claude_hub
