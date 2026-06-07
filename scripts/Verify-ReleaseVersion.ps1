<#
.SYNOPSIS
    Verify a release tag matches the VS Code extension version.

.DESCRIPTION
    The release workflow is triggered by a vX.Y.Z tag. This guards against
    publishing a tag whose version does not match the extension manifest, which
    would ship a mislabeled VSIX to the Marketplace. Compares the tag against the
    version in vscode/package.json (the published artifact) and fails if they
    differ.

.PARAMETER Tag
    The release tag, e.g. v0.1.0.

.EXAMPLE
    pwsh scripts/Verify-ReleaseVersion.ps1 -Tag v0.1.0
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Tag
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$manifestPath = Join-Path $root 'vscode/package.json'

$version = (Get-Content -Raw -Path $manifestPath | ConvertFrom-Json).version
if ([string]::IsNullOrWhiteSpace($version)) {
    throw "vscode/package.json does not contain a valid 'version'."
}

$expected = "v$version"
if ($Tag -ne $expected) {
    throw "Release tag '$Tag' does not match the extension version in vscode/package.json ('$version'). Expected '$expected'."
}

Write-Host "Release tag '$Tag' matches the extension version '$version'."
