#include "agent.hpp"
#include "config.hpp"
#include <filesystem>

namespace claude_hub {

Agent::Agent(const std::string& id, const std::string& cwd, int cols, int rows)
	: id_(id)
	, cwd_(cwd)
	, cols_(cols)
	, rows_(rows)
	, buffer_(cols, rows) {
	// Extract display name from cwd
	auto path = std::filesystem::path(cwd);
	display_name_ = path.filename().string();
	if (display_name_.empty())
		display_name_ = "home";
}

Agent::~Agent() {
	kill();
}

bool Agent::start() {
	std::string cmd = claude_exe_path();
	if (!pty_.spawn(cmd, cwd_, cols_, rows_))
		return false;

	// Start reading PTY output — forward to stdout directly, no processing
	pty_.start_reader([this](const char* data, size_t len) {
		last_output_time_ = std::chrono::steady_clock::now();

		std::lock_guard lock(forwarder_mutex_);
		if (output_forwarder_) {
			// Active agent: write straight to stdout, skip VT parsing
			output_forwarder_(data, len);
		}
	});

	return true;
}

void Agent::set_output_forwarder(OutputForwarder forwarder) {
	std::lock_guard lock(forwarder_mutex_);
	output_forwarder_ = std::move(forwarder);
}

void Agent::send_input(const char* data, size_t len) {
	pty_.write(data, len);
}

void Agent::send_input(const std::string& data) {
	pty_.write(data);
}

void Agent::resize(int cols, int rows) {
	cols_ = cols;
	rows_ = rows;
	pty_.resize(cols, rows);
	buffer_.resize(cols, rows);
}

void Agent::kill() {
	pty_.kill();
	state_ = AgentState::IDLE;
}

bool Agent::is_alive() const {
	return pty_.is_alive();
}

} // namespace claude_hub
