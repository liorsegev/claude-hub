#include "ClaudeSessionDiscovery.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <queue>
#include <sstream>
#include <system_error>
#include <unordered_map>

namespace ch {

namespace fs = std::filesystem;

namespace {

constexpr const char* HOME_FALLBACK = "C:\\Users\\liors";
constexpr const char* CLAUDE_SUBDIR = ".claude";
constexpr const char* SESSIONS_SUBDIR = "sessions";
constexpr const char* PROJECTS_SUBDIR = "projects";
constexpr const char* CLAUDE_EXE_REL = ".local\\bin\\claude.exe";
constexpr const char* KEY_SESSION_ID = "sessionId";
constexpr const char* KEY_PID = "pid";
constexpr const char* KEY_NAME = "name";
constexpr const char* KEY_STARTED_AT_PREFIX = "\"startedAt\":";
constexpr size_t KEY_STARTED_AT_PREFIX_LEN = 12;
constexpr const char* KEY_CWD_HEAD = "\"cwd\":\"";

std::string extract_simple_value(const std::string& content, const std::string& key) {
	const size_t p = content.find("\"" + key + "\":");
	if (p == std::string::npos) return "";
	size_t start = p + key.size() + 3;
	while (start < content.size() && (content[start] == ' ' || content[start] == '"')) start++;
	const size_t end = content.find_first_of(",\"}", start);
	return content.substr(start, end - start);
}

std::string extract_cwd(const std::string& content) {
	const size_t cp = content.find(KEY_CWD_HEAD);
	if (cp == std::string::npos) return "";
	const size_t start = cp + std::string(KEY_CWD_HEAD).size();
	const size_t end = content.find('"', start);
	std::string raw = content.substr(start, end - start);
	std::string out;
	for (size_t i = 0; i < raw.size(); i++) {
		if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == '\\') {
			out += '\\';
			i++;
		} else {
			out += raw[i];
		}
	}
	return out;
}

bool is_process_alive(unsigned int pid) {
	HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
	if (!h) return false;
	const DWORD res = WaitForSingleObject(h, 0);
	CloseHandle(h);
	return res == WAIT_TIMEOUT;
}

long long parse_started_at(const std::string& content) {
	const size_t sp = content.find(KEY_STARTED_AT_PREFIX);
	if (sp == std::string::npos) return 0;
	try { return std::stoll(content.c_str() + sp + KEY_STARTED_AT_PREFIX_LEN); }
	catch (...) { return 0; }
}

std::string read_file(const std::filesystem::path& p) {
	std::ifstream f(p);
	if (!f) return {};
	std::stringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

ClaudeSessionDiscovery::PidJsonEntry content_to_entry(const std::string& content) {
	ClaudeSessionDiscovery::PidJsonEntry out;
	out.session_id = extract_simple_value(content, KEY_SESSION_ID);
	try {
		out.pid = static_cast<unsigned int>(
			std::stoul(extract_simple_value(content, KEY_PID)));
	} catch (...) { out.pid = 0; }
	out.cwd = extract_cwd(content);
	out.name = extract_simple_value(content, KEY_NAME);
	return out;
}

}

fs::path ClaudeSessionDiscovery::home_dir() {
	const char* env = std::getenv("USERPROFILE");
	return fs::path(env ? env : HOME_FALLBACK);
}

fs::path ClaudeSessionDiscovery::claude_exe_path() {
	return home_dir() / CLAUDE_EXE_REL;
}

std::string ClaudeSessionDiscovery::encode_cwd(const std::string& cwd) {
	std::string out;
	out.reserve(cwd.size());
	for (char c : cwd) {
		if (c == ':' || c == '\\' || c == '/') out += '-';
		else out += c;
	}
	return out;
}

fs::path ClaudeSessionDiscovery::project_dir_for(const std::string& cwd) {
	return home_dir() / CLAUDE_SUBDIR / PROJECTS_SUBDIR / encode_cwd(cwd);
}

std::set<unsigned int> ClaudeSessionDiscovery::snapshot_pid_jsons() {
	std::set<unsigned int> out;
	const fs::path sessions_dir = home_dir() / CLAUDE_SUBDIR / SESSIONS_SUBDIR;
	std::error_code ec;
	if (!fs::exists(sessions_dir, ec)) return out;
	for (const auto& entry : fs::directory_iterator(sessions_dir, ec)) {
		if (ec) break;
		if (entry.path().extension() != ".json") continue;
		try { out.insert(static_cast<unsigned int>(std::stoul(entry.path().stem().string()))); }
		catch (...) {}
	}
	return out;
}

std::optional<ClaudeSessionDiscovery::PidJsonEntry>
ClaudeSessionDiscovery::find_new_pid_json_since(const std::set<unsigned int>& before,
                                                const std::set<unsigned int>& claimed) {
	const fs::path sessions_dir = home_dir() / CLAUDE_SUBDIR / SESSIONS_SUBDIR;
	std::error_code ec;
	if (!fs::exists(sessions_dir, ec)) return std::nullopt;

	long long best_started = 0;
	std::string best_content;
	for (const auto& entry : fs::directory_iterator(sessions_dir, ec)) {
		if (ec) break;
		if (entry.path().extension() != ".json") continue;
		unsigned int pid = 0;
		try { pid = static_cast<unsigned int>(std::stoul(entry.path().stem().string())); }
		catch (...) { continue; }
		if (before.count(pid)) continue;
		if (claimed.count(pid)) continue;
		if (!is_process_alive(pid)) continue;

		const std::string content = read_file(entry.path());
		if (content.empty()) continue;
		const long long started = parse_started_at(content);
		if (started <= best_started) continue;
		best_started = started;
		best_content = content;
	}
	if (best_content.empty()) return std::nullopt;

	auto out = content_to_entry(best_content);
	if (out.session_id.empty()) return std::nullopt;
	return out;
}

unsigned int ClaudeSessionDiscovery::find_current_claude(unsigned int initial_pid) {
	if (initial_pid == 0) return 0;

	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE) return 0;

	std::unordered_map<DWORD, std::vector<DWORD>> children_of;
	PROCESSENTRY32W e{};
	e.dwSize = sizeof(e);
	if (Process32FirstW(snap, &e)) {
		do {
			children_of[e.th32ParentProcessID].push_back(e.th32ProcessID);
		} while (Process32NextW(snap, &e));
	}
	CloseHandle(snap);

	// BFS: collect initial_pid + all descendants (bounded depth for safety).
	std::vector<DWORD> candidates;
	std::queue<std::pair<DWORD, int>> queue;
	queue.push({initial_pid, 0});
	std::set<DWORD> visited;
	while (!queue.empty()) {
		auto [p, depth] = queue.front(); queue.pop();
		if (visited.count(p) || depth > 16) continue;
		visited.insert(p);
		candidates.push_back(p);
		auto it = children_of.find(p);
		if (it != children_of.end())
			for (DWORD c : it->second) queue.push({c, depth + 1});
	}

	// Among candidates, pick the one that has a pid.json, is alive, and has
	// the newest startedAt. This follows /resume chains (new forked claude
	// always has a newer startedAt than its predecessor).
	const fs::path sessions_dir = home_dir() / CLAUDE_SUBDIR / SESSIONS_SUBDIR;
	long long best_started = 0;
	unsigned int best_pid = 0;
	for (DWORD pid : candidates) {
		const fs::path pj = sessions_dir / (std::to_string(pid) + ".json");
		std::error_code ec;
		if (!fs::exists(pj, ec)) continue;
		if (!is_process_alive(pid)) continue;

		const std::string content = read_file(pj);
		if (content.empty()) continue;
		const long long started = parse_started_at(content);
		if (started <= best_started) continue;
		best_started = started;
		best_pid = pid;
	}
	return best_pid;
}

std::optional<ClaudeSessionDiscovery::PidJsonEntry>
ClaudeSessionDiscovery::read_pid_json(unsigned int pid) {
	const fs::path p = home_dir() / CLAUDE_SUBDIR / SESSIONS_SUBDIR
		/ (std::to_string(pid) + ".json");
	std::error_code ec;
	if (!fs::exists(p, ec)) return std::nullopt;

	std::ifstream f(p);
	std::stringstream ss;
	ss << f.rdbuf();
	const std::string content = ss.str();

	PidJsonEntry out;
	out.session_id = extract_simple_value(content, KEY_SESSION_ID);
	out.cwd = extract_cwd(content);
	out.name = extract_simple_value(content, KEY_NAME);
	out.pid = pid;

	if (out.session_id.empty()) return std::nullopt;
	return out;
}

std::set<std::string> ClaudeSessionDiscovery::snapshot_jsonls(const fs::path& project_dir) {
	std::set<std::string> out;
	std::error_code ec;
	if (!fs::exists(project_dir, ec)) return out;
	for (const auto& entry : fs::directory_iterator(project_dir)) {
		if (entry.path().extension() == ".jsonl")
			out.insert(entry.path().filename().string());
	}
	return out;
}

}
