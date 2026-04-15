#pragma once

#include "conpty.hpp"
#include <string>
#include <vector>
#include <atomic>
#include <cstring>

namespace claude_hub {

/// A single Claude CLI session with ConPTY and lock-free output ring buffer.
class Agent {
public:
	static constexpr int RING_SIZE = 512 * 1024;
	static constexpr int MAX_LINES = 500;
	static constexpr int DRAIN_BUDGET = 1024;

	explicit Agent(const std::string& name);

	Agent(const Agent&) = delete;
	Agent& operator=(const Agent&) = delete;

	/// Spawn the Claude CLI process in the given working directory.
	bool spawn(const std::string& cwd, int cols, int rows);

	/// Send raw bytes to the ConPTY (keyboard input).
	void send_input(const char* data, size_t len);
	void send_input(const std::string& data);

	/// Drain pending output from the ring buffer into display lines.
	/// Called once per frame on the render thread. Budget-capped to avoid stalls.
	void drain_output();

	/// Kill the underlying process.
	void kill();

	// Accessors
	const std::string& name() const { return name_; }
	bool alive() const { return alive_; }
	const std::vector<std::string>& lines() const { return lines_; }
	const std::string& local_echo() const { return local_echo_; }

	void append_echo(char c) { local_echo_ += c; }
	void append_echo(const std::string& s) { local_echo_ += s; }
	void backspace_echo();
	void clear_echo() { local_echo_.clear(); }

private:
	std::string name_;
	ConPTY pty_;

	// Lock-free SPSC ring buffer (reader thread writes, render thread reads)
	char ring_[RING_SIZE];
	std::atomic<int> ring_write_{0};
	int ring_read_ = 0;

	// Display state (render thread only)
	std::vector<std::string> lines_ = {""};
	std::string local_echo_;
	std::atomic<bool> alive_{false};
};

} // namespace claude_hub
