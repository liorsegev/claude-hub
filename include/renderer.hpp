#pragma once

#include "agent_manager.hpp"

namespace claude_hub {

/// Handles all ImGui rendering: menu bar, main panel, side panels.
class Renderer {
public:
	void render(AgentManager& manager);

private:
	void render_menu_bar(AgentManager& manager);
	void render_main_panel(Agent& agent);
	void render_side_panels(AgentManager& manager);
	void render_terminal(Agent& agent, bool is_active);
};

} // namespace claude_hub
