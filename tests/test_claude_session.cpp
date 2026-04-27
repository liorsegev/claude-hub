#include "ClaudeSessionDiscovery.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using ch::ClaudeSessionDiscovery;

namespace {

// Redirects ClaudeSessionDiscovery's "home" to a tmp dir for the duration of
// one test. home_dir() reads %USERPROFILE% fresh on each call, so overriding
// the env var per-test is enough — no global state to protect.
class TempHome : public ::testing::Test {
protected:
	void SetUp() override {
		const fs::path tmp = fs::temp_directory_path() /
			("agents-hub-test-" + std::to_string(
				std::hash<const void*>{}(this)));
		fs::remove_all(tmp);
		fs::create_directories(tmp);
		tmp_ = tmp;
		prev_profile_ = std::getenv("USERPROFILE")
			? std::getenv("USERPROFILE")
			: std::string{};
		_putenv_s("USERPROFILE", tmp_.string().c_str());
	}

	void TearDown() override {
		if (!prev_profile_.empty())
			_putenv_s("USERPROFILE", prev_profile_.c_str());
		std::error_code ec;
		fs::remove_all(tmp_, ec);
	}

	fs::path sessions_dir() const {
		return tmp_ / ".claude" / "sessions";
	}

	void write(const fs::path& p, const std::string& content) {
		fs::create_directories(p.parent_path());
		std::ofstream f(p);
		f << content;
	}

	fs::path tmp_;
	std::string prev_profile_;
};

}

// ───────────────────────── encode_cwd ─────────────────────────

TEST(EncodeCwd, WindowsPathColonsAndBackslashes) {
	EXPECT_EQ(ClaudeSessionDiscovery::encode_cwd("C:\\Users\\foo"),
	          "C--Users-foo");
}

TEST(EncodeCwd, ForwardSlashesAlsoConverted) {
	EXPECT_EQ(ClaudeSessionDiscovery::encode_cwd("C:/Users/foo"),
	          "C--Users-foo");
}

TEST(EncodeCwd, EmptyStays) {
	EXPECT_EQ(ClaudeSessionDiscovery::encode_cwd(""), "");
}

TEST(EncodeCwd, NoSpecialCharsPassesThrough) {
	EXPECT_EQ(ClaudeSessionDiscovery::encode_cwd("abc_def.123"),
	          "abc_def.123");
}

// ───────────────────────── project_dir_for (uses home_dir) ─────────────────────────

TEST_F(TempHome, ProjectDirForBuildsUnderFakeHome) {
	const fs::path pd = ClaudeSessionDiscovery::project_dir_for("C:\\work\\x");
	const fs::path expected = tmp_ / ".claude" / "projects" / "C--work-x";
	EXPECT_EQ(pd, expected);
}

// ───────────────────────── read_pid_json ─────────────────────────

TEST_F(TempHome, ReadPidJsonExtractsFields) {
	const unsigned int pid = 12345;
	const fs::path pj = sessions_dir() / "12345.json";
	write(pj, R"({"sessionId":"abc-123","pid":12345,"cwd":"C:\\work","name":"my session","startedAt":1700000000000})");

	auto entry = ClaudeSessionDiscovery::read_pid_json(pid);
	ASSERT_TRUE(entry.has_value());
	EXPECT_EQ(entry->session_id, "abc-123");
	// Double-backslash in the JSON source collapses to a single backslash.
	EXPECT_EQ(entry->cwd, "C:\\work");
	EXPECT_EQ(entry->name, "my session");
	EXPECT_EQ(entry->pid, pid);
}

TEST_F(TempHome, ReadPidJsonReturnsNulloptWhenFileMissing) {
	auto entry = ClaudeSessionDiscovery::read_pid_json(99999);
	EXPECT_FALSE(entry.has_value());
}

TEST_F(TempHome, ReadPidJsonReturnsNulloptWhenSessionIdMissing) {
	const fs::path pj = sessions_dir() / "777.json";
	write(pj, R"({"pid":777,"cwd":"C:\\x"})");

	auto entry = ClaudeSessionDiscovery::read_pid_json(777);
	EXPECT_FALSE(entry.has_value());
}

TEST_F(TempHome, ReadPidJsonMissingNameIsEmpty) {
	// No /rename => Claude omits `name` or writes an empty one.
	const fs::path pj = sessions_dir() / "555.json";
	write(pj, R"({"sessionId":"xyz","pid":555,"cwd":"C:\\x"})");

	auto entry = ClaudeSessionDiscovery::read_pid_json(555);
	ASSERT_TRUE(entry.has_value());
	EXPECT_EQ(entry->name, "");
}

// ───────────────────────── snapshot_pid_jsons ─────────────────────────

TEST_F(TempHome, SnapshotPidJsonsCollectsNumericStems) {
	write(sessions_dir() / "100.json", "{}");
	write(sessions_dir() / "200.json", "{}");
	write(sessions_dir() / "notapid.json", "{}");  // non-numeric, ignored
	write(sessions_dir() / "300.txt", "{}");       // wrong extension, ignored

	const auto snap = ClaudeSessionDiscovery::snapshot_pid_jsons();
	EXPECT_TRUE(snap.count(100));
	EXPECT_TRUE(snap.count(200));
	EXPECT_EQ(snap.size(), 2u);
}

TEST_F(TempHome, SnapshotPidJsonsEmptyWhenDirMissing) {
	const auto snap = ClaudeSessionDiscovery::snapshot_pid_jsons();
	EXPECT_TRUE(snap.empty());
}

// ───────────────────────── snapshot_jsonls ─────────────────────────

TEST_F(TempHome, SnapshotJsonlsCollectsJsonlFilenames) {
	const fs::path proj = ClaudeSessionDiscovery::project_dir_for("C:\\p");
	write(proj / "a.jsonl", "{}");
	write(proj / "b.jsonl", "{}");
	write(proj / "c.txt", "{}");  // wrong ext, ignored

	const auto snap = ClaudeSessionDiscovery::snapshot_jsonls(proj);
	EXPECT_TRUE(snap.count("a.jsonl"));
	EXPECT_TRUE(snap.count("b.jsonl"));
	EXPECT_EQ(snap.size(), 2u);
}
