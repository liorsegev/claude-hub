#include "JsonlActivityProbe.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;
using ch::JsonlActivityProbe;

namespace {

// Fixture: gives each test a fresh tmp .jsonl path it can write fake content
// into, construct a probe over, and assert on after polling.
class ProbeFixture : public ::testing::Test {
protected:
	void SetUp() override {
		tmp_dir_ = fs::temp_directory_path() / "agents-hub-probe-test";
		fs::remove_all(tmp_dir_);
		fs::create_directories(tmp_dir_);
		jsonl_path_ = tmp_dir_ / "session.jsonl";
	}

	void TearDown() override {
		std::error_code ec;
		fs::remove_all(tmp_dir_, ec);
	}

	// Writes `content` to the fixture jsonl path, replacing any prior content.
	void write_jsonl(const std::string& content) {
		std::ofstream f(jsonl_path_, std::ios::trunc | std::ios::binary);
		f << content;
	}

	JsonlActivityProbe make_probe() {
		// Logger null is fine — probe tolerates nullptr.
		return JsonlActivityProbe(jsonl_path_, "test", nullptr);
	}

	void poll(JsonlActivityProbe& p) {
		p.poll(std::chrono::steady_clock::now());
	}

	fs::path tmp_dir_;
	fs::path jsonl_path_;
};

}

// ───────────────────────── parse_tail via poll() ─────────────────────────

// NOTE on synthetic JSONL shape: parse_tail uses rfind("\"type\":\"") on each
// line to pick up the root record type. Claude's real format writes the root
// `"type"` AFTER the nested message body — so any nested `"type":"..."` keys
// (like `"type":"text"` inside message.content) come first in the line and
// the rfind skips past them to land on the root type. Our fixtures mirror that
// ordering. If you flip the order, the assistant/system/user detection breaks.

TEST_F(ProbeFixture, DetectsAssistantEndTurn) {
	write_jsonl(
		R"({"message":{"role":"user","content":"hi"},"type":"user"})" "\n"
		R"({"message":{"content":[{"type":"text","text":"hello"}],"stop_reason":"end_turn","usage":{"input_tokens":10,"output_tokens":5}},"type":"assistant"})" "\n"
	);

	auto p = make_probe();
	poll(p);

	EXPECT_EQ(p.last_entry_type(), "assistant");
	EXPECT_EQ(p.last_stop_reason(), "end_turn");
	EXPECT_EQ(p.last_assistant_text(), "hello");
	EXPECT_EQ(p.input_tokens(), 10);
	EXPECT_EQ(p.output_tokens(), 5);
}

TEST_F(ProbeFixture, IgnoresSystemMarkerLines) {
	// Claude appends a "system" turn_duration marker AFTER each assistant turn.
	// That line must not clobber the "last entry is assistant end_turn" state.
	write_jsonl(
		R"({"message":{"content":[{"type":"text","text":"ok"}],"stop_reason":"end_turn","usage":{"input_tokens":3,"output_tokens":2}},"type":"assistant"})" "\n"
		R"({"subtype":"turn_duration","ms":1234,"type":"system"})" "\n"
	);

	auto p = make_probe();
	poll(p);

	EXPECT_EQ(p.last_entry_type(), "assistant");
	EXPECT_EQ(p.last_stop_reason(), "end_turn");
}

TEST_F(ProbeFixture, LastUserEntryClearsStopReason) {
	write_jsonl(
		R"({"message":{"content":[{"type":"text","text":"reply"}],"stop_reason":"end_turn"},"type":"assistant"})" "\n"
		R"({"message":{"role":"user","content":"next"},"type":"user"})" "\n"
	);

	auto p = make_probe();
	poll(p);

	EXPECT_EQ(p.last_entry_type(), "user");
	EXPECT_EQ(p.last_stop_reason(), "");
}

TEST_F(ProbeFixture, AccumulatesOutputTokensAcrossAssistantTurns) {
	write_jsonl(
		R"({"message":{"content":[{"type":"text","text":"a"}],"usage":{"input_tokens":10,"output_tokens":5}},"type":"assistant"})" "\n"
		R"({"message":{"content":[{"type":"text","text":"b"}],"usage":{"input_tokens":15,"output_tokens":7}},"type":"assistant"})" "\n"
	);

	auto p = make_probe();
	poll(p);

	EXPECT_EQ(p.input_tokens(), 15);         // last value wins
	EXPECT_EQ(p.output_tokens(), 5 + 7);     // accumulated
}

// ───────────────────────── try_extract_title priority ─────────────────────────

TEST_F(ProbeFixture, TitleCustomTitleBeatsAgentName) {
	write_jsonl(
		R"({"customTitle":"Chosen","agentName":"Other"})" "\n"
	);
	auto p = make_probe();
	poll(p);
	EXPECT_EQ(p.conversation_title(), "Chosen");
}

TEST_F(ProbeFixture, TitleAgentNameBeatsFirstUserPrompt) {
	write_jsonl(
		R"({"agentName":"Named"})" "\n"
		R"({"role":"user","content":"fallback text"})" "\n"
	);
	auto p = make_probe();
	poll(p);
	EXPECT_EQ(p.conversation_title(), "Named");
}

TEST_F(ProbeFixture, TitleFallsBackToFirstUserPrompt) {
	write_jsonl(
		R"({"role":"user","content":"what is the meaning of life"})" "\n"
	);
	auto p = make_probe();
	poll(p);
	EXPECT_EQ(p.conversation_title(), "what is the meaning of life");
}

TEST_F(ProbeFixture, EmptyFileYieldsNoTitleOrType) {
	write_jsonl("");
	auto p = make_probe();
	poll(p);
	EXPECT_EQ(p.conversation_title(), "");
	EXPECT_EQ(p.last_entry_type(), "");
}

// ───────────────────────── poll is idempotent when file unchanged ─────────────────────────

TEST_F(ProbeFixture, PollTwiceOnUnchangedFileIsStable) {
	write_jsonl(
		R"({"type":"assistant","message":{"content":[{"type":"text","text":"x"}],"stop_reason":"end_turn","usage":{"input_tokens":1,"output_tokens":1}}})" "\n"
	);

	auto p = make_probe();
	poll(p);
	const int out_after_first = p.output_tokens();
	poll(p);
	// Unchanged file => second poll should early-return without re-accumulating.
	EXPECT_EQ(p.output_tokens(), out_after_first);
}
