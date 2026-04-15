#include "input_handler.hpp"
#include <imgui.h>

namespace claude_hub {

bool InputHandler::process(AgentManager& manager) {
	ImGuiIO& io = ImGui::GetIO();
	bool ctrl = io.KeyCtrl;
	bool spawn_requested = false;

	// Shortcuts
	if (ctrl && ImGui::IsKeyPressed(ImGuiKey_N, false))
		spawn_requested = true;

	if (ctrl && ImGui::IsKeyPressed(ImGuiKey_RightBracket, false))
		manager.cycle_next();

	if (ctrl && ImGui::IsKeyPressed(ImGuiKey_W, false) && manager.active() >= 0)
		manager.kill(manager.active());

	// Ctrl+C → interrupt active agent
	if (ctrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
		Agent* a = manager.get(manager.active());
		if (a) {
			a->send_input("\x03", 1);
			a->clear_echo();
		}
		return spawn_requested;
	}

	// Forward all other input to active agent
	Agent* active = manager.get(manager.active());
	if (!active || ctrl || ImGui::IsAnyItemActive())
		return spawn_requested;

	// Typed characters
	for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
		ImWchar ch = io.InputQueueCharacters[i];
		if (ch < 0x80) {
			char c = static_cast<char>(ch);
			active->send_input(&c, 1);
			active->append_echo(c);
		}
	}
	io.InputQueueCharacters.resize(0);

	// Special keys → VT sequences
	struct KeyMapping { ImGuiKey key; const char* seq; bool repeat; };
	static const KeyMapping mappings[] = {
		{ImGuiKey_Enter,      "\r",      true},
		{ImGuiKey_Backspace,  "\x7f",    true},
		{ImGuiKey_Tab,        "\t",      true},
		{ImGuiKey_UpArrow,    "\x1b[A",  true},
		{ImGuiKey_DownArrow,  "\x1b[B",  true},
		{ImGuiKey_RightArrow, "\x1b[C",  true},
		{ImGuiKey_LeftArrow,  "\x1b[D",  true},
		{ImGuiKey_Escape,     "\x1b",    false},
	};

	for (const auto& m : mappings) {
		if (ImGui::IsKeyPressed(m.key, m.repeat))
			active->send_input(m.seq);
	}

	// Echo management for Enter/Backspace
	if (ImGui::IsKeyPressed(ImGuiKey_Enter, false))
		active->clear_echo();
	if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true))
		active->backspace_echo();

	return spawn_requested;
}

} // namespace claude_hub
