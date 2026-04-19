#include "WaitingFlagWatcher.hpp"

#include <system_error>

namespace ch {

namespace fs = std::filesystem;

WaitingFlagWatcher::WaitingFlagWatcher(fs::path flag_dir)
	: dir_(std::move(flag_dir)) {
	std::error_code ec;
	fs::create_directories(dir_, ec);
}

std::set<std::string> WaitingFlagWatcher::poll_pending() const {
	std::set<std::string> pending;
	std::error_code ec;
	if (!fs::exists(dir_, ec)) return pending;

	for (const auto& e : fs::directory_iterator(dir_, ec)) {
		if (ec) break;
		if (e.path().extension() != ".flag") continue;
		pending.insert(e.path().stem().string());
	}
	return pending;
}

void WaitingFlagWatcher::clear(const std::string& session_id) {
	std::error_code ec;
	fs::remove(dir_ / (session_id + ".flag"), ec);
}

}
