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
	: container_(container_window)
	, log_(log)
	, flag_watcher_(ClaudeSessionDiscovery::home_dir() / ".claude" / "hub-waiting") {}

void AgentManager::spawn() {
	char unique_name[64];
	std::snprintf(unique_name, sizeof(unique_name), "chub_%llu_%d",
		static_cast<unsigned long long>(GetTickCount64()), next_id_++);
	const std::string name(unique_name);

	const auto pid_snapshot = ClaudeSessionDiscovery::snapshot_pid_jsons();
	const std::vector<HWND> before = WindowsTerminalSpawner::enumerate_candidate_windows();
	const auto spawn_time = std::chrono::steady_clock::now();
	const std::string claude_exe = ClaudeSessionDiscovery::claude_exe_path().string();

	log_.logf("Agent %s: spawn queued\n", name.c_str());

	// Two-stage async: emit the window as soon as it's found so the UI can
	// dock the terminal; continue on the same thread to find the pid.json
	// and emit the probe info later.
	auto window_promise = std::make_shared<std::promise<WindowStage>>();
	auto probe_promise  = std::make_shared<std::promise<ProbeStage>>();

	PendingSpawn ps;
	ps.spawn_time     = spawn_time;
	ps.window_future  = window_promise->get_future();
	ps.probe_future   = probe_promise->get_future();
	pending_spawns_.push_back(std::move(ps));

	std::thread(
		[this, name, pid_snapshot, before, claude_exe, window_promise, probe_promise]() {
			// ── Stage 1: launch wt.exe and find the new terminal window ──
			WindowStage w;
			w.unique_name = name;
			auto sr = WindowsTerminalSpawner::spawn(name, claude_exe, before);
			w.window = sr.window;
			w.process_handle = sr.process_handle;
			window_promise->set_value(w);

			if (!sr.window) { probe_promise->set_value({}); return; }

			// ── Stage 2: claim the pid.json this spawn produced ──
			ProbeStage pr;
			{
				std::lock_guard<std::mutex> lock(claim_mutex_);
				for (int i = 0; i < constants::SESSION_FILE_POLL_ATTEMPTS; ++i) {
					auto info = ClaudeSessionDiscovery::find_new_pid_json_since(
						pid_snapshot, claimed_pids_);
					if (info) {
						pr.claude_pid = info->pid;
						pr.cwd = info->cwd;
						claimed_pids_.insert(info->pid);
						break;
					}
					Sleep(constants::WT_POLL_SLEEP_MS);
				}
			}
			if (!pr.cwd.empty()) {
				pr.jsonl_snapshot = ClaudeSessionDiscovery::snapshot_jsonls(
					ClaudeSessionDiscovery::project_dir_for(pr.cwd));
			}
			probe_promise->set_value(pr);
		}
	).detach();
}

void AgentManager::poll_pending_spawns() {
	for (auto it = pending_spawns_.begin(); it != pending_spawns_.end(); ) {
		// Window stage: dock as soon as ready.
		if (it->agent_index < 0 && it->window_future.valid() &&
		    it->window_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
			WindowStage w = it->window_future.get();
			it->agent_index = commit_window_stage(std::move(w), it->spawn_time);
			if (it->agent_index < 0) { it = pending_spawns_.erase(it); continue; }
		}

		// Probe stage: fill in claude_pid/cwd/jsonl_snapshot once ready.
		if (it->agent_index >= 0 && it->probe_future.valid() &&
		    it->probe_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
			ProbeStage pr = it->probe_future.get();
			commit_probe_stage(it->agent_index, std::move(pr));
			it = pending_spawns_.erase(it);
			continue;
		}
		++it;
	}
}

int AgentManager::commit_window_stage(WindowStage w,
                                       std::chrono::steady_clock::time_point spawn_time) {
	if (!w.window) {
		log_.logf("Agent %s: WT spawn failed\n", w.unique_name.c_str());
		return -1;
	}

	auto agent = std::make_unique<Agent>(
		w.unique_name,
		w.window,
		w.process_handle,
		0,                           // claude_pid filled in by probe stage
		std::string{},               // cwd filled in by probe stage
		std::set<std::string>{},     // jsonl_snapshot filled in by probe stage
		spawn_time);

	agent->reparent_as_child(container_);

	if (active_ >= 0 && active_ < static_cast<int>(agents_.size()))
		agents_[active_]->hide();

	agents_.push_back(std::move(agent));
	const int idx = static_cast<int>(agents_.size()) - 1;
	active_ = idx;
	agents_[idx]->show();
	reposition_active();
	agents_[idx]->focus();
	return idx;
}

void AgentManager::commit_probe_stage(int agent_index, ProbeStage p) {
	if (agent_index < 0 || agent_index >= static_cast<int>(agents_.size())) return;
	Agent& a = *agents_[agent_index];

	if (p.claude_pid == 0) {
		log_.logf("Agent %s: pid.json NOT FOUND after spawn\n", a.name().c_str());
		return;
	}
	log_.logf("Agent %s: claude pid=%u cwd=%s\n",
		a.name().c_str(), p.claude_pid, p.cwd.c_str());

	// Fold in any JSONLs already attached to sibling agents so discover_jsonls
	// doesn't re-claim one of theirs.
	for (const auto& ag : agents_)
		if (!ag->jsonl_path().empty())
			p.jsonl_snapshot.insert(ag->jsonl_path().filename().string());

	a.set_claude_pid(p.claude_pid);
	a.set_cwd(std::move(p.cwd));
	a.set_jsonl_snapshot(std::move(p.jsonl_snapshot));
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
	if (!agents_[active_]->jsonl_path().empty())
		flag_watcher_.clear(agents_[active_]->jsonl_path().stem().string());
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
	poll_pending_spawns();
	reap_dead();
	discover_jsonls();
	sync_pid_state();

	const auto now = std::chrono::steady_clock::now();
	for (auto& a : agents_)
		if (a->has_probe()) a->probe()->poll(now);

	update_waiting();
}

void AgentManager::sync_pid_state() {
	// Follow the /resume chain per-tab: from the initial claude pid we claimed
	// at spawn, find the current live claude (itself or a forked descendant).
	// This works because /resume spawns the new claude *inside* the old one,
	// so the new pid is a descendant of the old — walking the process tree
	// from the anchor pid reliably locates it.
	for (auto& a : agents_) {
		if (a->claude_pid() == 0) continue;
		const unsigned int live_pid = ClaudeSessionDiscovery::find_current_claude(a->claude_pid());
		if (!live_pid) continue;

		if (live_pid != a->claude_pid()) {
			log_.logf("Agent %s: claude pid %u -> %u\n",
				a->name().c_str(), a->claude_pid(), live_pid);
			a->set_claude_pid(live_pid);
		}

		auto info = ClaudeSessionDiscovery::read_pid_json(live_pid);
		if (!info) continue;

		if (a->title() != info->name)
			a->set_title(info->name);  // empty string when no /rename yet

		const std::string current_sid = a->jsonl_path().empty()
			? std::string{}
			: a->jsonl_path().stem().string();
		if (info->session_id.empty() || info->session_id == current_sid) continue;

		const fs::path new_jsonl = ClaudeSessionDiscovery::project_dir_for(a->cwd())
			/ (info->session_id + ".jsonl");
		std::error_code ec;
		if (!fs::exists(new_jsonl, ec)) continue;

		log_.logf("Agent %s: session switched %s -> %s\n",
			a->name().c_str(), current_sid.c_str(), info->session_id.c_str());
		a->attach_jsonl(new_jsonl, &log_);
	}
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
	const auto pending = flag_watcher_.poll_pending();

	for (int i = 0; i < static_cast<int>(agents_.size()); ++i) {
		Agent& a = *agents_[i];
		const bool prev_waiting = a.waiting();

		if (i == active_) {
			a.set_waiting(false);
			if (!a.jsonl_path().empty())
				flag_watcher_.clear(a.jsonl_path().stem().string());
			continue;
		}
		if (!a.has_probe()) { a.set_waiting(false); continue; }

		const std::string sid = a.jsonl_path().stem().string();
		const bool hook_flagged = pending.count(sid) > 0;

		const IActivityProbe& p = *a.probe();
		const bool turn_complete = p.last_entry_type() == "assistant"
			&& p.last_stop_reason() == "end_turn";
		const bool new_waiting = hook_flagged || turn_complete;

		a.set_waiting(new_waiting);

		if (prev_waiting != new_waiting) {
			log_.logf("Agent %s: waiting %d -> %d (last=%s stop=%s hook=%d)\n",
				a.name().c_str(),
				prev_waiting ? 1 : 0,
				new_waiting ? 1 : 0,
				p.last_entry_type().c_str(),
				p.last_stop_reason().c_str(),
				hook_flagged ? 1 : 0);
		}
	}
}

}
