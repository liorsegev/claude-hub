#include "App.hpp"
#include "Constants.hpp"

#include <imgui.h>

namespace ch {

namespace {

constexpr float CLEAR_COLOR[4] = {0.1f, 0.1f, 0.12f, 1.0f};

// Renders a centered "Spawning agent…" placeholder over the agent area while
// at least one spawn is in flight. Lives in App rather than Sidebar so it can
// cover the full agent region (Sidebar's draw is constrained to its column).
void draw_spawning_overlay(int agent_area_w, int client_h) {
	if (agent_area_w <= 0 || client_h <= 0) return;

	ImGui::SetNextWindowPos(ImVec2(0, 0));
	ImGui::SetNextWindowSize(ImVec2(static_cast<float>(agent_area_w),
	                                 static_cast<float>(client_h)));
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.12f, 0.85f));
	ImGui::Begin("##spawning_overlay", nullptr,
		ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav);

	const char* msg = "Spawning agent...";
	const ImVec2 ts = ImGui::CalcTextSize(msg);
	ImGui::SetCursorPos(ImVec2(
		(agent_area_w - ts.x) * 0.5f,
		(client_h     - ts.y) * 0.5f));
	ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "%s", msg);

	ImGui::End();
	ImGui::PopStyleColor();
}

}

App::App() : log_("debug.log") {}

int App::run() {
	window_ = std::make_unique<MainWindow>(
		L"Claude-Hub",
		constants::WINDOW_INIT_W,
		constants::WINDOW_INIT_H,
		*this);
	d3d_    = std::make_unique<D3D11Context>(window_->hwnd());
	imgui_  = std::make_unique<ImGuiRuntime>(
		window_->hwnd(), d3d_->device(), d3d_->context());
	manager_ = std::make_unique<AgentManager>(window_->hwnd(), log_);

	window_->show();

	MSG msg{};
	while (!quitting_) {
		while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) quitting_ = true;
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		if (quitting_) break;
		frame();
	}

	return 0;
}

void App::on_resize(unsigned int w, unsigned int h) {
	if (d3d_)     d3d_->queue_resize(w, h);
	if (manager_) manager_->reposition_active();
}

void App::on_quit() {
	quitting_ = true;
}

void App::frame() {
	d3d_->apply_pending_resize();

	// Spawn polling is cheap (two future::wait_for(0) per pending spawn) and
	// is what gates how quickly a freshly-created terminal docks into the UI;
	// run it every frame so we don't add up to TICK_EVERY_N_FRAMES of latency.
	manager_->poll_spawns();

	if (++frame_count_ % constants::TICK_EVERY_N_FRAMES == 0)
		manager_->tick();

	imgui_->begin_frame();

	const RECT cr = window_->client_rect();
	const SidebarCommands cmd = sidebar_.draw(*manager_, window_->hwnd(), cr.right, cr.bottom);
	apply_sidebar_commands(cmd);

	if (manager_->pending_spawn_count() > 0)
		draw_spawning_overlay(cr.right - constants::SIDEBAR_WIDTH_PX, cr.bottom);

	d3d_->clear(CLEAR_COLOR);
	imgui_->render_draw_data();
	d3d_->present();
}

void App::apply_sidebar_commands(const SidebarCommands& cmd) {
	// Terminal child HWNDs z-order above the D3D11 surface that ImGui paints
	// into, so any ImGui modal would render beneath them. While the New-Agent
	// modal is open, hide the active agent; reveal it on close.
	if (cmd.new_agent_modal_open != active_hidden_for_modal_) {
		active_hidden_for_modal_ = cmd.new_agent_modal_open;
		const int idx = manager_->active_index();
		if (idx >= 0) {
			Agent& a = *manager_->agents()[idx];
			if (active_hidden_for_modal_) {
				a.hide();
			} else {
				manager_->reposition_active();
				a.show();
			}
		}
	}

	if (cmd.spawn_requested)         manager_->spawn(*cmd.spawn_requested);
	if (cmd.kill_active_requested)   manager_->kill(manager_->active_index());
	if (cmd.switch_to_index >= 0)    manager_->switch_to(cmd.switch_to_index);
	if (cmd.kill_index >= 0)         manager_->kill(cmd.kill_index);
}

}
