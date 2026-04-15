#pragma once

// Require Windows 10 1809+ for ConPTY support
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000006
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <functional>
#include <thread>
#include <atomic>

namespace claude_hub {

/// Wraps a Windows ConPTY (pseudo-console) with pipe-based I/O.
class ConPTY {
public:
	ConPTY() = default;
	~ConPTY();

	ConPTY(const ConPTY&) = delete;
	ConPTY& operator=(const ConPTY&) = delete;

	/// Spawn a process inside the ConPTY.
	/// @param command  Full command line (e.g. path to claude.exe)
	/// @param cwd      Working directory for the process
	/// @param cols     Terminal width
	/// @param rows     Terminal height
	/// @return true on success
	bool spawn(const std::string& command, const std::string& cwd, int cols, int rows);

	/// Write raw bytes to the ConPTY input (keypresses).
	bool write(const char* data, size_t len);
	bool write(const std::string& data);

	/// Start a background thread that reads ConPTY output and calls `callback`.
	/// The callback receives a pointer and length of raw bytes.
	void start_reader(std::function<void(const char*, size_t)> callback);

	/// Resize the pseudo-console.
	void resize(int cols, int rows);

	/// Send Ctrl+C and close handles.
	void kill();

	/// Check if the spawned process is still running.
	bool is_alive() const;

	/// Get the process ID of the spawned process.
	DWORD pid() const { return process_info_.dwProcessId; }

private:
	void close_handles();

	HPCON hpc_ = nullptr;
	HANDLE pipe_in_ = INVALID_HANDLE_VALUE;   // We write to this -> PTY stdin
	HANDLE pipe_out_ = INVALID_HANDLE_VALUE;  // We read from this <- PTY stdout
	PROCESS_INFORMATION process_info_{};
	std::thread reader_thread_;
	std::atomic<bool> alive_{false};
};

} // namespace claude_hub
