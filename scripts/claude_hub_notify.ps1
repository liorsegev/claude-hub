# Claude-Hub waiting-notification hook.
# Writes %USERPROFILE%\.claude\hub-waiting\<session_id>.flag when Claude Code
# finishes its turn or raises a notification. The claude-hub.exe multiplexer
# polls this directory and blinks the corresponding sidebar tab.
#
# Install by adding to %USERPROFILE%\.claude\settings.json:
#
#   {
#     "hooks": {
#       "Stop": [{
#         "hooks": [{
#           "type": "command",
#           "command": "powershell -NoProfile -ExecutionPolicy Bypass -File \"<FULL-PATH>\\scripts\\claude_hub_notify.ps1\""
#         }]
#       }],
#       "Notification": [{
#         "hooks": [{
#           "type": "command",
#           "command": "powershell -NoProfile -ExecutionPolicy Bypass -File \"<FULL-PATH>\\scripts\\claude_hub_notify.ps1\""
#         }]
#       }]
#     }
#   }
#
# Replace <FULL-PATH> with the absolute path to this repo.

$ErrorActionPreference = 'Stop'

try {
	$raw = [Console]::In.ReadToEnd()
	if ([string]::IsNullOrWhiteSpace($raw)) { exit 0 }
	$payload = $raw | ConvertFrom-Json
	if (-not $payload.session_id) { exit 0 }

	$flagDir = Join-Path $env:USERPROFILE '.claude\hub-waiting'
	New-Item -ItemType Directory -Force -Path $flagDir | Out-Null

	$flag = Join-Path $flagDir ($payload.session_id + '.flag')
	Set-Content -LiteralPath $flag -Value ''
} catch {
	# Never block Claude Code on hook errors.
}

exit 0
