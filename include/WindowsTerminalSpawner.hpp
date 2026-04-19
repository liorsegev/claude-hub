#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>

namespace ch {

// Stateless helper: runs wt.exe and finds the CASCADIA_HOSTING_WINDOW that
// appeared as a result. No instance state needed.
class WindowsTerminalSpawner {
public:
	struct SpawnResult {
		HWND window = nullptr;
		HANDLE process_handle = nullptr;
		unsigned int pid = 0;
	};

	WindowsTerminalSpawner() = delete;

	static std::vector<HWND> enumerate_candidate_windows();

	static SpawnResult spawn(const std::string& title,
	                         const std::string& claude_exe_path,
	                         const std::vector<HWND>& before_snapshot);
};

}
