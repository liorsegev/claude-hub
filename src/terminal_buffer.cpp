#include "terminal_buffer.hpp"
#include <algorithm>

namespace claude_hub {

TerminalBuffer::TerminalBuffer(int cols, int rows)
	: cols_(cols), rows_(rows) {
	buffer_.resize(rows, std::vector<Cell>(cols));
}

void TerminalBuffer::feed(const char* data, size_t len) {
	std::lock_guard lock(mutex_);
	for (size_t i = 0; i < len; ++i) {
		process_byte(static_cast<uint8_t>(data[i]));
	}
	dirty_ = true;
}

bool TerminalBuffer::check_dirty() {
	return dirty_.exchange(false);
}

std::vector<std::vector<Cell>> TerminalBuffer::snapshot() const {
	std::lock_guard lock(mutex_);
	return buffer_;
}

static void encode_utf8(std::string& out, char32_t ch) {
	if (ch < 0x80) {
		out += static_cast<char>(ch >= 0x20 ? ch : ' ');
	} else if (ch < 0x800) {
		out += static_cast<char>(0xC0 | (ch >> 6));
		out += static_cast<char>(0x80 | (ch & 0x3F));
	} else if (ch < 0x10000) {
		out += static_cast<char>(0xE0 | (ch >> 12));
		out += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (ch & 0x3F));
	} else {
		out += static_cast<char>(0xF0 | (ch >> 18));
		out += static_cast<char>(0x80 | ((ch >> 12) & 0x3F));
		out += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (ch & 0x3F));
	}
}

std::vector<std::string> TerminalBuffer::text_lines() const {
	std::lock_guard lock(mutex_);
	std::vector<std::string> result;
	result.reserve(rows_);
	for (int r = 0; r < rows_; ++r) {
		std::string line;
		line.reserve(cols_);
		for (int c = 0; c < cols_; ++c) {
			encode_utf8(line, buffer_[r][c].ch);
		}
		result.push_back(std::move(line));
	}
	return result;
}

std::vector<std::string> TerminalBuffer::preview_lines(int n) const {
	std::lock_guard lock(mutex_);
	std::vector<std::string> result;

	int last_nonempty = -1;
	for (int r = rows_ - 1; r >= 0; --r) {
		for (int c = 0; c < cols_; ++c) {
			if (buffer_[r][c].ch != ' ') {
				last_nonempty = r;
				goto found;
			}
		}
	}
found:
	if (last_nonempty < 0) {
		result.resize(n, "");
		return result;
	}

	int start = std::max(0, last_nonempty - n + 1);
	for (int r = start; r <= last_nonempty; ++r) {
		std::string line;
		for (int c = 0; c < cols_; ++c) {
			encode_utf8(line, buffer_[r][c].ch);
		}
		// Trim trailing spaces
		while (!line.empty() && line.back() == ' ')
			line.pop_back();
		result.push_back(std::move(line));
	}

	return result;
}

void TerminalBuffer::resize(int cols, int rows) {
	std::lock_guard lock(mutex_);
	cols_ = cols;
	rows_ = rows;
	buffer_.resize(rows);
	for (auto& row : buffer_)
		row.resize(cols);
	cursor_row_ = std::min(cursor_row_, rows - 1);
	cursor_col_ = std::min(cursor_col_, cols - 1);
}

// ─── Byte-level processing with UTF-8 decoding ───

void TerminalBuffer::process_byte(uint8_t b) {
	// If we're in the middle of a UTF-8 sequence
	if (state_ == State::UTF8) {
		if ((b & 0xC0) == 0x80) {
			utf8_codepoint_ = (utf8_codepoint_ << 6) | (b & 0x3F);
			--utf8_remaining_;
			if (utf8_remaining_ == 0) {
				state_ = State::NORMAL;
				put_char(utf8_codepoint_);
			}
			return;
		}
		// Invalid continuation — reset and process this byte normally
		state_ = State::NORMAL;
		utf8_remaining_ = 0;
	}

	// Handle current parser state
	switch (state_) {
	case State::NORMAL: {
		if (b == 0x1B) { // ESC
			state_ = State::ESCAPE;
			csi_params_.clear();
		} else if (b == '\r') {
			cursor_col_ = 0;
		} else if (b == '\n') {
			if (cursor_row_ < rows_ - 1)
				++cursor_row_;
			else
				scroll_up();
		} else if (b == '\t') {
			cursor_col_ = std::min(cols_ - 1, (cursor_col_ / 8 + 1) * 8);
		} else if (b == '\b') {
			if (cursor_col_ > 0) --cursor_col_;
		} else if (b == 0x07) {
			// Bell — ignore
		} else if (b >= 0x20 && b < 0x80) {
			// Printable ASCII
			put_char(static_cast<char32_t>(b));
		} else if ((b & 0xE0) == 0xC0) {
			// UTF-8 2-byte start
			utf8_codepoint_ = b & 0x1F;
			utf8_remaining_ = 1;
			state_ = State::UTF8;
		} else if ((b & 0xF0) == 0xE0) {
			// UTF-8 3-byte start
			utf8_codepoint_ = b & 0x0F;
			utf8_remaining_ = 2;
			state_ = State::UTF8;
		} else if ((b & 0xF8) == 0xF0) {
			// UTF-8 4-byte start
			utf8_codepoint_ = b & 0x07;
			utf8_remaining_ = 3;
			state_ = State::UTF8;
		}
		break;
	}
	case State::ESCAPE:
		process_escape(b);
		break;
	case State::CSI:
	case State::CSI_PARAM:
		process_csi(b);
		break;
	case State::OSC:
	case State::OSC_STRING:
		process_osc(b);
		break;
	default:
		break;
	}
}

void TerminalBuffer::put_char(char32_t ch) {
	if (cursor_col_ >= cols_) {
		cursor_col_ = 0;
		if (cursor_row_ < rows_ - 1)
			++cursor_row_;
		else
			scroll_up();
	}
	auto& cell = buffer_[cursor_row_][cursor_col_];
	cell.ch = ch;
	cell.fg = current_attr_.fg;
	cell.bg = current_attr_.bg;
	cell.bold = current_attr_.bold;
	cell.italic = current_attr_.italic;
	cell.underline = current_attr_.underline;
	++cursor_col_;
}

void TerminalBuffer::process_escape(uint8_t b) {
	switch (b) {
	case '[':
		state_ = State::CSI;
		csi_params_.clear();
		break;
	case ']':
		state_ = State::OSC;
		osc_string_.clear();
		break;
	case '(': case ')': case '*': case '+':
	case '>': case '=': case 'c':
		state_ = State::NORMAL;
		break;
	case 'M': // Reverse index
		if (cursor_row_ > 0) --cursor_row_;
		state_ = State::NORMAL;
		break;
	case '7': // Save cursor
	case '8': // Restore cursor
		state_ = State::NORMAL;
		break;
	default:
		state_ = State::NORMAL;
		break;
	}
}

void TerminalBuffer::process_csi(uint8_t b) {
	if ((b >= '0' && b <= '9') || b == ';' || b == '?' || b == '>' || b == '!' || b == ':') {
		csi_params_ += static_cast<char>(b);
		state_ = State::CSI_PARAM;
		return;
	}
	if (b == ' ' || b == '"' || b == '\'') {
		// Intermediate bytes — collect but mostly ignore
		csi_params_ += static_cast<char>(b);
		return;
	}
	// Final byte — execute command
	execute_csi(static_cast<char>(b));
	state_ = State::NORMAL;
}

void TerminalBuffer::execute_csi(char cmd) {
	// Parse semicolon-separated params
	std::vector<int> params;
	std::string current;
	for (char c : csi_params_) {
		if (c == ';' || c == ':') {
			params.push_back(current.empty() ? 0 : std::stoi(current));
			current.clear();
		} else if (c >= '0' && c <= '9') {
			current += c;
		}
	}
	if (!current.empty())
		params.push_back(std::stoi(current));

	auto param = [&](int idx, int def = 1) -> int {
		return (idx < static_cast<int>(params.size()) && params[idx] > 0) ? params[idx] : def;
	};

	switch (cmd) {
	case 'A': cursor_row_ = std::max(0, cursor_row_ - param(0)); break;
	case 'B': cursor_row_ = std::min(rows_ - 1, cursor_row_ + param(0)); break;
	case 'C': cursor_col_ = std::min(cols_ - 1, cursor_col_ + param(0)); break;
	case 'D': cursor_col_ = std::max(0, cursor_col_ - param(0)); break;
	case 'E':
		cursor_row_ = std::min(rows_ - 1, cursor_row_ + param(0));
		cursor_col_ = 0;
		break;
	case 'F':
		cursor_row_ = std::max(0, cursor_row_ - param(0));
		cursor_col_ = 0;
		break;
	case 'G':
		cursor_col_ = std::clamp(param(0) - 1, 0, cols_ - 1);
		break;
	case 'H': case 'f':
		cursor_row_ = std::clamp(param(0) - 1, 0, rows_ - 1);
		cursor_col_ = std::clamp(param(1, 1) - 1, 0, cols_ - 1);
		break;
	case 'J': erase_in_display(param(0, 0)); break;
	case 'K': erase_in_line(param(0, 0)); break;
	case 'L': {
		int n = param(0);
		for (int i = 0; i < n && cursor_row_ + i < rows_; ++i) {
			buffer_.insert(buffer_.begin() + cursor_row_, std::vector<Cell>(cols_));
			buffer_.pop_back();
		}
		break;
	}
	case 'M': {
		int n = param(0);
		for (int i = 0; i < n && cursor_row_ < rows_; ++i) {
			buffer_.erase(buffer_.begin() + cursor_row_);
			buffer_.emplace_back(cols_);
		}
		break;
	}
	case 'P': {
		int n = std::min(param(0), cols_ - cursor_col_);
		auto& row = buffer_[cursor_row_];
		row.erase(row.begin() + cursor_col_, row.begin() + cursor_col_ + n);
		row.resize(cols_);
		break;
	}
	case 'm': // SGR
		if (params.empty()) params.push_back(0);
		apply_sgr(params);
		break;
	case 'd': // Vertical position absolute
		cursor_row_ = std::clamp(param(0) - 1, 0, rows_ - 1);
		break;
	case 'S': {
		int n = param(0);
		for (int i = 0; i < n; ++i) scroll_up();
		break;
	}
	case 'T': {
		int n = param(0);
		for (int i = 0; i < n; ++i) {
			buffer_.insert(buffer_.begin(), std::vector<Cell>(cols_));
			buffer_.pop_back();
		}
		break;
	}
	case '@': {
		int n = std::min(param(0), cols_ - cursor_col_);
		auto& row = buffer_[cursor_row_];
		row.insert(row.begin() + cursor_col_, n, Cell{});
		row.resize(cols_);
		break;
	}
	case 'X': {
		int n = std::min(param(0), cols_ - cursor_col_);
		for (int i = 0; i < n; ++i)
			buffer_[cursor_row_][cursor_col_ + i] = Cell{};
		break;
	}
	default:
		break; // h, l, r, t, n, c, u — mode sets, queries — ignore
	}
}

void TerminalBuffer::apply_sgr(const std::vector<int>& params) {
	size_t i = 0;
	while (i < params.size()) {
		int p = params[i];
		switch (p) {
		case 0: // Reset
			current_attr_ = Cell{};
			break;
		case 1: current_attr_.bold = true; break;
		case 3: current_attr_.italic = true; break;
		case 4: current_attr_.underline = true; break;
		case 22: current_attr_.bold = false; break;
		case 23: current_attr_.italic = false; break;
		case 24: current_attr_.underline = false; break;
		case 30: case 31: case 32: case 33:
		case 34: case 35: case 36: case 37:
			current_attr_.fg = TermColor::make_indexed(static_cast<uint8_t>(p - 30));
			break;
		case 38: // Extended foreground
			if (i + 1 < params.size()) {
				if (params[i + 1] == 5 && i + 2 < params.size()) {
					// 256-color: 38;5;N
					current_attr_.fg = TermColor::make_indexed(static_cast<uint8_t>(params[i + 2]));
					i += 2;
				} else if (params[i + 1] == 2 && i + 4 < params.size()) {
					// True-color: 38;2;R;G;B
					current_attr_.fg = TermColor::make_rgb(
						static_cast<uint8_t>(params[i + 2]),
						static_cast<uint8_t>(params[i + 3]),
						static_cast<uint8_t>(params[i + 4])
					);
					i += 4;
				}
			}
			break;
		case 39: // Default fg
			current_attr_.fg = TermColor::make_indexed(7);
			break;
		case 40: case 41: case 42: case 43:
		case 44: case 45: case 46: case 47:
			current_attr_.bg = TermColor::make_indexed(static_cast<uint8_t>(p - 40));
			break;
		case 48: // Extended background
			if (i + 1 < params.size()) {
				if (params[i + 1] == 5 && i + 2 < params.size()) {
					current_attr_.bg = TermColor::make_indexed(static_cast<uint8_t>(params[i + 2]));
					i += 2;
				} else if (params[i + 1] == 2 && i + 4 < params.size()) {
					current_attr_.bg = TermColor::make_rgb(
						static_cast<uint8_t>(params[i + 2]),
						static_cast<uint8_t>(params[i + 3]),
						static_cast<uint8_t>(params[i + 4])
					);
					i += 4;
				}
			}
			break;
		case 49: // Default bg
			current_attr_.bg = TermColor::make_default();
			break;
		case 90: case 91: case 92: case 93:
		case 94: case 95: case 96: case 97:
			current_attr_.fg = TermColor::make_indexed(static_cast<uint8_t>(p - 90 + 8));
			break;
		case 100: case 101: case 102: case 103:
		case 104: case 105: case 106: case 107:
			current_attr_.bg = TermColor::make_indexed(static_cast<uint8_t>(p - 100 + 8));
			break;
		}
		++i;
	}
}

void TerminalBuffer::process_osc(uint8_t b) {
	if (b == 0x07 || b == 0x1B) {
		// OSC terminated by BEL or ESC (ST will follow but we'll handle in NORMAL)
		state_ = State::NORMAL;
	} else {
		osc_string_ += static_cast<char>(b);
		state_ = State::OSC_STRING;
	}
}

void TerminalBuffer::scroll_up() {
	buffer_.erase(buffer_.begin());
	buffer_.emplace_back(cols_);
}

void TerminalBuffer::erase_in_display(int mode) {
	switch (mode) {
	case 0:
		for (int c = cursor_col_; c < cols_; ++c)
			buffer_[cursor_row_][c] = Cell{};
		for (int r = cursor_row_ + 1; r < rows_; ++r)
			std::fill(buffer_[r].begin(), buffer_[r].end(), Cell{});
		break;
	case 1:
		for (int r = 0; r < cursor_row_; ++r)
			std::fill(buffer_[r].begin(), buffer_[r].end(), Cell{});
		for (int c = 0; c <= cursor_col_ && c < cols_; ++c)
			buffer_[cursor_row_][c] = Cell{};
		break;
	case 2: case 3:
		for (auto& row : buffer_)
			std::fill(row.begin(), row.end(), Cell{});
		break;
	}
}

void TerminalBuffer::erase_in_line(int mode) {
	auto& row = buffer_[cursor_row_];
	switch (mode) {
	case 0:
		for (int c = cursor_col_; c < cols_; ++c) row[c] = Cell{};
		break;
	case 1:
		for (int c = 0; c <= cursor_col_ && c < cols_; ++c) row[c] = Cell{};
		break;
	case 2:
		std::fill(row.begin(), row.end(), Cell{});
		break;
	}
}

} // namespace claude_hub
