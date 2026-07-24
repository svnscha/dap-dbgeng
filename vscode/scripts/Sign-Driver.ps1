<#
.SYNOPSIS
    Test-signs a driver binary with a self-signed code-signing certificate.

.DESCRIPTION
    Used by the extension's deploy flow (the launch config's target.signing
    block). Finds signtool.exe, creates or reuses a self-signed code-signing
    certificate in Cert:\CurrentUser\My, signs the binary, and exports the
    certificate so the deploy flow can trust it on the target machine.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$Binary,
    [Parameter(Mandatory)] [string]$CertSubject,
    [Parameter(Mandatory)] [string]$OutCertificate,
    # Extra signtool search globs (e.g. a workspace-local WDK NuGet layout),
    # tried before the machine-wide locations.
    [string[]]$SigntoolGlob = @()
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$patterns = $SigntoolGlob + @(
    "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe",
    "$env:USERPROFILE\.nuget\packages\microsoft.windows.wdk.x64\*\c\bin\*\x64\signtool.exe"
)
$signtool = $null
foreach ($pattern in $patterns) {
    $hits = @(Resolve-Path -Path $pattern -ErrorAction SilentlyContinue | Sort-Object -Property Path)
    if ($hits.Count -gt 0) { $signtool = $hits[-1].Path; break }
}
if (-not $signtool) {
    throw 'signtool.exe not found (looked in Windows Kits and the WDK NuGet packages).'
}

# Filtering in Where-Object rather than with -CodeSigningCert on purpose: that
# is a dynamic parameter of the certificate provider, and it does not bind in
# every host (Windows PowerShell launched without a console, for one).
$cert = @(Get-ChildItem Cert:\CurrentUser\My |
    Where-Object { $_.Subject -eq $CertSubject -and $_.HasPrivateKey -and $_.NotAfter -gt (Get-Date) }) |
    Select-Object -First 1
if (-not $cert) {
    $cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject $CertSubject `
        -CertStoreLocation Cert:\CurrentUser\My -KeyUsage DigitalSignature `
        -NotAfter (Get-Date).AddYears(2)
}
Export-Certificate -Cert $cert -FilePath $OutCertificate | Out-Null

$output = & $signtool sign /fd SHA256 /sha1 $cert.Thumbprint $Binary 2>&1
if ($LASTEXITCODE -ne 0) {
    $output | ForEach-Object { Write-Host "$_" }
    throw "signtool sign failed (exit $LASTEXITCODE)."
}
