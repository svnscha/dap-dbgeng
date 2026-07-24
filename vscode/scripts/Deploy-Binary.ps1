<#
.SYNOPSIS
    Deploys a binary to a remote Windows machine in one SSH round trip.

.DESCRIPTION
    The extension's default deploy stage, usable standalone (CI, other
    editors). Sends a generated PowerShell script to the target via
    -EncodedCommand and streams the binary - and optionally a signing
    certificate - as base64 over stdin, so the whole deploy costs a single
    SSH connection. On the target the script:
      1. stages the payload in ~\.dap-dbgeng\staging,
      2. resolves the destination from the service registration
         (sc.exe qc BINARY_PATH_NAME, normalizing quoted, \??\, \SystemRoot\
         and system32-relative forms) - or uses -Destination when given,
      3. stops the service (a running service's image file is locked),
      4. replaces the destination file,
      5. trusts the signing certificate (Root + TrustedPublisher),
      6. optionally reports the test-signing state (kernel targets).

    Markers on stdout: DEST=<resolved path>, TESTSIGNING=on|off.

.EXAMPLE
    ./Deploy-Binary.ps1 -HostName box -Binary build\Debug\hello.sys -ServiceName hello -CheckTestSigning

.EXAMPLE
    ./Deploy-Binary.ps1 -HostName box -Binary build\Debug\tool.exe -Destination C:\tool.exe

.EXAMPLE
    ./Deploy-Binary.ps1 -HostName localhost -Binary build\Debug\svc.exe -ServiceName svc
    Same steps on this machine, without ssh: stop the service, replace its
    registered binary, and leave it stopped.
#>
[CmdletBinding()]
param(
    # The target machine, or 'localhost' (or '.') to deploy on this one - the
    # same steps, without ssh in the way.
    [Parameter(Mandatory)] [string]$HostName,
    [Parameter(Mandatory)] [string]$Binary,
    # Deploy over this registered service's binary (the destination is derived
    # from the registration; the service is stopped first).
    [string]$ServiceName,
    # Explicit destination path on the target; alternative to -ServiceName.
    [string]$Destination,
    # Also ship this certificate and trust it (Root + TrustedPublisher).
    [string]$CertificateFile,
    [switch]$CheckTestSigning,
    # Forces the local path regardless of -HostName; also the test seam.
    [switch]$LocalTest
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$isLocal = $LocalTest -or ($HostName -in @('localhost', '127.0.0.1', '.', $env:COMPUTERNAME))

if (-not (Test-Path $Binary)) {
    throw "binary not found: $Binary - build it first."
}
if (-not $ServiceName -and -not $Destination) {
    throw 'provide -ServiceName (deploy over its registered binary) or -Destination.'
}

$binaryName = Split-Path $Binary -Leaf

# The remote side, kept literal in a single-quoted here-string (backticks and
# $-variables in it are evaluated on the TARGET). Placeholders are substituted
# below with single quotes doubled.
$remote = @'
$ErrorActionPreference = 'Stop'
# Progress records would come back as CLIXML noise on stderr.
$ProgressPreference = 'SilentlyContinue'
$payload = [Console]::In.ReadToEnd() -split "`r?`n"
$dir = Join-Path $env:USERPROFILE '.dap-dbgeng\staging'
New-Item -ItemType Directory -Force $dir | Out-Null
$staged = Join-Path $dir '__BINARYNAME__'
[IO.File]::WriteAllBytes($staged, [Convert]::FromBase64String($payload[0]))
$service = '__SERVICE__'
if ($service) {
    $qc = sc.exe qc $service 2>&1
    if ($LASTEXITCODE -ne 0) { Write-Output 'ERR=NOTREGISTERED'; exit 3 }
    $m = $qc | Select-String 'BINARY_PATH_NAME\s*:\s*(.+)' | Select-Object -First 1
    if (-not $m) { Write-Output 'ERR=NOPATH'; exit 4 }
    $dest = $m.Matches[0].Groups[1].Value.Trim()
    if ($dest -match '^"([^"]+)"') { $dest = $Matches[1] }
    if ($dest.StartsWith('\??\')) { $dest = $dest.Substring(4) }
    if ($dest -match '^\\SystemRoot\\(.*)$') { $dest = Join-Path $env:SystemRoot $Matches[1] }
    elseif ($dest -notmatch '^[A-Za-z]:') { $dest = Join-Path $env:SystemRoot $dest }
    sc.exe stop $service 2>&1 | Out-Null
} else {
    $dest = '__DESTINATION__'
}
Copy-Item $staged $dest -Force
Write-Output "DEST=$dest"
if ($payload.Count -gt 1 -and $payload[1]) {
    $cer = Join-Path $dir 'dap-dbgeng-test-signer.cer'
    [IO.File]::WriteAllBytes($cer, [Convert]::FromBase64String($payload[1]))
    certutil -addstore -f Root $cer | Out-Null
    certutil -addstore -f TrustedPublisher $cer | Out-Null
}
if ('__CHECKTS__' -eq '1') {
    $ts = bcdedit /enum '{current}' 2>&1 | Select-String testsigning
    if ("$ts" -match 'Yes') { Write-Output 'TESTSIGNING=on' } else { Write-Output 'TESTSIGNING=off' }
}
'@

# Remote PowerShell returns its error stream as CLIXML when it is redirected.
# Turn that back into the message a human wants to read.
function Format-RemoteError([string]$text) {
    if (-not $text) { return '' }
    $trimmed = $text.Trim()
    if ($trimmed -notmatch '^#<\s*CLIXML') { return $trimmed }
    try {
        $xml = [xml]($trimmed -replace '^#<\s*CLIXML\s*', '')
        $parts = @($xml.Objs.S | Where-Object { $_.S -eq 'Error' } | ForEach-Object { $_.'#text' })
        $joined = (($parts -join '') -replace '_x000D__x000A_', "`n").Trim()
        if ($joined) { return $joined }
    }
    catch { }
    return $trimmed
}

function Escape-Quotes([string]$value) { $value.Replace("'", "''") }
$remote = $remote.
    Replace('__BINARYNAME__', (Escape-Quotes $binaryName)).
    Replace('__SERVICE__', (Escape-Quotes "$ServiceName")).
    Replace('__DESTINATION__', (Escape-Quotes "$Destination")).
    Replace('__CHECKTS__', $(if ($CheckTestSigning) { '1' } else { '0' }))

$certificatePayload = if ($CertificateFile -and (Test-Path $CertificateFile)) {
    [Convert]::ToBase64String([IO.File]::ReadAllBytes($CertificateFile))
}
else { '' }
$payload = "{0}`n{1}`n" -f [Convert]::ToBase64String([IO.File]::ReadAllBytes($Binary)), $certificatePayload

# -EncodedCommand survives every quoting layer between here, sshd, and the
# target's default shell (PowerShell or cmd alike).
$encoded = [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($remote))
$remoteCommand = "powershell.exe -NoProfile -NonInteractive -EncodedCommand $encoded"

# The payload reaches the child's stdin as a file, which is the only way that
# stays byte-exact no matter how this script is hosted:
#   - a PowerShell pipeline runs the long base64 line through the output
#     formatter, which can wrap it,
#   - Process.StandardInput is a StreamWriter created with AutoFlush = true,
#     and that flush writes the encoding's preamble (a UTF-8 BOM) ahead of
#     anything we send.
# Both only misbehave when the host has no console - exactly how an editor runs
# this script - so they look fine from a terminal and corrupt the payload under
# the extension.
function Invoke-WithPayload {
    param([string]$FilePath, [string]$Arguments, [byte[]]$Payload)

    $inFile = [IO.Path]::GetTempFileName()
    $outFile = [IO.Path]::GetTempFileName()
    $errFile = [IO.Path]::GetTempFileName()
    try {
        [IO.File]::WriteAllBytes($inFile, $Payload)
        $process = Start-Process -FilePath $FilePath -ArgumentList $Arguments -NoNewWindow -Wait -PassThru `
            -RedirectStandardInput $inFile -RedirectStandardOutput $outFile -RedirectStandardError $errFile
        [PSCustomObject]@{
            ExitCode = $process.ExitCode
            StdOut   = [IO.File]::ReadAllText($outFile)
            StdErr   = [IO.File]::ReadAllText($errFile)
        }
    }
    finally {
        Remove-Item $inFile, $outFile, $errFile -Force -ErrorAction SilentlyContinue
    }
}

$payloadBytes = [Text.Encoding]::ASCII.GetBytes($payload)
$result = if ($isLocal) {
    Invoke-WithPayload -FilePath 'powershell.exe' -Arguments "-NoProfile -NonInteractive -EncodedCommand $encoded" -Payload $payloadBytes
}
else {
    Invoke-WithPayload -FilePath 'ssh' -Arguments "-o BatchMode=yes $HostName `"$remoteCommand`"" -Payload $payloadBytes
}
$code = $result.ExitCode

$result.StdOut -split "`r?`n" | Where-Object { $_ } | ForEach-Object { Write-Output $_ }
$remoteError = Format-RemoteError $result.StdErr
if ($code -ne 0 -and $remoteError) {
    Write-Host $remoteError
}
if ($code -eq 3) {
    throw ("service '$ServiceName' is not registered on $HostName. Register it once with: " +
        "sc.exe create $ServiceName type= kernel binPath= <path> (kernel), or " +
        "sc.exe create $ServiceName binPath= <path> (user mode).")
}
if ($code -ne 0) {
    $detail = ($remoteError -split "`n" | Where-Object { $_.Trim() -and $_ -notmatch '^(At line:|\+|\s+\+)' } |
        Select-Object -First 1)
    if ($detail -match 'being used by another process') {
        throw ("the destination file is in use on $HostName, so it cannot be replaced. Deploy with " +
            '-ServiceName so the service is stopped first, or stop whatever is running it.')
    }
    throw ("remote deploy failed (exit $code)" + $(if ($detail) { ": $($detail.Trim())" } else { '.' }))
}
