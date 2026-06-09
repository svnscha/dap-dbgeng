<#
.SYNOPSIS
    Normalizes a raw DAP session recording into a portable replay fixture.

.DESCRIPTION
    Replaces machine/run-specific values in a recorded `--trace` / "trace"-config session with portable tokens
    that the replay harness de-normalizes at runtime:

      - the repository root (both \ and / forms) -> ${workspaceFolder}
      - the dbgeng.dll path                       -> ${dbgEngPath}
      - the attach request's processId            -> "${attachProcessId}"

    Run this once when turning a captured session into a fixture under
    tests/replay/data. The replayer substitutes the tokens back to concrete values for the
    current box and run (see tests/replay/replay_harness.{h,cpp}: load_session / replay).

.EXAMPLE
    ./scripts/Normalize-DapRecording.ps1 -Path test-targets/testapp/recordings/attach.session.json `
        -OutFile tests/replay/data/attach-process.json
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Path,

    [Parameter(Mandatory = $true, Position = 1)]
    [string]$OutFile
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path.TrimEnd('\', '/')
$text = [System.IO.File]::ReadAllText((Resolve-Path -LiteralPath $Path))

# Find the launch/attach request so we can detect the engine path and attach pid to tokenize.
$session = $text | ConvertFrom-Json
$configRequest = $session.messages |
    Where-Object { $_.direction -eq 'in' -and $_.message.type -eq 'request' -and ($_.message.command -in @('attach', 'launch')) } |
    Select-Object -First 1
if (-not $configRequest) {
    throw "No launch/attach request found in '$Path'; nothing to normalize."
}
$configArgs = $configRequest.message.arguments

# 1) Repository root -> ${workspaceFolder}. The JSON text stores backslashes escaped (\\), so replace that form
#    as well as the forward-slash form. Match case-insensitively because the debugger emits a lowercased drive
#    letter (e.g. d:\) in some source paths.
$ignoreCase = [System.StringComparison]::OrdinalIgnoreCase
$text = $text.Replace($repoRoot.Replace('\', '\\'), '${workspaceFolder}', $ignoreCase)
$text = $text.Replace($repoRoot.Replace('\', '/'), '${workspaceFolder}', $ignoreCase)

# 2) dbgeng path -> ${dbgEngPath}. Like the repository root, the value is a Windows path whose
#    backslashes are stored escaped (\\) in the JSON text, so replace that form as well as the
#    forward-slash form (and the raw value, in case a recorder emitted single separators).
if (($configArgs.PSObject.Properties.Name -contains 'dbgengPath') -and $configArgs.dbgengPath) {
    $dbgengPath = [string]$configArgs.dbgengPath
    $text = $text.Replace($dbgengPath.Replace('\', '\\'), '${dbgEngPath}', $ignoreCase)
    $text = $text.Replace($dbgengPath.Replace('\', '/'), '${dbgEngPath}', $ignoreCase)
    $text = $text.Replace($dbgengPath, '${dbgEngPath}', $ignoreCase)
}

# 3) attach processId -> "${attachProcessId}" (only the request field, not coincidental numbers elsewhere)
if (($configArgs.PSObject.Properties.Name -contains 'processId') -and ($null -ne $configArgs.processId)) {
    $targetPid = [int]$configArgs.processId
    $text = $text.Replace(('"processId": {0}' -f $targetPid), '"processId": "${attachProcessId}"')
}

# 4) dbgsrv connection-string port -> ${dbgsrvPort}. Only the port is volatile per run; the transport/server stay.
if (($configArgs.PSObject.Properties.Name -contains 'connectionString') -and $configArgs.connectionString) {
    $portMatch = [regex]::Match([string]$configArgs.connectionString, 'port=\d+')
    if ($portMatch.Success) {
        $text = $text.Replace($portMatch.Value, 'port=${dbgsrvPort}', $ignoreCase)
    }
}

$outDirectory = Split-Path -Parent $OutFile
if ($outDirectory -and -not (Test-Path -LiteralPath $outDirectory)) {
    New-Item -ItemType Directory -Force -Path $outDirectory | Out-Null
}

[System.IO.File]::WriteAllText($OutFile, $text, (New-Object System.Text.UTF8Encoding($false)))
Write-Host "Normalized recording written to $OutFile"
