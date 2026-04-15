#pragma once

#include <string>
#include <cstdlib>

namespace claude_hub {

constexpr int DEFAULT_COLS = 120;
constexpr int DEFAULT_ROWS = 40;
constexpr int MAX_SIDE_PANELS = 3;
constexpr int SIDE_PANEL_PREVIEW_LINES = 10;
constexpr double INPUT_QUIESCENCE_SECONDS = 2.0;

inline std::string claude_exe_path() {
	const char* home = std::getenv("USERPROFILE");
	if (!home) home = "C:\\Users\\liors";
	return std::string(home) + "\\.local\\bin\\claude.exe";
}

} // namespace claude_hub
