#pragma once

#include "AgentKind.hpp"

#include <string>

namespace ch {

// Per-kind strategy object, composed into Agent. Encapsulates everything that
// varies across Claude / Copilot / Gemini: the CLI command line, the tab-name
// prefix, and whether the kind exposes Claude-style pid.json + JSONL telemetry
// that the rest of the app can tail.
class IAgentDriver {
public:
	virtual ~IAgentDriver() = default;

	virtual AgentKind   kind() const = 0;
	virtual const char* name_prefix() const = 0;

	// Returns the command that follows `wt.exe --title "..." [-d "<cwd>"] -- `.
	// Must be a complete shell-ready string (may include arguments).
	virtual std::string build_command() const = 0;

	// True when the kind produces ~/.claude/sessions/<pid>.json and a JSONL
	// conversation file we can tail for waiting / preview / titles.
	virtual bool uses_claude_telemetry() const = 0;
};

// Returns a process-wide singleton driver for the given kind. Safe to call
// from any thread; the returned reference is stable for the lifetime of the
// process.
IAgentDriver& get_driver(AgentKind kind);

}
