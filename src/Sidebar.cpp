#include "Sidebar.hpp"
#include "Constants.hpp"
#include "IActivityProbe.hpp"

#include <imgui.h>
#include <string>

namespace ch {

namespace {

constexpr int PREVIEW_HEIGHT_PX = 90;
constexpr size_t PREVIEW_MAX_CHARS = 240;

ImVec4 color_for(bool active, bool waiting, bool blink_on) {
	constexpr ImVec4 ACTIVE{0.4f, 1.0f, 0.4f, 1.0f};
	constexpr ImVec4 WAIT_BRIGHT{1.0f, 1.0f, 0.2f, 1.0f};
	constexpr ImVec4 WAIT_DIM{0.7f, 0.7f, 0.1f, 1.0f};
	constexpr ImVec4 IDLE{1.0f, 1.0f, 1.0f, 1.0f};
	if (active) return ACTIVE;
	if (waiting) return blink_on ? WAIT_BRIGHT : WAIT_DIM;
	return IDLE;
}

std::string truncate_preview(const std::string& text) {
	if (text.size() <= PREVIEW_MAX_CHARS) return text;
	return text.substr(0, PREVIEW_MAX_CHARS) + "\xE2\x80\xA6";  // ellipsis
}

std::string display_name(const Agent& a) {
	if (!a.title().empty()) return a.title();
	if (a.has_probe()) {
		const std::string& title = a.probe()->conversation_title();
		if (!title.empty()) return title;
	}
	return a.name();
}

std::string short_label(const std::string& name, bool is_active, bool is_waiting) {
	std::string label = (is_active ? "* " : "  ") + name.substr(0, constants::LABEL_NAME_MAX);
	if (is_waiting && !is_active) label += " [WAITING]";
	return label;
}

}

SidebarCommands Sidebar::draw(const AgentManager& manager, int client_w, int client_h) {
	SidebarCommands cmd;

	ImGui::SetNextWindowPos(ImVec2(static_cast<float>(client_w - constants::SIDEBAR_WIDTH_PX), 0));
	ImGui::SetNextWindowSize(ImVec2(static_cast<float>(constants::SIDEBAR_WIDTH_PX),
		static_cast<float>(client_h)));
	ImGui::Begin("##sidebar", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

	if (ImGui::Button("+ New Agent", ImVec2(-1, constants::BUTTON_HEIGHT_PX)))
		cmd.spawn_requested = true;
	if (ImGui::Button("Kill Active", ImVec2(-1, constants::BUTTON_HEIGHT_PX)))
		cmd.kill_active_requested = true;
	ImGui::Separator();
	ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Yellow = waiting for input");
	ImGui::Separator();

	const bool blink_on = (GetTickCount64() / constants::BLINK_PERIOD_MS) % 2 == 0;
	const int active_idx = manager.active_index();
	const auto& agents = manager.agents();

	for (int i = 0; i < static_cast<int>(agents.size()); ++i) {
		const Agent& a = *agents[i];
		const bool is_active = (i == active_idx);
		ImGui::PushID(i);

		const ImVec2 row_start = ImGui::GetCursorScreenPos();
		const float avail_w = ImGui::GetContentRegionAvail().x;

		const ImVec4 col = color_for(is_active, a.waiting(), blink_on);
		ImGui::PushStyleColor(ImGuiCol_Text, col);
		const std::string label = short_label(display_name(a), is_active, a.waiting());
		if (ImGui::Selectable(label.c_str(), is_active)) cmd.switch_to_index = i;
		ImGui::PopStyleColor();

		if (a.has_probe()) {
			const IActivityProbe& p = *a.probe();
			ImGui::Indent();
			ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1), "in=%d out=%d last=%s",
				p.input_tokens(), p.output_tokens(), p.last_entry_type().c_str());
			ImGui::Unindent();

			if (!p.last_assistant_text().empty()) {
				const ImVec4 bg = a.waiting()
					? ImVec4(0.18f, 0.18f, 0.08f, 1)
					: ImVec4(0.12f, 0.12f, 0.14f, 1);
				ImGui::Indent();
				ImGui::PushStyleColor(ImGuiCol_ChildBg, bg);
				ImGui::BeginChild("preview",
					ImVec2(0, static_cast<float>(PREVIEW_HEIGHT_PX)), true,
					ImGuiWindowFlags_NoScrollbar);
				ImGui::TextWrapped("%s", truncate_preview(p.last_assistant_text()).c_str());
				ImGui::EndChild();
				ImGui::PopStyleColor();
				ImGui::Unindent();
			}
		}

		ImGui::SameLine();
		if (ImGui::SmallButton("X")) cmd.kill_index = i;

		const ImVec2 row_end = ImGui::GetCursorScreenPos();
		if (a.waiting()) {
			const ImU32 border = blink_on
				? IM_COL32(255, 255, 60, 255)
				: IM_COL32(180, 180, 20, 255);
			ImGui::GetWindowDrawList()->AddRect(
				ImVec2(row_start.x - 2, row_start.y - 2),
				ImVec2(row_start.x + avail_w + 2, row_end.y - 2),
				border, 4.0f, 0, 2.5f);
		}

		ImGui::PopID();
	}

	ImGui::End();
	return cmd;
}

}
