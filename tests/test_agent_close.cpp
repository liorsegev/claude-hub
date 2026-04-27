#include "Agent.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <gtest/gtest.h>

#include <chrono>
#include <set>
#include <string>

using ch::Agent;
using ch::AgentKind;

namespace {

// Spawns a real long-lived Windows process and hands it back. We use ping
// against localhost rather than a sleep so this works on every Windows SKU
// without relying on PowerShell or .NET being present.
struct OwnedProcess {
	PROCESS_INFORMATION info{};

	OwnedProcess() {
		// 30 seconds of pings — far longer than the test should ever need to
		// wait, so we can be sure that if the process exits within the test
		// window it was Agent::close() that did it.
		std::string cmd = "cmd.exe /c ping -n 30 127.0.0.1 > NUL";
		STARTUPINFOA si{};
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;
		const BOOL ok = CreateProcessA(nullptr, cmd.data(),
			nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
			nullptr, nullptr, &si, &info);
		if (!ok) info = {};
	}

	~OwnedProcess() {
		if (info.hProcess) {
			TerminateProcess(info.hProcess, 0);
			CloseHandle(info.hProcess);
			CloseHandle(info.hThread);
		}
	}

	bool valid() const { return info.hProcess != nullptr; }
	unsigned int pid() const { return info.dwProcessId; }
	HANDLE handle() const { return info.hProcess; }
};

// Builds an Agent that owns the given pid as its `claude_pid` but has no wt
// window. Agent::close() must skip window operations gracefully when window_
// is null — that's exercised here too.
Agent make_agent_for_pid(unsigned int pid) {
	return Agent(
		AgentKind::Claude,
		"test_agent",
		/*wt_window=*/nullptr,
		/*wt_process=*/nullptr,
		pid,
		/*cwd=*/"",
		/*jsonl_snapshot=*/std::set<std::string>{},
		std::chrono::steady_clock::now());
}

}

TEST(AgentClose, TerminatesClaudePidWithinTimeout) {
	OwnedProcess proc;
	ASSERT_TRUE(proc.valid()) << "could not spawn ping for test fixture";

	// Sanity: the process must still be running when we hand it to Agent.
	ASSERT_EQ(WaitForSingleObject(proc.handle(), 0), WAIT_TIMEOUT);

	{
		Agent a = make_agent_for_pid(proc.pid());
		a.close();
	}

	// TerminateProcess is asynchronous; give the kernel up to 2s to reap.
	// In practice this fires in a few ms.
	const DWORD wait = WaitForSingleObject(proc.handle(), 2000);
	EXPECT_EQ(wait, WAIT_OBJECT_0)
		<< "child process was not terminated by Agent::close()";
}

TEST(AgentClose, IsIdempotent) {
	OwnedProcess proc;
	ASSERT_TRUE(proc.valid());

	Agent a = make_agent_for_pid(proc.pid());
	a.close();
	a.close();  // second call must not crash even though pid was zeroed out.
	a.close();
	SUCCEED();
}

TEST(AgentClose, SafeOnAgentWithoutKnownInnerPid) {
	// Copilot/Gemini agents (and Claude before its probe stage commits) have
	// claude_pid == 0. close() must still be safe — no termination work to do,
	// no window to touch.
	Agent a(
		AgentKind::Copilot,
		"copilot_agent",
		/*wt_window=*/nullptr,
		/*wt_process=*/nullptr,
		/*claude_pid=*/0,
		/*cwd=*/"",
		/*jsonl_snapshot=*/std::set<std::string>{},
		std::chrono::steady_clock::now());
	a.close();
	SUCCEED();
}

TEST(AgentClose, DestructorCallsCloseImplicitly) {
	// The whole point of folding close() into ~Agent is that
	// agents_.erase(...) in AgentManager::kill is enough to clean up. Verify
	// that path by ONLY destroying the Agent (no manual close()) and asserting
	// the child still gets terminated.
	OwnedProcess proc;
	ASSERT_TRUE(proc.valid());

	{
		Agent a = make_agent_for_pid(proc.pid());
		// destructor runs as `a` leaves scope — no explicit close()
	}

	const DWORD wait = WaitForSingleObject(proc.handle(), 2000);
	EXPECT_EQ(wait, WAIT_OBJECT_0)
		<< "child process was not terminated by ~Agent";
}
