#pragma once

#include <filesystem>
#include <set>
#include <string>

namespace ch {

// Consumes sentinel files written by the Claude-Hub hook script
// (scripts/claude_hub_notify.ps1) to signal that a specific Claude Code
// session has finished its turn and is waiting for user input.
//
// Flag filename convention: "<session_id>.flag".
// session_id == the stem of the conversation JSONL, so AgentManager can
// match flags to Agents via their attached jsonl_path.
class WaitingFlagWatcher {
public:
	explicit WaitingFlagWatcher(std::filesystem::path flag_dir);

	// Enumerate currently-present flag files; returns the set of session_ids.
	std::set<std::string> poll_pending() const;

	// Remove the flag for a specific session, if present. Idempotent.
	void clear(const std::string& session_id);

	const std::filesystem::path& dir() const { return dir_; }

private:
	std::filesystem::path dir_;
};

}
