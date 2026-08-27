<#
.SYNOPSIS
    Stops a service on a target machine (or this one).

.DESCRIPTION
    The extension's default stop step, usable standalone. An already-stopped
    service counts as success: that is the normal case in teardown, when the
    debug session ended by terminating the process.

.EXAMPLE
    ./Stop-RemoteService.ps1 -HostName box -ServiceName hello

.EXAMPLE
    ./Stop-RemoteService.ps1 -HostName localhost -ServiceName hello-service
#>
[CmdletBinding()]
param(
    # The target machine, or 'localhost' (or '.') for this one.
    [Parameter(Mandatory)] [string]$HostName,
    [Parameter(Mandatory)] [string]$ServiceName
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$isLocal = $HostName -in @('localhost', '127.0.0.1', '.', $env:COMPUTERNAME)
$out = if ($isLocal) { sc.exe stop $ServiceName 2>&1 } else { ssh -o BatchMode=yes $HostName "sc.exe stop $ServiceName" 2>&1 }

if ($LASTEXITCODE -ne 0) {
    # Already stopped is the state this asks for. The exit code is reset because
    # callers (the extension's hook runner) treat a leftover non-zero
    # $LASTEXITCODE as the command having failed.
    if ("$out" -match '1062|has not been started') {
        $global:LASTEXITCODE = 0
        return
    }
    $out | ForEach-Object { Write-Host "$_" }
    throw "sc.exe stop $ServiceName failed on $HostName (exit $LASTEXITCODE)."
}
