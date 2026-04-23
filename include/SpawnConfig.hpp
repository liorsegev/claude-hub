#pragma once

#include "AgentKind.hpp"

#include <filesystem>

namespace ch {

// User-chosen parameters for a single New-Agent spawn request. Flows from
// Sidebar (dialog) -> App -> AgentManager::spawn.
struct SpawnConfig {
	AgentKind kind = AgentKind::Claude;
	// Working directory passed to wt.exe via -d. Empty => inherit claude-hub's cwd.
	std::filesystem::path cwd;
};

}
