# agents-hub

A Windows desktop app that multiplexes multiple AI coding CLI sessions inside a
single window. Each tab is a real Windows Terminal — running `claude`, `copilot`,
or `gemini` — docked as a child window. An ImGui sidebar lets you spawn, switch,
and kill sessions, shows waiting-for-input status per Claude tab, and renders a
live preview of the last assistant reply.

## Features

- **+ New Agent** dialog lets you pick the CLI (Claude / Copilot / Gemini) and the
  project folder for each spawn. Defaults preserved so a single Enter still gives
  you "Claude in current directory".
- Spawn multiple sessions of any kind, switch between them from the sidebar.
- **Claude-only today**: per-tab waiting indicator (the row blinks yellow when the
  turn is complete and a border is drawn around the waiting agent), live text
  preview of the latest assistant message, sidebar labels follow Claude Code's
  conversation name (set with `/rename`, preserved across `/resume`).
- Copilot / Gemini tabs currently behave as plain docked terminals with a kill
  button — generic waiting detection is on the roadmap.
- Optional hook (`scripts/claude_hub_notify.ps1`) that forwards Claude Code's
  `Stop` / `Notification` events as waiting flags, for instant signalling.

## Requirements

- Windows 10 1809+ or Windows 11 (needs modern ConPTY and Windows Terminal).
- [Windows Terminal](https://apps.microsoft.com/detail/9n0dx20hk701) (`wt.exe` on PATH).
- At least one of:
  - [Claude Code](https://docs.claude.com/en/docs/claude-code) installed at
    `%USERPROFILE%\.local\bin\claude.exe`.
  - GitHub Copilot CLI: `copilot` on PATH.
  - Google Gemini CLI: `gemini.cmd` on PATH (`npm i -g @google/gemini-cli`).
- Visual Studio 2022 (with the **Desktop development with C++** workload) or any
  MSVC toolchain + CMake 3.20+.
- Git (CMake's `FetchContent` pulls Dear ImGui from GitHub at configure time).

## Build

```powershell
git clone https://github.com/liorsegev/claude-hub.git
cd claude-hub
cmake -B build -S .
cmake --build build --config Release
```

The resulting binary is `build\Release\agents-hub.exe`.

### Build inside an IDE

- **CLion**: open the folder, pick the Visual Studio toolchain, let it run CMake,
  then build the `agents-hub` target.
- **Visual Studio**: *File > Open > CMake...*, select `CMakeLists.txt`, choose the
  `x64-Release` configuration, build the `agents-hub` target.

## Run

```powershell
build\Release\agents-hub.exe
```

Click **+ New Agent** in the sidebar, pick the CLI and folder, then **Create**.
A new Windows Terminal window opens and its HWND is reparented into the main area.

## Tests

```powershell
cmake -B cmake-build-tests -S . -DBUILD_TESTS=ON
cmake --build cmake-build-tests
ctest --test-dir cmake-build-tests --output-on-failure
```

In CLion / VS, set `-DBUILD_TESTS=ON` on your CMake profile and the
`agents_hub_tests` target plus per-test gutter ▶ icons appear automatically.

## Optional: waiting-notification hook (Claude only)

For instant "agent is waiting for input" signalling (instead of inferring from the
JSONL), install the hook:

1. Copy the absolute path to `scripts/claude_hub_notify.ps1`.
2. Add the following to `%USERPROFILE%\.claude\settings.json` (merge with the
   existing `hooks` block if any):

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
agents-hub polls that directory and blinks the matching tab the instant a flag
appears. Without the hook, the app falls back to parsing the JSONL's `stop_reason`
field — also works, just slightly less immediate.

## Troubleshooting

- **Terminal takes a couple of seconds to dock.** The `claude` / `copilot` /
  `gemini` startup + (for Claude) pid.json write can take ~1 s; the sidebar shows
  a "Spawning agent..." overlay until the docked terminal appears.
- **Label stays `claude_<id>` / `copilot_<id>` / `gemini_<id>`.** For Claude:
  no `/rename` has been issued and no first user prompt exists yet — send a
  prompt or `/rename it-something`. For Copilot/Gemini: no title extraction is
  implemented yet, so the fallback name is the only label.
- **Gemini fails to launch with `0x80070002 file not found`.** wt.exe's
  `CreateProcess` doesn't honor PATHEXT, so the `.cmd` extension is required.
  The default `gemini.cmd` matches an npm install. Adjust `GEMINI_COMMAND` in
  `include/Constants.hpp` if your install differs.
- **Waiting never fires (Claude).** Check `debug.log` in the working directory
  for lines like `last=assistant stop_reason=end_turn` for the agent you expect —
  if you don't see `assistant` / `end_turn`, the JSONL probe hasn't caught up yet
  (usually resolves within a tick).
- **Stale pid.json files in `%USERPROFILE%\.claude\sessions\`** are harmless —
  the app filters them out by liveness and by snapshot diff.
