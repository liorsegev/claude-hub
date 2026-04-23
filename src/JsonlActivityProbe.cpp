#include "JsonlActivityProbe.hpp"
#include "Constants.hpp"
#include "detail/JsonlParsing.hpp"

#include <cstdlib>
#include <fstream>
#include <system_error>

namespace ch {

namespace fs = std::filesystem;

using detail::find_json_string_end;
using detail::unescape_json;

JsonlActivityProbe::JsonlActivityProbe(fs::path jsonl_path,
                                       std::string owner_name,
                                       Logger* log)
	: path_(std::move(jsonl_path))
	, owner_name_(std::move(owner_name))
	, log_(log) {}

void JsonlActivityProbe::poll(std::chrono::steady_clock::time_point /*now*/) {
	std::error_code ec;
	auto fsize = fs::file_size(path_, ec);
	if (ec || fsize == 0 || fsize == last_size_) return;

	last_size_ = fsize;
	if (log_) log_->logf("Agent %s: JSONL grew to %llu bytes\n",
		owner_name_.c_str(), static_cast<unsigned long long>(fsize));

	try_extract_title();

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
	// Walk every line in the tail once. For each line determine:
	//   - root "type" (via rfind — nested "type" keys appear earlier)
	//   - if assistant, its stop_reason and last text-content item
	//   - if it carries a usage{} block, the token counts
	//
	// Claude Code appends a `"type":"system"` turn_duration marker after each
	// assistant turn, so we must ignore non-meaningful types (system /
	// attachment / partial-flush "message") and track the latest assistant
	// or user entry as the "current state" of the conversation.
	constexpr const char* TEXT_ITEM_MARKER = "\"type\":\"text\",\"text\":\"";
	constexpr size_t TEXT_ITEM_MARKER_LEN = 22;

	last_stop_reason_.clear();

	size_t cursor = 0;
	while (cursor < tail.size()) {
		const size_t nl = tail.find('\n', cursor);
		const std::string line = tail.substr(cursor,
			(nl == std::string::npos ? tail.size() : nl) - cursor);
		cursor = (nl == std::string::npos) ? tail.size() : nl + 1;
		if (line.empty()) continue;

		const size_t tp = line.rfind("\"type\":\"");
		if (tp == std::string::npos) continue;
		const size_t ts = tp + 8;
		const size_t te = line.find('"', ts);
		if (te == std::string::npos) continue;
		const std::string line_type = line.substr(ts, te - ts);

		if (line_type != "user" && line_type != "assistant") continue;

		last_entry_type_ = line_type;

		if (line_type != "assistant") {
			last_stop_reason_.clear();
			continue;
		}

		// Assistant line: pull stop_reason, last text content item, usage.
		const size_t stop_bound = line.find("\"stop_reason\":\"");
		if (stop_bound != std::string::npos) {
			const size_t s = stop_bound + 15;
			const size_t e = line.find('"', s);
			last_stop_reason_ = (e == std::string::npos) ? std::string{} : line.substr(s, e - s);
		} else {
			last_stop_reason_.clear();
		}

		const size_t content_end = (stop_bound == std::string::npos) ? line.size() : stop_bound;
		const size_t text_tp = line.rfind(TEXT_ITEM_MARKER, content_end);
		if (text_tp != std::string::npos && text_tp < content_end) {
			const size_t str_start = text_tp + TEXT_ITEM_MARKER_LEN;
			const size_t str_end = find_json_string_end(line, str_start);
			if (str_end != std::string::npos)
				last_assistant_text_ = unescape_json(line.substr(str_start, str_end - str_start));
		}

		const size_t up = line.find("\"usage\":");
		if (up != std::string::npos) {
			auto parse_int = [&](const std::string& key) -> int {
				const size_t kp = line.find("\"" + key + "\":", up);
				if (kp == std::string::npos) return 0;
				return std::atoi(line.c_str() + kp + key.size() + 3);
			};
			const int in_t = parse_int("input_tokens");
			const int out_t = parse_int("output_tokens");
			if (in_t) input_tokens_ = in_t;
			if (out_t) output_tokens_ += out_t;
		}
	}

	if (log_ && !last_assistant_text_.empty()) {
		const size_t preview_n = std::min<size_t>(80, last_assistant_text_.size());
		log_->logf("Agent %s: assistant_text[0..%zu]=\"%.*s\"\n",
			owner_name_.c_str(), preview_n,
			static_cast<int>(preview_n), last_assistant_text_.c_str());
	}
}

void JsonlActivityProbe::try_extract_title() {
	// Read the first few KB and pick a title in priority order:
	//   1. "customTitle"  — written by Claude Code's /rename command (explicit user intent)
	//   2. "agentName"    — written alongside customTitle by /rename
	//   3. first user prompt as `"role":"user","content":"..."` (string content only,
	//                       tool-result user entries use `"content":[...]` so they won't match)
	constexpr size_t HEAD_BYTES = 4096;

	std::ifstream f(path_);
	if (!f) return;
	std::string head(HEAD_BYTES, '\0');
	f.read(&head[0], HEAD_BYTES);
	head.resize(static_cast<size_t>(f.gcount()));

	auto extract = [&](const char* marker, size_t marker_len) -> std::string {
		const size_t pos = head.find(marker);
		if (pos == std::string::npos) return {};
		const size_t str_start = pos + marker_len;
		const size_t str_end = find_json_string_end(head, str_start);
		if (str_end == std::string::npos) return {};
		return unescape_json(head.substr(str_start, str_end - str_start));
	};

	std::string title = extract("\"customTitle\":\"", 15);
	if (title.empty()) title = extract("\"agentName\":\"", 13);
	if (title.empty()) title = extract("\"role\":\"user\",\"content\":\"", 25);

	if (!title.empty() && title != conversation_title_) {
		conversation_title_ = std::move(title);
		if (log_) log_->logf("Agent %s: title=\"%s\"\n",
			owner_name_.c_str(), conversation_title_.c_str());
	}
}

}
