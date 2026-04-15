#include "conpty.hpp"
#include <vector>

namespace claude_hub {

ConPTY::~ConPTY() {
	kill();
}

bool ConPTY::spawn(const std::string& command, const std::string& cwd, int cols, int rows) {
	// Create pipes for PTY I/O
	HANDLE pipe_pty_in = INVALID_HANDLE_VALUE;
	HANDLE pipe_pty_out = INVALID_HANDLE_VALUE;

	if (!CreatePipe(&pipe_pty_in, &pipe_in_, nullptr, 0))
		return false;
	if (!CreatePipe(&pipe_out_, &pipe_pty_out, nullptr, 0)) {
		CloseHandle(pipe_pty_in);
		CloseHandle(pipe_in_);
		return false;
	}

	// Create the pseudo-console
	COORD size{static_cast<SHORT>(cols), static_cast<SHORT>(rows)};
	HRESULT hr = CreatePseudoConsole(size, pipe_pty_in, pipe_pty_out, 0, &hpc_);

	// Close the PTY-side pipe handles (ConPTY owns them now)
	CloseHandle(pipe_pty_in);
	CloseHandle(pipe_pty_out);

	if (FAILED(hr))
		return false;

	// Set up startup info with the pseudo-console attribute
	STARTUPINFOEXW si{};
	si.StartupInfo.cb = sizeof(si);

	SIZE_T attr_size = 0;
	InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
	std::vector<BYTE> attr_buf(attr_size);
	si.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_buf.data());

	if (!InitializeProcThreadAttributeList(si.lpAttributeList, 1, 0, &attr_size))
		return false;

	if (!UpdateProcThreadAttribute(
			si.lpAttributeList, 0,
			PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
			hpc_, sizeof(hpc_), nullptr, nullptr))
		return false;

	// Convert strings to wide chars
	auto to_wide = [](const std::string& s) -> std::wstring {
		if (s.empty()) return {};
		int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
		std::wstring ws(len, 0);
		MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, ws.data(), len);
		return ws;
	};

	std::wstring wcmd = to_wide(command);
	std::wstring wcwd = to_wide(cwd);

	// Create the process
	BOOL ok = CreateProcessW(
		nullptr,
		wcmd.data(),
		nullptr, nullptr,
		FALSE,
		EXTENDED_STARTUPINFO_PRESENT,
		nullptr,
		wcwd.empty() ? nullptr : wcwd.c_str(),
		&si.StartupInfo,
		&process_info_
	);

	DeleteProcThreadAttributeList(si.lpAttributeList);

	if (!ok)
		return false;

	alive_ = true;
	return true;
}

bool ConPTY::write(const char* data, size_t len) {
	if (pipe_in_ == INVALID_HANDLE_VALUE)
		return false;
	DWORD written = 0;
	return WriteFile(pipe_in_, data, static_cast<DWORD>(len), &written, nullptr) != 0;
}

bool ConPTY::write(const std::string& data) {
	return write(data.c_str(), data.size());
}

void ConPTY::start_reader(std::function<void(const char*, size_t)> callback) {
	reader_thread_ = std::thread([this, cb = std::move(callback)]() {
		char buf[4096];
		while (alive_) {
			DWORD bytes_read = 0;
			BOOL ok = ReadFile(pipe_out_, buf, sizeof(buf), &bytes_read, nullptr);
			if (!ok || bytes_read == 0) {
				alive_ = false;
				break;
			}
			cb(buf, bytes_read);
		}
	});
}

void ConPTY::resize(int cols, int rows) {
	if (hpc_) {
		COORD size{static_cast<SHORT>(cols), static_cast<SHORT>(rows)};
		ResizePseudoConsole(hpc_, size);
	}
}

void ConPTY::kill() {
	alive_ = false;

	// Terminate the process
	if (process_info_.hProcess) {
		TerminateProcess(process_info_.hProcess, 0);
		WaitForSingleObject(process_info_.hProcess, 1000);
	}

	close_handles();

	if (reader_thread_.joinable())
		reader_thread_.join();
}

bool ConPTY::is_alive() const {
	if (!alive_) return false;
	if (!process_info_.hProcess) return false;
	DWORD exit_code = 0;
	if (GetExitCodeProcess(process_info_.hProcess, &exit_code))
		return exit_code == STILL_ACTIVE;
	return false;
}

void ConPTY::close_handles() {
	if (hpc_) {
		ClosePseudoConsole(hpc_);
		hpc_ = nullptr;
	}
	if (pipe_in_ != INVALID_HANDLE_VALUE) {
		CloseHandle(pipe_in_);
		pipe_in_ = INVALID_HANDLE_VALUE;
	}
	if (pipe_out_ != INVALID_HANDLE_VALUE) {
		CloseHandle(pipe_out_);
		pipe_out_ = INVALID_HANDLE_VALUE;
	}
	if (process_info_.hProcess) {
		CloseHandle(process_info_.hProcess);
		process_info_.hProcess = nullptr;
	}
	if (process_info_.hThread) {
		CloseHandle(process_info_.hThread);
		process_info_.hThread = nullptr;
	}
}

} // namespace claude_hub
