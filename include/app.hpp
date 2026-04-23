#pragma once

#include "AgentManager.hpp"
#include "D3D11Context.hpp"
#include "ImGuiRuntime.hpp"
#include "Logger.hpp"
#include "MainWindow.hpp"
#include "Sidebar.hpp"

#include <memory>

namespace ch {

// Composition root. Wires together all subsystems and runs the frame loop.
//
// ── Member declaration order is load-bearing ──
// Destruction runs in reverse: imgui_ (uses device + HWND) must be destroyed
// BEFORE d3d_ (owns device) and window_ (owns HWND). Logger_ is first so it
// is destroyed last, letting other destructors log if they need to.
class App : public IWindowHandler {
public:
	App();
	~App() override = default;

	int run();

	// IWindowHandler
	void on_resize(unsigned int w, unsigned int h) override;
	void on_quit() override;

private:
	void frame();
	void apply_sidebar_commands(const SidebarCommands& cmd);

	Logger log_;
	std::unique_ptr<MainWindow> window_;
	std::unique_ptr<D3D11Context> d3d_;
	std::unique_ptr<ImGuiRuntime> imgui_;
	std::unique_ptr<AgentManager> manager_;
	Sidebar sidebar_;
	bool quitting_ = false;
	bool active_hidden_for_modal_ = false;
	int frame_count_ = 0;
};

}
