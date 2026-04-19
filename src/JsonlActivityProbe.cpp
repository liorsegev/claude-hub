#include "JsonlActivityProbe.hpp"
#include "Constants.hpp"

#include <cstdlib>
#include <fstream>
#include <system_error>

namespace ch {

namespace fs = std::filesystem;

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
