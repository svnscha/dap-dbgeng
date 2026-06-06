<#
.SYNOPSIS
    Restores the WDK NuGet packages needed to build the kernel driver in test-targets/sys.

.DESCRIPTION
    The driver build (cmake/FindWDKNuGet.cmake) looks for the WDK + SDK NuGet
    packages in the per-user global cache (%USERPROFILE%\.nuget\packages) and in a
    local .\packages directory. Visual Studio restores them automatically when it
    sees packages.config; on a fresh box (or a build agent) without that, run this
    script. It uses nuget.exe (downloading a private copy under .tools if needed)
    and restores into .\packages, which the CMake module also searches.

    Note: `dotnet restore` is not used here — the WDK packages are not consumable by
    the .NET CLI. nuget.exe restore just downloads the package content, which is all
    the CMake build needs.

.EXAMPLE
    ./test-targets/sys/Restore-Wdk.ps1
#>
[CmdletBinding()]
param(
    [string]$PackagesDir = (Join-Path $PSScriptRoot 'packages')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$nuget = (Get-Command nuget -ErrorAction SilentlyContinue)?.Source
if (-not $nuget) {
    $nuget = Join-Path $PSScriptRoot '.tools\nuget.exe'
    if (-not (Test-Path $nuget)) {
        Write-Host 'nuget.exe not found on PATH — downloading a local copy to .tools ...'
        New-Item -ItemType Directory -Force -Path (Split-Path $nuget) | Out-Null
        Invoke-WebRequest -Uri 'https://dist.nuget.org/win-x86-commandline/latest/nuget.exe' -OutFile $nuget
    }
}

$config = Join-Path $PSScriptRoot 'packages.config'
Write-Host "Restoring $config -> $PackagesDir"
& $nuget restore $config -PackagesDirectory $PackagesDir
Write-Host 'Done. Configure with: cmake -S test-targets/sys -B test-targets/sys/build -G "Visual Studio 17 2022" -A x64'
