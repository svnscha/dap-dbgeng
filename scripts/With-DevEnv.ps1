<#
.SYNOPSIS
    Run a command inside the Visual Studio x64 developer environment.

.DESCRIPTION
    The Ninja generator needs the MSVC toolchain (cl/link, INCLUDE/LIB) on the
    environment, which a plain shell does not have. This locates the latest VS
    install with the C++ toolset via vswhere, enters its developer shell, and
    then runs the supplied command — so `npm run build` etc. work from any shell.

.EXAMPLE
    pwsh scripts/With-DevEnv.ps1 cmake --preset windows-x64
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, ValueFromRemainingArguments = $true)]
    [string[]] $Command
)

$ErrorActionPreference = 'Stop'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw "vswhere.exe not found at '$vswhere'. Install Visual Studio with the C++ workload."
}

$installPath = & $vswhere -latest -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath
if ([string]::IsNullOrWhiteSpace($installPath)) {
    throw 'No Visual Studio installation with the C++ toolset was found.'
}

Import-Module (Join-Path $installPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $installPath -SkipAutomaticLocation `
    -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null

& $Command[0] @($Command[1..($Command.Length - 1)])
exit $LASTEXITCODE
