#include "WindowsTerminalSpawner.hpp"
#include "Constants.hpp"

namespace ch {

namespace {

struct EnumData {
	std::vector<HWND>* list;
};

BOOL CALLBACK collect_windows_proc(HWND h, LPARAM lp) {
	if (GetWindow(h, GW_OWNER) != nullptr) return TRUE;
	wchar_t cls[64] = {};
	GetClassNameW(h, cls, 63);
	if (wcsstr(cls, L"CASCADIA") || wcsstr(cls, L"ConsoleWindow") || IsWindowVisible(h))
		reinterpret_cast<EnumData*>(lp)->list->push_back(h);
	return TRUE;
}

bool is_cascadia_or_console(HWND h) {
	wchar_t cls[64] = {};
	GetClassNameW(h, cls, 63);
	return wcsstr(cls, L"CASCADIA") != nullptr || wcsstr(cls, L"ConsoleWindow") != nullptr;
}

}

std::vector<HWND> WindowsTerminalSpawner::enumerate_candidate_windows() {
	std::vector<HWND> list;
	EnumData data{&list};
	EnumWindows(collect_windows_proc, reinterpret_cast<LPARAM>(&data));
	return list;
}

WindowsTerminalSpawner::SpawnResult
WindowsTerminalSpawner::spawn(const std::string& title,
                              const std::string& claude_exe_path,
                              const std::vector<HWND>& before_snapshot) {
	SpawnResult result;

	std::string cmd = "wt.exe -w -1 --title \"" + title + "\" -- " + claude_exe_path;

	STARTUPINFOA si{};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi{};
	if (!CreateProcessA(nullptr, const_cast<char*>(cmd.c_str()),
		nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
		return result;
	}
	WaitForSingleObject(pi.hProcess, constants::WT_SPAWN_WAIT_MS);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	for (int attempt = 0; attempt < constants::WT_WINDOW_POLL_ATTEMPTS; ++attempt) {
		std::vector<HWND> now = enumerate_candidate_windows();
		for (HWND h : now) {
			bool was_before = false;
			for (HWND b : before_snapshot) {
				if (b == h) { was_before = true; break; }
			}
			if (was_before) continue;
			if (!is_cascadia_or_console(h)) continue;

			DWORD pid = 0;
			GetWindowThreadProcessId(h, &pid);
			result.window = h;
			result.pid = pid;
			result.process_handle = OpenProcess(
				SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
			return result;
		}
		Sleep(constants::WT_POLL_SLEEP_MS);
	}
	return result;
}

}
