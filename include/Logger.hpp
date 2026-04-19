#pragma once

#include <cstdio>
#include <filesystem>

namespace ch {

class Logger {
public:
	explicit Logger(const std::filesystem::path& path);
	~Logger();

	Logger(const Logger&) = delete;
	Logger& operator=(const Logger&) = delete;

	void logf(const char* fmt, ...);

private:
	std::FILE* fp_ = nullptr;
};

}
