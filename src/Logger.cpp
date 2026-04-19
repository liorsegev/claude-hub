#include "Logger.hpp"

#include <cstdarg>
#include <share.h>

namespace ch {

Logger::Logger(const std::filesystem::path& path) {
	fp_ = _fsopen(path.string().c_str(), "w", _SH_DENYWR);
}

Logger::~Logger() {
	if (fp_) std::fclose(fp_);
}

void Logger::logf(const char* fmt, ...) {
	if (!fp_) return;
	std::va_list ap;
	va_start(ap, fmt);
	std::vfprintf(fp_, fmt, ap);
	va_end(ap);
	std::fflush(fp_);
}

}
