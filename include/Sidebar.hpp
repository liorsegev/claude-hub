#pragma once

#include "AgentManager.hpp"

namespace ch {

// Commands returned by the sidebar so App can decide how to react without
// Sidebar holding a mutable reference to AgentManager during draw().
struct SidebarCommands {
	bool spawn_requested = false;
	bool kill_active_requested = false;
	int switch_to_index = -1;
	int kill_index = -1;
};

class Sidebar {
public:
	// Render the sidebar aligned to the right of the container client rect.
	// Returns user-initiated commands for the frame.
	SidebarCommands draw(const AgentManager& manager, int client_w, int client_h);
};

}
