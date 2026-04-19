#pragma once

#include "IActivityProbe.hpp"
#include "Logger.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

namespace ch {

class JsonlActivityProbe : public IActivityProbe {
public:
	JsonlActivityProbe(std::filesystem::path jsonl_path,
	                   std::string owner_name,
	                   Logger* log);

	void poll(std::chrono::steady_clock::time_point now) override;
	const std::string& last_entry_type() const override { return last_entry_type_; }
	const std::string& last_stop_reason() const override { return last_stop_reason_; }
	const std::string& last_assistant_text() const override { return last_assistant_text_; }
	const std::string& conversation_title() const override { return conversation_title_; }
	int input_tokens() const override { return input_tokens_; }
	int output_tokens() const override { return output_tokens_; }

	const std::filesystem::path& path() const { return path_; }

private:
	void parse_tail(const std::string& tail);
	void try_extract_title();

	std::filesystem::path path_;
	std::string owner_name_;
	Logger* log_ = nullptr;

	std::uint64_t last_size_ = 0;
	std::string last_entry_type_;
	std::string last_stop_reason_;
	std::string last_assistant_text_;
	std::string conversation_title_;
	int input_tokens_ = 0;
	int output_tokens_ = 0;
};

}
