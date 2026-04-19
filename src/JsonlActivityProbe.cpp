#include "JsonlActivityProbe.hpp"
#include "Constants.hpp"

#include <cstdlib>
#include <fstream>
#include <system_error>

namespace ch {

namespace fs = std::filesystem;

namespace {

// Walk forward through s from `start` (one past an opening JSON quote) to the
// matching closing quote, respecting backslash escapes. Returns npos if unterminated.
size_t find_json_string_end(const std::string& s, size_t start) {
	while (start < s.size()) {
		const char c = s[start];
		if (c == '\\' && start + 1 < s.size()) { start += 2; continue; }
		if (c == '"') return start;
		++start;
	}
	return std::string::npos;
}

std::string unescape_json(const std::string& raw) {
	std::string out;
	out.reserve(raw.size());
	for (size_t i = 0; i < raw.size(); ++i) {
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

JsonlActivityProbe::JsonlActivityProbe(fs::path jsonl_path,
                                       std::string owner_name,
                                       Logger* log,
                                       std::chrono::steady_clock::time_point spawn_time)
	: path_(std::move(jsonl_path))
	, owner_name_(std::move(owner_name))
	, log_(log)
	, last_growth_(spawn_time) {}

float JsonlActivityProbe::seconds_since_growth(std::chrono::steady_clock::time_point now) const {
	return std::chrono::duration<float>(now - last_growth_).count();
}

void JsonlActivityProbe::poll(std::chrono::steady_clock::time_point now) {
	std::error_code ec;
	auto fsize = fs::file_size(path_, ec);
	if (ec || fsize == 0 || fsize == last_size_) return;

	last_size_ = fsize;
	last_growth_ = now;
	growth_count_++;
	if (log_) log_->logf("Agent %s: JSONL grew to %llu bytes (growth #%d)\n",
		owner_name_.c_str(), static_cast<unsigned long long>(fsize), growth_count_);

	std::ifstream f(path_, std::ios::ate);
	if (!f) return;
	const std::streampos fsz = f.tellg();
	const size_t read_size = fsz < static_cast<std::streamoff>(constants::JSONL_TAIL_BYTES)
		? static_cast<size_t>(fsz)
		: constants::JSONL_TAIL_BYTES;
	f.seekg(-static_cast<std::streamoff>(read_size), std::ios::end);
	std::string tail(read_size, '\0');
	f.read(&tail[0], read_size);

	parse_tail(tail);

	if (log_) log_->logf("Agent %s: last=%s stop_reason=%s\n",
		owner_name_.c_str(), last_entry_type_.c_str(), last_stop_reason_.c_str());
}

void JsonlActivityProbe::parse_tail(const std::string& tail) {
	// Grab the last JSONL record's root "type" (entries contain nested
	// types, so we rfind to get the outermost one).
	const size_t last_nl = tail.rfind('\n', tail.size() - 2);
	const std::string last_line = (last_nl == std::string::npos) ? tail : tail.substr(last_nl + 1);

	const size_t tp = last_line.rfind("\"type\":\"");
	if (tp != std::string::npos) {
		const size_t start = tp + 8;
		const size_t end = last_line.find('"', start);
		last_entry_type_ = last_line.substr(start, end - start);
	}

	// Latest stop_reason found anywhere in the tail window.
	const size_t sp = tail.rfind("\"stop_reason\":\"");
	if (sp != std::string::npos) {
		const size_t start = sp + 15;
		const size_t end = tail.find('"', start);
		last_stop_reason_ = tail.substr(start, end - start);
	}

	// Latest assistant-text content item. Scan each line; within each
	// assistant line take the LAST `{"type":"text","text":"..."}` item so
	// we pick the final text block when Claude interleaves text and tool_use.
	constexpr const char* TEXT_ITEM_MARKER = "\"type\":\"text\",\"text\":\"";
	constexpr size_t TEXT_ITEM_MARKER_LEN = 22;
	size_t cursor = 0;
	while (cursor < tail.size()) {
		const size_t nl = tail.find('\n', cursor);
		const std::string line = tail.substr(cursor,
			(nl == std::string::npos ? tail.size() : nl) - cursor);
		cursor = (nl == std::string::npos) ? tail.size() : nl + 1;

		if (line.find("\"type\":\"assistant\"") == std::string::npos) continue;

		const size_t tp = line.rfind(TEXT_ITEM_MARKER);
		if (tp == std::string::npos) continue;
		const size_t str_start = tp + TEXT_ITEM_MARKER_LEN;
		const size_t str_end = find_json_string_end(line, str_start);
		if (str_end == std::string::npos) continue;

		last_assistant_text_ = unescape_json(line.substr(str_start, str_end - str_start));
	}

	// Token counts: find usage{} blocks in the tail, keep the last
	// input_tokens we see, accumulate output_tokens across writes.
	const size_t up = last_line.find("\"usage\":");
	if (up != std::string::npos) {
		auto parse_int = [&](const std::string& key) -> int {
			const size_t kp = last_line.find("\"" + key + "\":", up);
			if (kp == std::string::npos) return 0;
			return std::atoi(last_line.c_str() + kp + key.size() + 3);
		};
		const int in_t = parse_int("input_tokens");
		const int out_t = parse_int("output_tokens");
		if (in_t) input_tokens_ = in_t;
		if (out_t) output_tokens_ += out_t;
	}
}

}
