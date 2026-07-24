<#
.SYNOPSIS
    Starts a service on a target machine (or this one).

.DESCRIPTION
    The extension's default start step, usable standalone. Uses sc.exe rather
    than Start-Service on purpose: it returns as soon as the start is issued,
    instead of waiting for the service to report RUNNING - which never happens
    while the service is parked waiting for a debugger to attach.

.EXAMPLE
    ./Start-RemoteService.ps1 -HostName box -ServiceName hello

.EXAMPLE
    ./Start-RemoteService.ps1 -HostName localhost -ServiceName hello-service
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
$out = if ($isLocal) { sc.exe start $ServiceName 2>&1 } else { ssh -o BatchMode=yes $HostName "sc.exe start $ServiceName" 2>&1 }

if ($LASTEXITCODE -ne 0) {
    # Already running is the state this asks for.
    if ("$out" -match '1056|already running') {
        $global:LASTEXITCODE = 0
        return
    }
    $out | ForEach-Object { Write-Host "$_" }
    throw "sc.exe start $ServiceName failed on $HostName (exit $LASTEXITCODE)."
}
