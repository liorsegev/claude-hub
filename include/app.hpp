#pragma once

#include "agent_manager.hpp"
#include "input_detector.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// FTXUI included only for header compatibility (stubs)
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

#include <string>
#include <vector>
#include <atomic>

namespace claude_hub {

/// Main application — direct console I/O, no framework overhead.
class App {
public:
	App();
	~App();

	void run();

private:
	void handle_key_event(const KEY_EVENT_RECORD& key);
	void spawn_agent();
	void switch_to_agent(const std::string& id);
	void kill_active_agent();
	void next_agent();
	void detection_loop();

	// FTXUI stubs (unused — kept for link compatibility)
	ftxui::Component build_ui();
	ftxui::Element render_main_panel();
	ftxui::Element render_side_panel(Agent*);
	ftxui::Element render_status_bar();
	ftxui::Element render_side_bar();
	ftxui::Element cells_to_element(Agent*);
	ftxui::Element render_row(const std::vector<Cell>&, int);
	static ftxui::Color cell_fg_color(const Cell&);
	static ftxui::Color cell_bg_color(const Cell&);
	std::vector<Agent*> get_side_agents();
	bool handle_key(ftxui::Event);
	bool handle_spawn_dialog_key(ftxui::Event);

	AgentManager manager_;
	InputDetector detector_;
	ftxui::ScreenInteractive screen_; // Unused, kept for header compat
	std::string active_agent_id_;
	std::atomic<bool> running_{true};
	std::string spawn_cwd_;
};

} // namespace claude_hub
