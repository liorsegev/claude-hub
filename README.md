# claude-hub

A Windows desktop app that multiplexes multiple Claude Code CLI sessions inside a single window.
Each tab is a real Windows Terminal running `claude`, docked as a child window; an ImGui sidebar lets
you spawn, switch, and kill sessions, shows waiting-for-input status per tab, and renders a live
preview of the last assistant reply.

## Features

- Spawn multiple Claude Code sessions, switch between them from the sidebar.
- Per-tab waiting indicator: the row blinks yellow when that tab's turn is complete
  (last JSONL entry is an `assistant` with `stop_reason=end_turn`) and a border is drawn around
  the waiting agent.
- Live text preview of the latest assistant message under each row.
- Sidebar labels follow Claude Code's own conversation name — set with `/rename`, preserved
  across `/resume`.
- Optional hook (`scripts/claude_hub_notify.ps1`) that forwards Claude Code's `Stop` /
  `Notification` events as waiting flags, for instant signalling.

## Requirements

- Windows 10 1809+ or Windows 11 (needs modern ConPTY and Windows Terminal).
- [Windows Terminal](https://apps.microsoft.com/detail/9n0dx20hk701) (`wt.exe` on PATH).
- [Claude Code](https://docs.claude.com/en/docs/claude-code) CLI installed at
  `%USERPROFILE%\.local\bin\claude.exe`.
- Visual Studio 2022 (with the **Desktop development with C++** workload) or any MSVC toolchain
  + CMake 3.20+.
- Git (CMake's `FetchContent` pulls Dear ImGui from GitHub at configure time).

## Build

```powershell
git clone https://github.com/liorsegev/claude-hub.git
cd claude-hub
cmake -B build -S .
cmake --build build --config Release
```

The resulting binary is `build\Release\claude-hub.exe`.

### Build inside an IDE

- **CLion**: open the folder, pick the Visual Studio toolchain, let it run CMake, then build the
  `claude-hub` target.
- **Visual Studio**: *File > Open > CMake...*, select `CMakeLists.txt`, choose the
  `x64-Release` configuration, build the `claude-hub` target.

## Run

```powershell
build\Release\claude-hub.exe
```

Click **+ New Agent** in the sidebar to spawn a Claude Code session. Each click opens a new
Windows Terminal window whose HWND is reparented into the main area.

## Optional: waiting-notification hook

For instant "agent is waiting for input" signalling (instead of inferring from the JSONL),
install the hook:

1. Copy the absolute path to `scripts/claude_hub_notify.ps1`.
2. Add the following to `%USERPROFILE%\.claude\settings.json` (merge with the existing
   `hooks` block if any):

   ```json
   {
     "hooks": {
       "Stop": [{
         "hooks": [{
           "type": "command",
           "command": "powershell -NoProfile -ExecutionPolicy Bypass -File \"<FULL-PATH>\\scripts\\claude_hub_notify.ps1\""
         }]
       }],
       "Notification": [{
         "hooks": [{
           "type": "command",
           "command": "powershell -NoProfile -ExecutionPolicy Bypass -File \"<FULL-PATH>\\scripts\\claude_hub_notify.ps1\""
         }]
       }]
     }
   }
   ```

   Replace `<FULL-PATH>` with the absolute path of your cloned repo.
3. Restart any running `claude` sessions so they pick up the hook.

The hook writes a sentinel file to `%USERPROFILE%\.claude\hub-waiting\<session-id>.flag`.
claude-hub polls that directory and blinks the matching tab the instant a flag appears.
Without the hook, the app falls back to parsing the JSONL's `stop_reason` field — also works,
just slightly less immediate.

## Troubleshooting

- **Terminal takes a couple of seconds to dock.** The `claude` startup + pid.json write can
  take ~1 s; the tab shows up in the sidebar immediately but the docked terminal appears when
  wt.exe's window is found.
- **Label stays `chub_<number>`.** No `/rename` has been issued for that session and no first
  user prompt exists yet. Send a prompt, or `/rename it-something`.
- **Waiting never fires.** Check `debug.log` in the working directory for lines like
  `last=assistant stop_reason=end_turn` for the agent you expect — if you don't see `assistant`
  / `end_turn`, the JSONL probe hasn't caught up yet (usually resolves within a tick).
- **Stale pid.json files in `%USERPROFILE%\.claude\sessions\`** are harmless — the app filters
  them out by liveness and by snapshot diff.
