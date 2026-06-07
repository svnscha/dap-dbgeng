<#
.SYNOPSIS
    Copy the built adapter into the VS Code extension so it ships inside the .vsix.

.DESCRIPTION
    The extension embeds the adapter under vscode/bin so a packaged extension is
    self-contained (only dbgeng.dll, from the Windows SDK, stays external). It bundles
    the Release build, which links statically (x64-windows-static), so the output is a
    single dap-dbgeng.exe with no fmt/spdlog/CRT DLLs. Every .exe/.dll in the build
    output is copied anyway, so a stray dependency would still be picked up.
    vscode/bin is git-ignored (a build artifact) but included in the .vsix.

.PARAMETER SourceDir
    The adapter build output to bundle. Defaults to the Release tree.
#>
[CmdletBinding()]
param(
    [string]$SourceDir = "build/windows-x64-release/src",
    [string]$Dest = "vscode/bin"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$src = Join-Path $root $SourceDir
$dst = Join-Path $root $Dest

$exe = Join-Path $src "dap-dbgeng.exe"
if (-not (Test-Path $exe)) {
    throw "Adapter not found at '$exe'. Build it first (npm run build:release)."
}

if (Test-Path $dst) {
    Remove-Item -Recurse -Force $dst
}
New-Item -ItemType Directory -Force -Path $dst | Out-Null

# The adapter plus the vcpkg runtime DLLs it loads (fmt, spdlog).
Get-ChildItem $src -File | Where-Object { $_.Extension -in ".exe", ".dll" } | ForEach-Object {
    Copy-Item $_.FullName -Destination $dst -Force
}

Write-Host "Bundled adapter into ${Dest}:"
Get-ChildItem $dst -File | ForEach-Object { Write-Host ("  {0} ({1:N0} bytes)" -f $_.Name, $_.Length) }
