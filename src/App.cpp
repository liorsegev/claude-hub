#include "App.hpp"
#include "Constants.hpp"

namespace ch {

namespace {
constexpr const wchar_t* WINDOW_TITLE = L"Claude-Hub";
constexpr const char* LOG_PATH = "C:\\Users\\liors\\claude-hub\\debug.log";
constexpr float CLEAR_COLOR[4] = {0.1f, 0.1f, 0.12f, 1.0f};
}

App::App()
	: log_(LOG_PATH)
	, sidebar_() {
	log_.logf("=== claude-hub started ===\n");

	window_ = std::make_unique<MainWindow>(
		WINDOW_TITLE,
		constants::WINDOW_INIT_W,
		constants::WINDOW_INIT_H,
		*this);
	window_->show();

	d3d_ = std::make_unique<D3D11Context>(window_->hwnd());
	imgui_ = std::make_unique<ImGuiRuntime>(
		window_->hwnd(), d3d_->device(), d3d_->context());

	manager_ = std::make_unique<AgentManager>(window_->hwnd(), log_);

	manager_->spawn();
}

int App::run() {
	MSG msg{};
	while (!quitting_ && msg.message != WM_QUIT) {
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			continue;
		}
		frame();
	}
	return 0;
}

void App::on_resize(unsigned int w, unsigned int h) {
	if (d3d_) d3d_->queue_resize(w, h);
	if (manager_) manager_->reposition_active();
}

void App::on_quit() {
	quitting_ = true;
}

void App::frame() {
	d3d_->apply_pending_resize();

	if (++frame_count_ % constants::TICK_EVERY_N_FRAMES == 0 && manager_)
		manager_->tick();

	imgui_->begin_frame();

	const RECT cr = window_->client_rect();
	const SidebarCommands cmd = sidebar_.draw(*manager_, cr.right, cr.bottom);
	apply_sidebar_commands(cmd);

	d3d_->clear(CLEAR_COLOR);
	imgui_->render_draw_data();
	d3d_->present();
}

void App::apply_sidebar_commands(const SidebarCommands& cmd) {
	if (cmd.spawn_requested) manager_->spawn();
	if (cmd.kill_active_requested && manager_->active_index() >= 0)
		manager_->kill(manager_->active_index());
	if (cmd.kill_index >= 0) manager_->kill(cmd.kill_index);
	else if (cmd.switch_to_index >= 0) manager_->switch_to(cmd.switch_to_index);
}

}
