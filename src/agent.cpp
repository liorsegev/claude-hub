#include "agent.hpp"
#include "config.hpp"

namespace claude_hub {

Agent::Agent(const std::string& name) : name_(name) {
	memset(ring_, 0, RING_SIZE);
}

bool Agent::spawn(const std::string& cwd, int cols, int rows) {
	std::string cmd = claude_exe_path();
	if (!pty_.spawn(cmd, cwd, cols, rows))
		return false;

	alive_ = true;

	// Reader thread: dump raw bytes into ring buffer (zero allocations)
	pty_.start_reader([this](const char* data, size_t len) {
		int w = ring_write_.load(std::memory_order_relaxed);
		for (size_t i = 0; i < len; ++i)
			ring_[w++ % RING_SIZE] = data[i];
		ring_write_.store(w, std::memory_order_release);
	});

	return true;
}

void Agent::send_input(const char* data, size_t len) {
	pty_.write(data, len);
}

void Agent::send_input(const std::string& data) {
	pty_.write(data);
}

void Agent::drain_output() {
	int rw = ring_write_.load(std::memory_order_acquire);
	int budget = DRAIN_BUDGET;

	while (ring_read_ < rw && budget > 0) {
		char c = ring_[ring_read_ % RING_SIZE];
		ring_read_++;
		budget--;

		if (c == '\n') {
			lines_.push_back("");
			while (lines_.size() > MAX_LINES)
				lines_.erase(lines_.begin());
		} else if (c == '\r') {
			// Carriage return — ignore
		} else if (c == '\x1b') {
			// Skip ANSI escape sequences
			if (ring_read_ < rw && ring_[ring_read_ % RING_SIZE] == '[') {
				ring_read_++;
				budget--;
				while (ring_read_ < rw) {
					char sc = ring_[ring_read_ % RING_SIZE];
					ring_read_++;
					budget--;
					if (sc >= 0x40) break;
				}
			} else if (ring_read_ < rw) {
				ring_read_++;
				budget--;
			}
		} else if (c >= 0x20 || static_cast<unsigned char>(c) >= 0x80) {
			lines_.back() += c;
		}
	}
}

void Agent::backspace_echo() {
	if (!local_echo_.empty())
		local_echo_.pop_back();
}

void Agent::kill() {
	pty_.kill();
	alive_ = false;
}

} // namespace claude_hub
