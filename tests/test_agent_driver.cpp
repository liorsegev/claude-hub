#include "AgentKind.hpp"
#include "IAgentDriver.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>

using ch::AgentKind;
using ch::get_driver;

// ───────────────────────── AgentKind to_string ─────────────────────────

TEST(AgentKindToString, AllKindsHaveDistinctLabels) {
	const std::string claude  = ch::to_string(AgentKind::Claude);
	const std::string copilot = ch::to_string(AgentKind::Copilot);
	const std::string gemini  = ch::to_string(AgentKind::Gemini);

	EXPECT_EQ(claude,  "claude");
	EXPECT_EQ(copilot, "copilot");
	EXPECT_EQ(gemini,  "gemini");

	EXPECT_NE(claude, copilot);
	EXPECT_NE(claude, gemini);
	EXPECT_NE(copilot, gemini);
}

// ───────────────────────── get_driver singleton ─────────────────────────

TEST(GetDriver, ReturnsSameInstanceAcrossCalls) {
	EXPECT_EQ(&get_driver(AgentKind::Claude),  &get_driver(AgentKind::Claude));
	EXPECT_EQ(&get_driver(AgentKind::Copilot), &get_driver(AgentKind::Copilot));
	EXPECT_EQ(&get_driver(AgentKind::Gemini),  &get_driver(AgentKind::Gemini));
}

TEST(GetDriver, DifferentKindsAreDifferentInstances) {
	EXPECT_NE(&get_driver(AgentKind::Claude),  &get_driver(AgentKind::Copilot));
	EXPECT_NE(&get_driver(AgentKind::Claude),  &get_driver(AgentKind::Gemini));
	EXPECT_NE(&get_driver(AgentKind::Copilot), &get_driver(AgentKind::Gemini));
}

// ───────────────────────── per-kind driver contract ─────────────────────────

TEST(DriverContract, ClaudeReportsClaudeTraits) {
	auto& d = get_driver(AgentKind::Claude);
	EXPECT_EQ(d.kind(), AgentKind::Claude);
	EXPECT_STREQ(d.name_prefix(), "claude");
	EXPECT_TRUE(d.uses_claude_telemetry());
	EXPECT_FALSE(d.build_command().empty());
}

TEST(DriverContract, CopilotIsNonTelemetry) {
	auto& d = get_driver(AgentKind::Copilot);
	EXPECT_EQ(d.kind(), AgentKind::Copilot);
	EXPECT_STREQ(d.name_prefix(), "copilot");
	EXPECT_FALSE(d.uses_claude_telemetry());
	EXPECT_FALSE(d.build_command().empty());
}

TEST(DriverContract, GeminiIsNonTelemetry) {
	auto& d = get_driver(AgentKind::Gemini);
	EXPECT_EQ(d.kind(), AgentKind::Gemini);
	EXPECT_STREQ(d.name_prefix(), "gemini");
	EXPECT_FALSE(d.uses_claude_telemetry());
	EXPECT_FALSE(d.build_command().empty());
}

TEST(DriverContract, NamePrefixesAreDistinct) {
	const std::string_view a = get_driver(AgentKind::Claude).name_prefix();
	const std::string_view b = get_driver(AgentKind::Copilot).name_prefix();
	const std::string_view c = get_driver(AgentKind::Gemini).name_prefix();
	EXPECT_NE(a, b);
	EXPECT_NE(a, c);
	EXPECT_NE(b, c);
}
