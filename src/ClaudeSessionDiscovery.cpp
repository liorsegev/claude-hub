#include "ClaudeSessionDiscovery.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>

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

std::optional<ClaudeSessionDiscovery::PidJsonEntry>
ClaudeSessionDiscovery::find_new_pid_json(const std::vector<unsigned int>& known_pids,
                                          fs::file_time_type since) {
	const fs::path sessions_dir = home_dir() / CLAUDE_SUBDIR / SESSIONS_SUBDIR;
	std::error_code ec;
	if (!fs::exists(sessions_dir, ec)) return std::nullopt;

	fs::file_time_type best_time{};
	fs::path best_path;
	for (const auto& entry : fs::directory_iterator(sessions_dir)) {
		if (entry.path().extension() != ".json") continue;
		unsigned int pid = 0;
		try { pid = std::stoul(entry.path().stem().string()); } catch (...) { continue; }

		bool already = false;
		for (unsigned int kp : known_pids) if (kp == pid) { already = true; break; }
		if (already) continue;

		const auto mt = fs::last_write_time(entry.path(), ec);
		if (ec) continue;
		if (mt < since) continue;  // stale — predates our spawn
		if (mt > best_time) { best_time = mt; best_path = entry.path(); }
	}
	if (best_path.empty()) return std::nullopt;

	std::ifstream f(best_path);
	std::stringstream ss;
	ss << f.rdbuf();
	const std::string content = ss.str();

	PidJsonEntry out;
	out.session_id = extract_simple_value(content, KEY_SESSION_ID);
	try {
		out.pid = static_cast<unsigned int>(std::stoul(extract_simple_value(content, KEY_PID)));
	} catch (...) { out.pid = 0; }
	out.cwd = extract_cwd(content);
	out.name = extract_simple_value(content, KEY_NAME);

	if (out.session_id.empty()) return std::nullopt;
	return out;
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
