#include "renderer.hpp"
#include <imgui.h>
#include <algorithm>

namespace claude_hub {

void Renderer::render(AgentManager& manager) {
	ImGuiViewport* vp = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(vp->WorkPos);
	ImGui::SetNextWindowSize(vp->WorkSize);
	ImGui::Begin("##Main", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_MenuBar);

	render_menu_bar(manager);

	if (manager.count() == 0) {
		ImGui::Text("No agents. Press Ctrl+N to start.");
	} else {
		ImVec2 avail = ImGui::GetContentRegionAvail();
		float main_w = avail.x * 0.7f;
		float side_w = avail.x * 0.3f - 8;

		ImGui::BeginChild("MainPanel", ImVec2(main_w, avail.y), true);
		Agent* active = manager.get(manager.active());
		if (active) {
			ImGui::TextColored(ImVec4(0.3f, 1, 0.3f, 1), "Active: %s", active->name().c_str());
			ImGui::Separator();
			render_terminal(*active, true);
		}
		ImGui::EndChild();

		ImGui::SameLine();

		ImGui::BeginChild("SidePanels", ImVec2(side_w, avail.y), false);
		render_side_panels(manager);
		ImGui::EndChild();
	}

	ImGui::End();
}

void Renderer::render_menu_bar(AgentManager& manager) {
	if (!ImGui::BeginMenuBar()) return;

	if (ImGui::MenuItem("+ New (Ctrl+N)")) {
		const char* home = std::getenv("USERPROFILE");
		std::string cwd = home ? home : "C:\\Users\\liors";
		manager.spawn(cwd, 100, 40);
	}
	ImGui::Separator();

	for (int i = 0; i < manager.count(); ++i) {
		Agent* a = manager.get(i);
		bool selected = (i == manager.active());
		std::string label = a->name();
		if (selected) label = "* " + label;
		if (ImGui::MenuItem(label.c_str(), nullptr, selected))
			manager.set_active(i);
	}

	ImGui::EndMenuBar();
}

void Renderer::render_main_panel(Agent& agent) {
	render_terminal(agent, true);
}

void Renderer::render_side_panels(AgentManager& manager) {
	int side_count = 0;
	for (int i = 0; i < manager.count(); ++i)
		if (i != manager.active()) side_count++;

	float panel_h = side_count > 0
		? (ImGui::GetContentRegionAvail().y / side_count)
		: ImGui::GetContentRegionAvail().y;

	for (int i = 0; i < manager.count(); ++i) {
		if (i == manager.active()) continue;
		Agent& a = *manager.get(i);

		std::string id = "##side" + std::to_string(i);
		ImGui::BeginChild(id.c_str(), ImVec2(-1, panel_h - 2), true);

		if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0))
			manager.set_active(i);

		ImGui::TextColored(ImVec4(0.4f, 0.8f, 1, 1), "%s", a.name().c_str());
		ImGui::Separator();

		// Preview: last 8 lines
		const auto& lines = a.lines();
		int start = std::max(0, static_cast<int>(lines.size()) - 8);
		for (int r = start; r < static_cast<int>(lines.size()); ++r)
			ImGui::TextUnformatted(lines[r].c_str(), lines[r].c_str() + lines[r].size());

		ImGui::EndChild();
	}
}

void Renderer::render_terminal(Agent& agent, bool is_active) {
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.06f, 0.08f, 1));
	ImGui::BeginChild("##term", ImVec2(-1, -1), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);

	const auto& lines = agent.lines();
	ImGuiListClipper clip;
	clip.Begin(static_cast<int>(lines.size()));
	while (clip.Step()) {
		for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r)
			ImGui::TextUnformatted(lines[r].c_str(), lines[r].c_str() + lines[r].size());
	}
	clip.End();

	if (is_active && !agent.local_echo().empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 1, 0.5f, 1));
		std::string echo = "> " + agent.local_echo() + "_";
		ImGui::TextUnformatted(echo.c_str());
		ImGui::PopStyleColor();
	}

	if (is_active && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20)
		ImGui::SetScrollHereY(1.0f);

	ImGui::EndChild();
	ImGui::PopStyleColor();
}

} // namespace claude_hub
