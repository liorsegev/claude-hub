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

	// Find the newest unclaimed ~/.claude/sessions/<pid>.json whose:
	//   - PID isn't in known_pids
	//   - cwd matches target_cwd (case-insensitive on Windows)
	//   - PID refers to a process that is currently alive
	//
	// The cwd filter keeps us from matching Claude processes in other projects;
	// the liveness check keeps us from matching stale pid.json files left over
	// from crashed/exited Claude processes in the same project.
	static std::optional<PidJsonEntry>
		find_new_pid_json(const std::vector<unsigned int>& known_pids,
		                  const std::string& target_cwd);

	// Re-read the pid.json for a specific PID (used to detect /resume mid-run).
	static std::optional<PidJsonEntry> read_pid_json(unsigned int pid);

	// List all .jsonl filenames currently present in the project dir.
	static std::set<std::string> snapshot_jsonls(const std::filesystem::path& project_dir);
};

}
