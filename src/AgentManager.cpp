#include "AgentManager.hpp"
#include "ClaudeSessionDiscovery.hpp"
#include "Constants.hpp"
#include "WindowsTerminalSpawner.hpp"

#include <algorithm>
#include <cstdio>
#include <system_error>

namespace ch {

namespace fs = std::filesystem;

AgentManager::AgentManager(HWND container_window, Logger& log)
	: container_(container_window), log_(log) {}

void AgentManager::spawn() {
	char unique_name[64];
	std::snprintf(unique_name, sizeof(unique_name), "chub_%llu_%d",
		static_cast<unsigned long long>(GetTickCount64()), next_id_++);

	std::vector<unsigned int> known_claude_pids;
	for (const auto& a : agents_)
		if (a->claude_pid()) known_claude_pids.push_back(a->claude_pid());

	// ── 1. Launch wt.exe + claude and locate the new terminal window ──
	const std::vector<HWND> before = WindowsTerminalSpawner::enumerate_candidate_windows();
	auto spawn_result = WindowsTerminalSpawner::spawn(
		unique_name,
		ClaudeSessionDiscovery::claude_exe_path().string(),
		before);
	if (!spawn_result.window) {
		log_.logf("Agent %s: WT spawn failed\n", unique_name);
		return;
	}

	// ── 2. Read sessions/<pid>.json just to learn cwd (sessionId is unreliable) ──
	std::string cwd;
	unsigned int claude_pid = 0;
	for (int i = 0; i < constants::SESSION_FILE_POLL_ATTEMPTS; ++i) {
		auto info = ClaudeSessionDiscovery::find_new_pid_json(known_claude_pids);
		if (info) {
			cwd = info->cwd;
			claude_pid = info->pid;
			log_.logf("Agent %s: pid.json cwd=%s initial_sid=%s\n",
				unique_name, cwd.c_str(), info->session_id.c_str());
			break;
		}
		Sleep(constants::WT_POLL_SLEEP_MS);
	}

	// ── 3. Snapshot existing JSONLs so tick() can find the new one ──
	std::set<std::string> snapshot;
	if (!cwd.empty()) {
		snapshot = ClaudeSessionDiscovery::snapshot_jsonls(
			ClaudeSessionDiscovery::project_dir_for(cwd));
		for (const auto& ag : agents_)
			if (!ag->jsonl_path().empty())
				snapshot.insert(ag->jsonl_path().filename().string());
	}
	log_.logf("Agent %s: snapshot=%zu existing jsonls\n", unique_name, snapshot.size());

	// ── 4. Build Agent, reparent, and show ──
	auto agent = std::make_unique<Agent>(
		unique_name,
		spawn_result.window,
		spawn_result.process_handle,
		claude_pid,
		cwd,
		std::move(snapshot),
		std::chrono::steady_clock::now());

	agent->reparent_as_child(container_);

	if (active_ >= 0 && active_ < static_cast<int>(agents_.size()))
		agents_[active_]->hide();

	agents_.push_back(std::move(agent));
	active_ = static_cast<int>(agents_.size()) - 1;

	agents_[active_]->show();
	reposition_active();
	agents_[active_]->focus();
}

void AgentManager::kill(int index) {
	if (index < 0 || index >= static_cast<int>(agents_.size())) return;
	agents_.erase(agents_.begin() + index);

	if (agents_.empty()) { active_ = -1; return; }

	active_ = index % static_cast<int>(agents_.size());
	agents_[active_]->show();
	reposition_active();
	agents_[active_]->focus();
}

void AgentManager::switch_to(int index) {
	if (index < 0 || index >= static_cast<int>(agents_.size()) || index == active_) return;
	if (active_ >= 0) agents_[active_]->hide();
	active_ = index;
	agents_[active_]->show();
	reposition_active();
	agents_[active_]->focus();
	agents_[active_]->set_waiting(false);
}

void AgentManager::reposition_active() {
	if (active_ < 0 || !container_) return;
	RECT cr;
	GetClientRect(container_, &cr);
	agents_[active_]->move_to(
		0, 0,
		cr.right - constants::SIDEBAR_WIDTH_PX,
		cr.bottom);
}

void AgentManager::tick() {
	reap_dead();
	discover_jsonls();

	const auto now = std::chrono::steady_clock::now();
	for (auto& a : agents_)
		if (a->has_probe()) a->probe()->poll(now);

	update_waiting();
}

void AgentManager::reap_dead() {
	for (int i = static_cast<int>(agents_.size()) - 1; i >= 0; --i) {
		if (agents_[i]->is_alive()) continue;
		agents_.erase(agents_.begin() + i);
		if (active_ == i) active_ = agents_.empty() ? -1 : 0;
		else if (active_ > i) active_--;
		if (active_ >= 0) {
			agents_[active_]->show();
			reposition_active();
		}
	}
}

void AgentManager::discover_jsonls() {
	// FIFO match: oldest unclaimed agent <- oldest unclaimed JSONL (by mtime).
	std::vector<int> unclaimed;
	for (int j = 0; j < static_cast<int>(agents_.size()); ++j)
		if (!agents_[j]->has_probe() && !agents_[j]->cwd().empty())
			unclaimed.push_back(j);

	if (unclaimed.empty()) return;

	std::sort(unclaimed.begin(), unclaimed.end(), [this](int a, int b) {
		return agents_[a]->spawn_time() < agents_[b]->spawn_time();
	});

	Agent& target = *agents_[unclaimed.front()];
	const fs::path proj_dir = ClaudeSessionDiscovery::project_dir_for(target.cwd());

	std::error_code ec;
	if (!fs::exists(proj_dir, ec)) return;

	struct Candidate { fs::path path; fs::file_time_type mtime; };
	std::vector<Candidate> candidates;
	for (const auto& e : fs::directory_iterator(proj_dir)) {
		if (e.path().extension() != ".jsonl") continue;
		const std::string fn = e.path().filename().string();
		if (target.jsonl_snapshot().count(fn)) continue;

		bool claimed = false;
		for (const auto& ag : agents_) {
			if (!ag->jsonl_path().empty() &&
				ag->jsonl_path().filename().string() == fn) { claimed = true; break; }
		}
		if (claimed) continue;

		candidates.push_back({e.path(), fs::last_write_time(e.path(), ec)});
	}
	if (candidates.empty()) return;

	std::sort(candidates.begin(), candidates.end(),
		[](const Candidate& a, const Candidate& b) { return a.mtime < b.mtime; });

	target.attach_jsonl(candidates.front().path, &log_);
	log_.logf("Agent %s: discovered jsonl=%s (FIFO match, %zu candidates)\n",
		target.name().c_str(),
		candidates.front().path.string().c_str(),
		candidates.size());
}

void AgentManager::update_waiting() {
	const auto now = std::chrono::steady_clock::now();
	for (int i = 0; i < static_cast<int>(agents_.size()); ++i) {
		Agent& a = *agents_[i];
		if (i == active_ || !a.has_probe()) { a.set_waiting(false); continue; }

		const IActivityProbe& p = *a.probe();
		const bool silent = p.seconds_since_growth(now) > constants::WAITING_SILENCE_SECONDS;
		const bool mid_response = p.last_entry_type() == "user";
		a.set_waiting(silent && !mid_response);
	}
}

}
