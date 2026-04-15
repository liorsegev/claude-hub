#include "input_detector.hpp"

namespace claude_hub {

InputDetector::InputDetector() {
	prompt_patterns_ = {
		std::regex(R"(^\s*>\s*$)"),
		std::regex(R"(^\s*\?\s+)"),
		std::regex(R"(\(y/n\))", std::regex::icase),
		std::regex(R"(\(Y/n\))"),
		std::regex(R"(\(N/y\))"),
		std::regex(R"(Enter.*:\s*$)"),
		std::regex(R"(Press Enter)"),
		std::regex(R"(\$ $)"),
	};
}

AgentState InputDetector::detect(const Agent& agent) const {
	if (!agent.is_alive())
		return AgentState::IDLE;

	auto now = std::chrono::steady_clock::now();
	auto elapsed = std::chrono::duration<double>(now - agent.last_output_time()).count();

	// Still producing output — working
	if (elapsed < INPUT_QUIESCENCE_SECONDS)
		return AgentState::WORKING;

	// Check last few lines for prompt patterns
	auto lines = agent.buffer().preview_lines(5);
	for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
		if (it->empty()) continue;
		if (matches_prompt(*it))
			return AgentState::WAITING;
		break; // Only check last non-empty line
	}

	// Long silence — probably waiting
	if (elapsed > 10.0)
		return AgentState::WAITING;

	return AgentState::WORKING;
}

bool InputDetector::matches_prompt(const std::string& line) const {
	for (const auto& pattern : prompt_patterns_) {
		if (std::regex_search(line, pattern))
			return true;
	}
	return false;
}

} // namespace claude_hub
