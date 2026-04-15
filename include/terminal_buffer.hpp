#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>

namespace claude_hub {

/// Color representation supporting default, 16-color, 256-color, and true-color.
struct TermColor {
	enum class Type : uint8_t { DEFAULT, INDEXED, RGB };
	Type type = Type::DEFAULT;
	uint8_t index = 0;       // For INDEXED (0-255)
	uint8_t r = 0, g = 0, b = 0; // For RGB

	static TermColor make_default() { return {}; }
	static TermColor make_indexed(uint8_t idx) { return {Type::INDEXED, idx, 0, 0, 0}; }
	static TermColor make_rgb(uint8_t r, uint8_t g, uint8_t b) { return {Type::RGB, 0, r, g, b}; }
};

/// A single character cell in the terminal buffer.
struct Cell {
	char32_t ch = ' ';
	TermColor fg = TermColor::make_indexed(7); // Default white
	TermColor bg = TermColor::make_default();  // Default background
	bool bold = false;
	bool italic = false;
	bool underline = false;
};

/// Thread-safe virtual terminal buffer with VT100/xterm sequence parsing.
class TerminalBuffer {
public:
	TerminalBuffer(int cols, int rows);

	/// Feed raw bytes from ConPTY output. Thread-safe.
	void feed(const char* data, size_t len);

	/// Get a full cell snapshot for rendering. Thread-safe. EXPENSIVE.
	std::vector<std::vector<Cell>> snapshot() const;

	/// Get all lines as plain UTF-8 strings. Thread-safe. FAST.
	std::vector<std::string> text_lines() const;

	/// Check and reset dirty flag. Returns true if buffer changed since last check.
	bool check_dirty();

	/// Get a snapshot of the last N non-empty lines as plain text. Thread-safe.
	std::vector<std::string> preview_lines(int n) const;

	/// Resize the buffer.
	void resize(int cols, int rows);

	int cols() const { return cols_; }
	int rows() const { return rows_; }

private:
	enum class State {
		NORMAL,
		ESCAPE,
		CSI,
		CSI_PARAM,
		OSC,
		OSC_STRING,
		UTF8,         // Collecting multi-byte UTF-8 sequence
	};

	void process_byte(uint8_t b);
	void put_char(char32_t ch);
	void process_escape(uint8_t b);
	void process_csi(uint8_t b);
	void execute_csi(char cmd);
	void process_osc(uint8_t b);
	void apply_sgr(const std::vector<int>& params);

	void scroll_up();
	void erase_in_display(int mode);
	void erase_in_line(int mode);

	int cols_;
	int rows_;
	int cursor_row_ = 0;
	int cursor_col_ = 0;
	std::vector<std::vector<Cell>> buffer_;
	mutable std::mutex mutex_;

	State state_ = State::NORMAL;
	std::string csi_params_;
	std::string osc_string_;

	// UTF-8 decoding state
	char32_t utf8_codepoint_ = 0;
	int utf8_remaining_ = 0;

	Cell current_attr_;
	std::atomic<bool> dirty_{false};
};

} // namespace claude_hub
