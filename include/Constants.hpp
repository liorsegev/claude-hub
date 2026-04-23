#pragma once

namespace ch::constants {

// ─── UI ───
inline constexpr int SIDEBAR_WIDTH_PX = 280;
inline constexpr int WINDOW_INIT_W = 1400;
inline constexpr int WINDOW_INIT_H = 900;
inline constexpr int BUTTON_HEIGHT_PX = 30;
inline constexpr unsigned long long BLINK_PERIOD_MS = 500;
inline constexpr size_t LABEL_NAME_MAX = 32;

// ─── Activity detection ───
inline constexpr size_t JSONL_TAIL_BYTES = 8192;

// ─── Timing ───
inline constexpr int TICK_EVERY_N_FRAMES = 30;
inline constexpr int WT_WINDOW_POLL_ATTEMPTS = 300;
inline constexpr int WT_POLL_SLEEP_MS = 25;
inline constexpr int SESSION_FILE_POLL_ATTEMPTS = 200;
inline constexpr int WT_SPAWN_WAIT_MS = 5000;

// ─── CLI defaults ───
// wt.exe -- <cmd> goes through CreateProcess, which does NOT honor PATHEXT,
// so .cmd shims need to be named explicitly. Edit if your install differs.
inline constexpr const char* COPILOT_COMMAND = "copilot";       // ships as copilot.exe
inline constexpr const char* GEMINI_COMMAND  = "gemini.cmd";    // npm shim

}
