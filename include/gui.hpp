#pragma once

#include "conpty.hpp"
#include "terminal_buffer.hpp"
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct IDXGISwapChain;
struct ID3D11RenderTargetView;

namespace claude_hub {

struct GuiAgent {
	ConPTY pty;
	TerminalBuffer buffer;
	std::string name;
	std::atomic<bool> alive{false};
	std::atomic<bool> forwarding_input{false};
	std::atomic<std::chrono::steady_clock::time_point> last_output_time{
		std::chrono::steady_clock::now()
	};

	// Lock-free output ring buffer (SPSC: reader writes, renderer reads)
	static constexpr int RING_SIZE = 512 * 1024; // 512KB
	char ring[RING_SIZE];
	std::atomic<int> ring_write{0}; // Written by reader thread
	int ring_read = 0;              // Read by render thread only

	// Display lines (built by render thread from ring buffer, never touched by reader)
	std::vector<std::string> display_lines;

	// Input queue — render thread pushes, writer thread pops
	std::vector<std::string> input_queue;
	std::mutex input_mutex;
	std::thread input_thread;

	// Local echo
	std::string local_echo;

	GuiAgent(int cols, int rows) : buffer(cols, rows) {
		display_lines.push_back("");
		memset(ring, 0, RING_SIZE);
	}
};

class Gui {
public:
	Gui();
	~Gui();

	/// Run the GUI (blocks until window is closed).
	int run();

private:
	bool init_window();
	bool init_d3d();
	void cleanup_d3d();
	void render_frame();

	// Agent management
	void spawn_agent();
	void switch_agent(int idx);
	void kill_agent(int idx);

	// Render a terminal buffer into an ImGui child window
	void render_terminal(const char* title, GuiAgent& agent, bool is_active, bool compact);

public:
	// DirectX state (public for wndproc access)
	HWND hwnd_ = nullptr;
	ID3D11Device* device_ = nullptr;
	ID3D11DeviceContext* context_ = nullptr;
	IDXGISwapChain* swap_chain_ = nullptr;
	ID3D11RenderTargetView* rtv_ = nullptr;
	UINT resize_width_ = 0;
	UINT resize_height_ = 0;
private:

	// Agents
	std::vector<std::unique_ptr<GuiAgent>> agents_;
	int active_ = -1;
	int term_cols_ = 120;
	int term_rows_ = 40;

	// Input buffer for active agent
	char input_buf_[4096] = {};
};

} // namespace claude_hub
