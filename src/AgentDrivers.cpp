#include "IAgentDriver.hpp"
#include "ClaudeSessionDiscovery.hpp"
#include "Constants.hpp"

namespace ch {

namespace {

class ClaudeDriver : public IAgentDriver {
public:
	AgentKind   kind() const override        { return AgentKind::Claude; }
	const char* name_prefix() const override { return "claude"; }
	bool uses_claude_telemetry() const override { return true; }

	std::string build_command() const override {
		return ClaudeSessionDiscovery::claude_exe_path().string();
	}
};

class CopilotDriver : public IAgentDriver {
public:
	AgentKind   kind() const override        { return AgentKind::Copilot; }
	const char* name_prefix() const override { return "copilot"; }
	bool uses_claude_telemetry() const override { return false; }

	std::string build_command() const override {
		return constants::COPILOT_COMMAND;
	}
};

class GeminiDriver : public IAgentDriver {
public:
	AgentKind   kind() const override        { return AgentKind::Gemini; }
	const char* name_prefix() const override { return "gemini"; }
	bool uses_claude_telemetry() const override { return false; }

	std::string build_command() const override {
		return constants::GEMINI_COMMAND;
	}
};

}

IAgentDriver& get_driver(AgentKind kind) {
	static ClaudeDriver  claude;
	static CopilotDriver copilot;
	static GeminiDriver  gemini;
	switch (kind) {
		case AgentKind::Claude:  return claude;
		case AgentKind::Copilot: return copilot;
		case AgentKind::Gemini:  return gemini;
	}
	return claude;
}

}
