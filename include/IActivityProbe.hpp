#pragma once

#include <chrono>
#include <string>

namespace ch {

// Abstract interface for detecting whether an agent is actively doing work.
// Current implementation: JsonlActivityProbe (tails the Claude conversation JSONL).
// Future implementations could probe process CPU, stdout captures, etc.
class IActivityProbe {
public:
	virtual ~IActivityProbe() = default;

	// Poll the underlying source; update internal state.
	virtual void poll(std::chrono::steady_clock::time_point now) = 0;

	// Rolling metadata useful for UI display and waiting logic.
	virtual const std::string& last_entry_type() const = 0;
	virtual const std::string& last_stop_reason() const = 0;
	virtual const std::string& last_assistant_text() const = 0;
	virtual const std::string& conversation_title() const = 0;
	virtual int input_tokens() const = 0;
	virtual int output_tokens() const = 0;
};

}
