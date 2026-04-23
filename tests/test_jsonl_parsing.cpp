#include "detail/JsonlParsing.hpp"

#include <gtest/gtest.h>

#include <string>

using ch::detail::find_json_string_end;
using ch::detail::unescape_json;

// ───────────────────────── find_json_string_end ─────────────────────────

TEST(FindJsonStringEnd, SimpleString) {
	// input: "hello"   —   caller passes start = 1 (past opening quote)
	const std::string s = "\"hello\"";
	EXPECT_EQ(find_json_string_end(s, 1), 6u);
}

TEST(FindJsonStringEnd, EscapedQuoteIsNotTerminator) {
	// input: "he\"llo"  —  the \" should not terminate the string
	const std::string s = "\"he\\\"llo\"";
	//                     012 3 4 56789
	EXPECT_EQ(find_json_string_end(s, 1), 8u);
}

TEST(FindJsonStringEnd, EscapedBackslashBeforeQuote) {
	// input: "he\\"  —  the \\ is two chars; the next " IS the terminator
	const std::string s = "\"he\\\\\"";
	//                     012 3 4 5
	EXPECT_EQ(find_json_string_end(s, 1), 5u);
}

TEST(FindJsonStringEnd, UnterminatedReturnsNpos) {
	const std::string s = "\"unterminated";
	EXPECT_EQ(find_json_string_end(s, 1), std::string::npos);
}

TEST(FindJsonStringEnd, EmptyBodyTerminatesImmediately) {
	// input: ""   —  start=1 lands on closing quote directly
	const std::string s = "\"\"";
	EXPECT_EQ(find_json_string_end(s, 1), 1u);
}

// ───────────────────────── unescape_json ─────────────────────────

TEST(UnescapeJson, PassesPlainText) {
	EXPECT_EQ(unescape_json("hello world"), "hello world");
}

TEST(UnescapeJson, HandlesNewlineTabCarriageReturn) {
	EXPECT_EQ(unescape_json("a\\nb\\tc\\rd"), "a\nb\tc\rd");
}

TEST(UnescapeJson, HandlesEscapedQuote) {
	EXPECT_EQ(unescape_json("say \\\"hi\\\""), "say \"hi\"");
}

TEST(UnescapeJson, HandlesEscapedBackslash) {
	// Input text is four chars: '\\' '\\' + 'n' -> that's \\n literally in JSON.
	// After unescape, should yield: backslash + 'n' (NOT a newline).
	EXPECT_EQ(unescape_json("\\\\n"), "\\n");
}

TEST(UnescapeJson, HandlesEscapedForwardSlash) {
	EXPECT_EQ(unescape_json("path\\/to\\/file"), "path/to/file");
}

TEST(UnescapeJson, UnknownEscapePassesLiteralChar) {
	// \x is not defined — the 'x' should pass through, backslash consumed.
	EXPECT_EQ(unescape_json("\\x"), "x");
}

TEST(UnescapeJson, TrailingBackslashKeptAsIs) {
	// A lone trailing backslash has no char after it — kept as-is per impl.
	EXPECT_EQ(unescape_json("abc\\"), "abc\\");
}

TEST(UnescapeJson, EmptyInputYieldsEmpty) {
	EXPECT_EQ(unescape_json(""), "");
}
