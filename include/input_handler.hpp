#pragma once

#include "agent_manager.hpp"

namespace claude_hub {

/// Handles keyboard input: shortcuts and forwarding to active agent.
class InputHandler {
public:
	/// Process all keyboard input for this frame.
	/// Returns true if a spawn was requested (for the app to handle).
	bool process(AgentManager& manager);
};

} // namespace claude_hub
