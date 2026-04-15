#pragma once

#include "conpty.hpp"
#include "terminal_buffer.hpp"
#include <string>
#include <functional>
#include <memory>
#include <chrono>
#include <atomic>
#include <mutex>

namespace claude_hub {

enum class AgentState {
	WORKING,
	WAITING,
	IDLE,
};

/// Represents a single Claude CLI session with its ConPTY and terminal buffer.
class Agent {
public:
	Agent(const std::string& id, const std::string& cwd, int cols, int rows);
	~Agent();

	Agent(const Agent&) = delete;
	Agent& operator=(const Agent&) = delete;

	/// Start the Claude process.
	bool start();

	/// Send raw input bytes to the ConPTY.
	void send_input(const char* data, size_t len);
	void send_input(const std::string& data);

	/// Resize the terminal.
	void resize(int cols, int rows);

	/// Kill the Claude process.
	void kill();

	/// Check if the process is alive.
	bool is_alive() const;

	// Accessors
	const std::string& id() const { return id_; }
	const std::string& cwd() const { return cwd_; }
	const std::string& display_name() const { return display_name_; }
	TerminalBuffer& buffer() { return buffer_; }
	const TerminalBuffer& buffer() const { return buffer_; }
	AgentState state() const { return state_.load(); }
	void set_state(AgentState s) { state_ = s; }

	/// Time of last output from the ConPTY.
	std::chrono::steady_clock::time_point last_output_time() const {
		return last_output_time_.load();
	}

	/// Set a callback that receives raw ConPTY output directly (for active agent).
	/// Pass nullptr to disable forwarding.
	using OutputForwarder = std::function<void(const char*, size_t)>;
	void set_output_forwarder(OutputForwarder forwarder);

private:
	std::string id_;
	std::string cwd_;
	std::string display_name_;
	int cols_;
	int rows_;
	ConPTY pty_;
	TerminalBuffer buffer_;
	std::atomic<AgentState> state_{AgentState::WORKING};
	std::atomic<std::chrono::steady_clock::time_point> last_output_time_{
		std::chrono::steady_clock::now()
	};
	OutputForwarder output_forwarder_;
	std::mutex forwarder_mutex_;
};

} // namespace claude_hub
