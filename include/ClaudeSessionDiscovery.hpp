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

	// Find the newest ~/.claude/sessions/<pid>.json whose PID isn't in known_pids.
	static std::optional<PidJsonEntry>
		find_new_pid_json(const std::vector<unsigned int>& known_pids);

	// List all .jsonl filenames currently present in the project dir.
	static std::set<std::string> snapshot_jsonls(const std::filesystem::path& project_dir);
};

}
