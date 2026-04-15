#pragma once

#include "agent.hpp"
#include "config.hpp"
#include <chrono>
#include <regex>
#include <vector>
#include <string>

namespace claude_hub {

/// Detects whether a Claude agent is waiting for user input.
class InputDetector {
public:
	InputDetector();

	/// Detect the state of an agent based on output patterns and timing.
	AgentState detect(const Agent& agent) const;

private:
	bool matches_prompt(const std::string& line) const;

	std::vector<std::regex> prompt_patterns_;
};

} // namespace claude_hub
