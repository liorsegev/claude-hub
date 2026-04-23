#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "AgentManager.hpp"
#include "SpawnConfig.hpp"

#include <optional>
#include <string>

namespace ch {

// Commands returned by the sidebar so App can decide how to react without
// Sidebar holding a mutable reference to AgentManager during draw().
struct SidebarCommands {
	std::optional<SpawnConfig> spawn_requested;
	bool kill_active_requested = false;
	int switch_to_index = -1;
	int kill_index = -1;
	// True while the New-Agent modal is on screen. App uses this to hide the
	// active terminal child HWND, which would otherwise z-order above the modal.
	bool new_agent_modal_open = false;
};

class Sidebar {
public:
	// Render the sidebar aligned to the right of the container client rect.
	// Returns user-initiated commands for the frame. `owner` is the main
	// window HWND; used as parent for the folder-picker common dialog.
	SidebarCommands draw(const AgentManager& manager,
	                     HWND owner,
	                     int client_w, int client_h);

private:
	// New-Agent modal state. Persists across frames while the popup is open.
	bool         new_agent_pending_open_ = false;
	AgentKind    new_agent_kind_ = AgentKind::Claude;
	std::string  new_agent_cwd_;  // utf-8; edited via InputText
};

}
