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
	// Do everything the UI doesn't need immediately on a worker thread.
	// The only thing this function does synchronously is capture snapshots
	// (pid.json + window list) that must reflect pre-spawn state, then
	// queue a future. tick() picks up completed spawns and creates the Agent.
	char unique_name[64];
	std::snprintf(unique_name, sizeof(unique_name), "chub_%llu_%d",
		static_cast<unsigned long long>(GetTickCount64()), next_id_++);
	const std::string name(unique_name);

	const auto pid_snapshot = ClaudeSessionDiscovery::snapshot_pid_jsons();
	const std::vector<HWND> before = WindowsTerminalSpawner::enumerate_candidate_windows();
	const auto spawn_time = std::chrono::steady_clock::now();
	const std::string claude_exe = ClaudeSessionDiscovery::claude_exe_path().string();

	log_.logf("Agent %s: spawn queued\n", name.c_str());

	pending_spawns_.push_back(std::async(std::launch::async,
		[this, name, pid_snapshot, before, spawn_time, claude_exe]() {
			PendingSpawn p;
			p.unique_name = name;
			p.spawn_time = spawn_time;

			auto sr = WindowsTerminalSpawner::spawn(name, claude_exe, before);
			if (!sr.window) return p;
			p.window = sr.window;
			p.process_handle = sr.process_handle;

			// The claim step must be serialized across concurrent spawns so
			// two tabs can't race on the same newly-written pid.json.
			std::lock_guard<std::mutex> lock(claim_mutex_);
			for (int i = 0; i < constants::SESSION_FILE_POLL_ATTEMPTS; ++i) {
				auto info = ClaudeSessionDiscovery::find_new_pid_json_since(
					pid_snapshot, claimed_pids_);
				if (info) {
					p.cwd = info->cwd;
					p.claude_pid = info->pid;
					claimed_pids_.insert(info->pid);
					break;
				}
				Sleep(constants::WT_POLL_SLEEP_MS);
			}

			if (!p.cwd.empty()) {
				p.jsonl_snapshot = ClaudeSessionDiscovery::snapshot_jsonls(
					ClaudeSessionDiscovery::project_dir_for(p.cwd));
			}
			return p;
		}));
}

void AgentManager::poll_pending_spawns() {
	for (auto it = pending_spawns_.begin(); it != pending_spawns_.end(); ) {
		if (it->wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
			++it;
			continue;
		}
		PendingSpawn p = it->get();
		it = pending_spawns_.erase(it);
		commit_spawn(std::move(p));
	}
}

void AgentManager::commit_spawn(PendingSpawn p) {
	if (!p.window) {
		log_.logf("Agent %s: WT spawn failed\n", p.unique_name.c_str());
		return;
	}
	if (p.claude_pid == 0) {
		log_.logf("Agent %s: pid.json NOT FOUND after spawn\n", p.unique_name.c_str());
	} else {
		log_.logf("Agent %s: claude pid=%u cwd=%s\n",
			p.unique_name.c_str(), p.claude_pid, p.cwd.c_str());
	}

	for (const auto& ag : agents_)
		if (!ag->jsonl_path().empty())
			p.jsonl_snapshot.insert(ag->jsonl_path().filename().string());

	auto agent = std::make_unique<Agent>(
		p.unique_name,
		p.window,
		p.process_handle,
		p.claude_pid,
		p.cwd,
		std::move(p.jsonl_snapshot),
		p.spawn_time);

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
