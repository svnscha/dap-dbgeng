<#
.SYNOPSIS
    Makes sure a dbgsrv process server is listening on a remote Windows machine.

.DESCRIPTION
    The extension's default dbgsrv stage, usable standalone. When no dbgsrv is
    running on the target, this starts one - copying the debugger binaries to
    the target first if they are not there yet.

    Resolution order for the dbgsrv to start:
      1. -DbgsrvPath, when given and present on the target.
      2. ~\.dap-dbgeng\tools\dbgsrv.exe on the target (from an earlier run).
      3. A local debugger folder copied to that path: -SourcePath, else the
         installed WinDbg (store package), else the Windows Kits Debugging
         Tools. Pass -NoAutoDeploy to fail instead of copying.

.EXAMPLE
    ./Ensure-ProcessServer.ps1 -HostName box -Transport tcp:port=5005
    Starts dbgsrv, deploying the debugger to the target on first use.

.EXAMPLE
    ./Ensure-ProcessServer.ps1 -HostName box -Transport tcp:port=5005 -DbgsrvPath C:\Debuggers\dbgsrv.exe
    Uses a dbgsrv that is already on the target.
#>
[CmdletBinding()]
param(
    # The target machine, or 'localhost' (or '.') to run the process server on
    # this one - which is how a service can be debugged without running the
    # editor elevated: start dbgsrv elevated once, and the debugger attaches
    # through it.
    [Parameter(Mandatory)] [string]$HostName,
    # The dbgsrv transport, e.g. tcp:port=5005 (no client-side server= part).
    [Parameter(Mandatory)] [string]$Transport,
    # An existing dbgsrv.exe on the target; skips deployment when it is there.
    [string]$DbgsrvPath,
    # Local folder holding dbgsrv.exe and its dependencies, when auto-detection
    # picks the wrong one.
    [string]$SourcePath,
    [switch]$NoAutoDeploy
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Scratch root on the target, shared with the deploy stage. The remote path is
# built with Join-Path on the target so it survives a profile path with spaces.
$remoteTools = "(Join-Path `$env:USERPROFILE '.dap-dbgeng\tools')"
$remoteToolsRelative = '.dap-dbgeng/tools'

# Remote commands must not contain double quotes: Windows PowerShell drops them
# when it hands an argument to a native command, so a command that works from a
# terminal (PowerShell 7 quotes it properly) breaks when an editor runs this
# script. Single quotes and parentheses survive both.
$isLocal = $HostName -in @('localhost', '127.0.0.1', '.', $env:COMPUTERNAME)

function Invoke-Remote {
    param([string]$Command)
    if ($Command.Contains('"')) {
        throw "internal: remote command must not contain double quotes: $Command"
    }
    if ($isLocal) {
        return "$(& ([scriptblock]::Create($Command)))".Trim()
    }
    $output = ssh -o BatchMode=yes $HostName $Command 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "ssh $HostName failed: $($output -join ' ')"
    }
    "$output".Trim()
}

function Resolve-LocalDebuggerFolder {
    if ($SourcePath) {
        if (-not (Test-Path (Join-Path $SourcePath 'dbgsrv.exe'))) {
            throw "no dbgsrv.exe in -SourcePath '$SourcePath'."
        }
        return (Resolve-Path $SourcePath).Path
    }

    $candidates = @()
    try {
        $windbg = Get-AppxPackage -Name Microsoft.WinDbg -ErrorAction SilentlyContinue
        if ($windbg) { $candidates += (Join-Path $windbg.InstallLocation 'amd64') }
    }
    catch { }
    $candidates += "${env:ProgramFiles(x86)}\Windows Kits\10\Debuggers\x64"
    $candidates += "$env:ProgramFiles\Windows Kits\10\Debuggers\x64"

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path (Join-Path $candidate 'dbgsrv.exe'))) { return $candidate }
    }
    throw ('no local dbgsrv.exe found to deploy (looked for an installed WinDbg and the Windows Kits ' +
        'Debugging Tools). Install the Debugging Tools for Windows, or pass -SourcePath.')
}

if ((Invoke-Remote '(Get-Process dbgsrv -ErrorAction SilentlyContinue | Measure-Object).Count') -ne '0') {
    return
}

# 1/2: an existing dbgsrv on the target.
$remoteDbgsrv = "(Join-Path $remoteTools 'dbgsrv.exe')"
$target = $null
if ($DbgsrvPath) {
    if ((Invoke-Remote "Test-Path '$DbgsrvPath'") -eq 'True') {
        $target = "'$DbgsrvPath'"
    }
    elseif ($NoAutoDeploy) {
        throw "dbgsrv not found at '$DbgsrvPath' on $HostName."
    }
}
if (-not $target -and (Invoke-Remote "Test-Path $remoteDbgsrv") -eq 'True') {
    $target = $remoteDbgsrv
}

# 3a: locally there is nothing to copy - use the debugger that is installed here.
if (-not $target -and $isLocal) {
    $target = "'" + (Join-Path (Resolve-LocalDebuggerFolder) 'dbgsrv.exe') + "'"
}

# 3b: copy the debugger to the target.
if (-not $target) {
    if ($NoAutoDeploy) {
        throw "no dbgsrv on $HostName and -NoAutoDeploy was given."
    }
    $source = Resolve-LocalDebuggerFolder
    $files = @(Get-ChildItem -Path $source -File)
    $megabytes = [math]::Round((($files | Measure-Object Length -Sum).Sum / 1MB), 1)
    Write-Host ("[i] deploying the debugger to {0}:~\{1} ({2} files, {3} MB, one time)" -f
        $HostName, $remoteToolsRelative, $files.Count, $megabytes)

    $null = Invoke-Remote "New-Item -ItemType Directory -Force $remoteTools | Out-Null"
    $output = scp -q -o BatchMode=yes ($files.FullName) "${HostName}:$remoteToolsRelative/" 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "copying the debugger to $HostName failed: $($output -join ' ')"
    }
    $target = $remoteDbgsrv
}

if ($isLocal -and
    -not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole(
        [Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host ('[i] starting dbgsrv without administrator rights: it can debug your own processes, but not a ' +
        'service (those run as LocalSystem in session 0). For those, start dbgsrv elevated once - the editor ' +
        'itself can then stay unelevated.')
}

$null = Invoke-Remote "Start-Process $target -ArgumentList '-t','$Transport'"
