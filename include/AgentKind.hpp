#pragma once

namespace ch {

enum class AgentKind {
	Claude,
	Copilot,
	Gemini,
};

inline const char* to_string(AgentKind k) {
	switch (k) {
		case AgentKind::Claude:  return "claude";
		case AgentKind::Copilot: return "copilot";
		case AgentKind::Gemini:  return "gemini";
	}
	return "unknown";
}

}
