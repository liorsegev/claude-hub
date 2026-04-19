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

	// Find the newest ~/.claude/sessions/<pid>.json whose PID isn't in known_pids
	// AND whose last-write-time is at or after `since`. The `since` guard is
	// critical: the sessions dir accumulates stale pid.json files from other
	// Claude processes; without it the first spawn picks up somebody else's
	// session instead of the one we just started.
	static std::optional<PidJsonEntry>
		find_new_pid_json(const std::vector<unsigned int>& known_pids,
		                  std::filesystem::file_time_type since);

	// Re-read the pid.json for a specific PID (used to detect /resume mid-run).
	static std::optional<PidJsonEntry> read_pid_json(unsigned int pid);

	// List all .jsonl filenames currently present in the project dir.
	static std::set<std::string> snapshot_jsonls(const std::filesystem::path& project_dir);
};

}
