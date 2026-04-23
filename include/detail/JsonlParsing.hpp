#pragma once

#include <cstddef>
#include <string>

namespace ch::detail {

// Walk forward through `s` from `start` (expected to be one position past an
// opening JSON quote) to the matching unescaped closing quote. Returns the
// index of the closing `"`, or std::string::npos if unterminated.
//
// Understands `\"` and `\\` escapes; does not validate other escape sequences
// (leaves them for unescape_json to interpret).
inline std::size_t find_json_string_end(const std::string& s, std::size_t start) {
	while (start < s.size()) {
		const char c = s[start];
		if (c == '\\' && start + 1 < s.size()) { start += 2; continue; }
		if (c == '"') return start;
		++start;
	}
	return std::string::npos;
}

// Interpret backslash escapes in a JSON string body (the characters between
// the surrounding quotes). Handles \n \t \r \" \\ \/; for any other escape
// character, the trailing char passes through verbatim. A trailing lone
// backslash is kept as-is.
inline std::string unescape_json(const std::string& raw) {
	std::string out;
	out.reserve(raw.size());
	for (std::size_t i = 0; i < raw.size(); ++i) {
		if (raw[i] != '\\' || i + 1 >= raw.size()) { out += raw[i]; continue; }
		switch (raw[i + 1]) {
			case 'n':  out += '\n'; break;
			case 't':  out += '\t'; break;
			case 'r':  out += '\r'; break;
			case '"':  out += '"';  break;
			case '\\': out += '\\'; break;
			case '/':  out += '/';  break;
			default:   out += raw[i + 1]; break;
		}
		++i;
	}
	return out;
}

}
