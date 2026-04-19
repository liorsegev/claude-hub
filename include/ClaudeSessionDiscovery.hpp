#pragma once

#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ch {

// Stateless helpers for locating Claude's session files and conversation JSONLs.
class ClaudeSessionDiscovery {
public:
	struct PidJsonEntry {
		std::string session_id;
		std::string cwd;
		std::string name;  // set by Claude Code's /rename, preserved across /resume
		unsigned int pid = 0;
	};

	ClaudeSessionDiscovery() = delete;

	// User home directory (%USERPROFILE%, with a fallback).
	static std::filesystem::path home_dir();

	// Default claude.exe path: %USERPROFILE%\.local\bin\claude.exe
	static std::filesystem::path claude_exe_path();

	// Transforms a cwd into Claude's project directory slug.
	//   C:\Users\foo\bar  ->  C--Users-foo-bar
	static std::string encode_cwd(const std::string& cwd);

	// Absolute path: ~/.claude/projects/<encoded-cwd>/
	static std::filesystem::path project_dir_for(const std::string& cwd);

	// Snapshot the set of <pid>.json basenames currently in ~/.claude/sessions/.
	// Used around an agent spawn to diff against the post-spawn state and pick
	// out exactly the new pid.json that the just-launched claude wrote.
	static std::set<unsigned int> snapshot_pid_jsons();

	// Find a pid.json that appeared since `before`, whose PID is alive, and
	// whose PID isn't already in `claimed`. Among multiple candidates, returns
	// the one with the newest `startedAt`.
	static std::optional<PidJsonEntry>
		find_new_pid_json_since(const std::set<unsigned int>& before,
		                        const std::set<unsigned int>& claimed);

	// Given a pid previously claimed for this tab, return the pid of the
	// currently-live claude — either that pid itself, or a descendant of it
	// (when /resume has forked a new claude process inside the old one).
	// Returns 0 if nothing live under the chain.
	static unsigned int find_current_claude(unsigned int initial_pid);

	// Read the pid.json for a specific PID.
	static std::optional<PidJsonEntry> read_pid_json(unsigned int pid);

	// List all .jsonl filenames currently present in the project dir.
	static std::set<std::string> snapshot_jsonls(const std::filesystem::path& project_dir);
};

}
