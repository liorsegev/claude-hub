#include "AgentManager.hpp"
#include "Logger.hpp"

#include <gtest/gtest.h>

#include <filesystem>

namespace fs = std::filesystem;
using ch::AgentManager;
using ch::Logger;

namespace {

// Pins the API contract that lets App::frame poll spawns every frame without
// dragging the heavier tick() work along with it. If anyone re-merges the two
// (or removes pending_spawn_count, which the spawning-overlay relies on), this
// test catches it before the latency regression ships.
//
// We construct AgentManager with a null HWND. That is only safe because none
// of the methods exercised here touch the container window — reposition_active
// and commit_window_stage do, but neither is reachable on an empty manager.
class FreshManager : public ::testing::Test {
protected:
	void SetUp() override {
		log_path_ = fs::temp_directory_path() / "agents_hub_agent_mgr_test.log";
		log_ = std::make_unique<Logger>(log_path_);
		mgr_ = std::make_unique<AgentManager>(/*container=*/nullptr, *log_);
	}

	void TearDown() override {
		mgr_.reset();
		log_.reset();
		std::error_code ec;
		fs::remove(log_path_, ec);
	}

	fs::path log_path_;
	std::unique_ptr<Logger> log_;
	std::unique_ptr<AgentManager> mgr_;
};

}

TEST_F(FreshManager, PollSpawnsIsCallableIndependentlyOfTick) {
	// poll_spawns must be reachable without tick(); this is exactly what
	// App::frame relies on to drop spawn-docking latency below the slow-tick
	// interval. If poll_spawns disappears from the public surface this won't
	// compile.
	mgr_->poll_spawns();
	EXPECT_EQ(mgr_->size(), 0u);
	EXPECT_EQ(mgr_->pending_spawn_count(), 0u);
}

TEST_F(FreshManager, RepeatedPollSpawnsRemainsNoopWhenEmpty) {
	for (int i = 0; i < 100; ++i) mgr_->poll_spawns();
	EXPECT_EQ(mgr_->pending_spawn_count(), 0u);
}

TEST_F(FreshManager, PendingSpawnCountIsZeroBeforeAnySpawn) {
	EXPECT_EQ(mgr_->pending_spawn_count(), 0u);
}

TEST_F(FreshManager, ActiveIndexIsNegativeOneWhenNoAgents) {
	EXPECT_EQ(mgr_->active_index(), -1);
}
