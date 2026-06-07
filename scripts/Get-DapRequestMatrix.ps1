<#
.SYNOPSIS
    Generate (or verify) the DAP request coverage matrix documentation page.

.DESCRIPTION
    Builds a Markdown coverage matrix that maps every Debug Adapter Protocol
    request to its handler and the recorded replay fixtures that exercise it, and
    writes it to the generated documentation page (default
    docs/development/request-coverage.md).

    The matrix is derived from three live sources of truth, so it never drifts
    from the code:

      requests : the generated dispatch in src/protocol/dap_service.h (one
                 `virtual void handle_<name>_request(...)` per DAP request; the
                 wire command string is the argument to the default-body
                 `unhandled_dap_request(request.seq, "<command>")`).
      handlers : the src/service/dap_server_<command>.cpp partials, each defining
                 `void dap_server::handle_<name>_request(...)`.
      fixtures : the recorded replay sessions under tests/replay/data/*.json.

    The page is generated, never hand-edited. Regenerate it with `npm run matrix`
    after adding a handler or a fixture; `npm run matrix:check` (run in CI) fails
    if the committed page is out of date.

.PARAMETER RootPath
    Repo root. Defaults to the parent of the scripts/ folder.

.PARAMETER OutFile
    Destination page. Defaults to docs/development/request-coverage.md under the
    repo root.

.PARAMETER Check
    Verify only: regenerate the page in memory and compare it with the committed
    file. Exits 1 if they differ (use this in CI). Does not write.

.PARAMETER Stdout
    Print the raw Markdown table (with repo-relative links) to stdout instead of
    writing the page. A quick interactive view; does not touch the doc page.

.EXAMPLE
    pwsh scripts/Get-DapRequestMatrix.ps1
    Regenerate docs/development/request-coverage.md.

.EXAMPLE
    pwsh scripts/Get-DapRequestMatrix.ps1 -Check
    Verify the committed page is up to date (non-zero exit if stale).

.EXAMPLE
    pwsh scripts/Get-DapRequestMatrix.ps1 -Stdout
    Print just the matrix table for a quick look.
#>
[CmdletBinding()]
param(
    [string]$RootPath = (Split-Path -Parent $PSScriptRoot),
    [string]$OutFile,
    [switch]$Check,
    [switch]$Stdout
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path $RootPath).Path
$servicePath = Join-Path $root 'src\protocol\dap_service.h'
$handlersPath = Join-Path $root 'src\service'
$fixturesPath = Join-Path $root 'tests\replay\data'

if (-not $OutFile) {
    $OutFile = Join-Path $root 'docs\development\request-coverage.md'
}

# Absolute base for source/fixture links on the published documentation site.
# Repo-relative links would not resolve against docs_dir, so the page uses GitHub
# blob URLs; the -Stdout view keeps repo-relative links for local file opening.
$blobBase = 'https://github.com/svnscha/dap-dbgeng/blob/main/'

# Map the generated snake_case handler stem (e.g. configuration_done) to its wire
# command (e.g. configurationDone), read straight from dap_service.h so the matrix
# tracks the generated surface without a hand-maintained list.
function Get-RequestCommandMap {
    $content = Get-Content -Path $servicePath -Raw
    $map = [ordered]@{}

    # Each request handler's default body throws unhandled_dap_request(seq, "<command>").
    $pattern = 'virtual\s+void\s+handle_(?<stem>[a-z0-9_]+)_request\s*\([^)]*\)\s*\{\s*throw\s+unhandled_dap_request\([^,]+,\s*"(?<command>[^"]+)"\)'
    foreach ($match in [regex]::Matches($content, $pattern)) {
        $map[$match.Groups['stem'].Value] = $match.Groups['command'].Value
    }

    return $map
}

$requestCommandMap = Get-RequestCommandMap

# An implemented request has a partial that defines dap_server::handle_<stem>_request.
$handlerRows = @{}
foreach ($file in Get-ChildItem -Path $handlersPath -Filter 'dap_server_*.cpp') {
    $content = Get-Content -Path $file.FullName -Raw

    foreach ($request in $requestCommandMap.GetEnumerator()) {
        $methodPattern = "void\s+dap_server::handle_$($request.Key)_request\s*\("
        if ($content -match $methodPattern) {
            $handlerRows[$request.Value] = 'src/service/' + $file.Name
        }
    }
}

$fixtureContents = @{}
if (Test-Path $fixturesPath) {
    foreach ($file in Get-ChildItem -Path $fixturesPath -Filter '*.json' | Sort-Object Name) {
        $fixtureContents[$file.Name] = Get-Content -Path $file.FullName -Raw
    }
}

# Build the Markdown table. $LinkPrefix is prepended to every repo-relative path:
# '' yields repo-relative links (the -Stdout view), the GitHub blob base yields
# absolute links that resolve on the published site.
function Build-MatrixTable {
    param([string]$LinkPrefix = '')

    $rows = [System.Collections.Generic.List[string]]::new()
    $rows.Add('| Request | Implemented | Session replay fixtures |')
    $rows.Add('| --- | --- | --- |')

    foreach ($request in $requestCommandMap.GetEnumerator() | Sort-Object Value) {
        $command = $request.Value
        $implemented = if ($handlerRows.ContainsKey($command)) {
            $handlerPath = $handlerRows[$command]
            $handlerFile = Split-Path -Leaf $handlerPath
            "[$handlerFile]($LinkPrefix$handlerPath)"
        }
        else {
            'No'
        }

        $fixtures = foreach ($fixture in $fixtureContents.GetEnumerator() | Sort-Object Name) {
            if ($fixture.Value -match ('"command"\s*:\s*"' + [regex]::Escape($command) + '"')) {
                "[$($fixture.Key)]($($LinkPrefix)tests/replay/data/$($fixture.Key))"
            }
        }

        $fixtureText = if ($fixtures.Count -gt 0) { $fixtures -join ', ' } else { '' }
        $rows.Add("| ``$command`` | $implemented | $fixtureText |")
    }

    return ($rows -join "`n")
}

if ($Stdout) {
    Build-MatrixTable -LinkPrefix '' | Write-Output
    return
}

# Compose the full documentation page. Keep it deterministic (no timestamps) so
# -Check can compare by string equality. Links resolve on the published site.
$table = Build-MatrixTable -LinkPrefix $blobBase

$page = @"
<!-- Generated by scripts/Get-DapRequestMatrix.ps1. Do not edit by hand.
     Regenerate with ``npm run matrix``; verify freshness with ``npm run matrix:check``. -->

# Request coverage

This page maps every Debug Adapter Protocol request to its handler and the
recorded replay sessions that exercise it. It is the coverage map used by the
``dap-request-coverage`` workflow.

It is generated from three live sources, so it never drifts from the code:

- the generated dispatch in ``src/protocol/dap_service.h`` (the full request surface),
- the handler partials ``src/service/dap_server_<command>.cpp`` (what is implemented),
- the replay fixtures under ``tests/replay/data`` (what realistic traffic verifies).

A request with no fixture is not necessarily untested: argument validation and
pure helpers are covered by unit tests below the request surface. It does, though,
have no end-to-end replay of the command flow. The ``dap-request-coverage`` skill
explains how to classify such a gap and whether it needs a recorded session, a
helper unit test, or removal as dead code.

This page is generated. Regenerate it with ``npm run matrix`` after adding a
handler or a fixture; ``npm run matrix:check`` (run in CI) fails if it is out of
date.

$table
"@

# Normalize to LF so the committed page matches the repo's Markdown line endings
# and -Check is independent of the working-tree checkout style.
$page = ($page -replace "`r`n", "`n").TrimEnd() + "`n"
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)

if ($Check) {
    if (-not (Test-Path $OutFile)) {
        Write-Warning "Coverage page is missing: $OutFile"
        Write-Warning 'Run `npm run matrix` to generate it.'
        exit 1
    }

    $current = ([System.IO.File]::ReadAllText($OutFile) -replace "`r`n", "`n").TrimEnd() + "`n"
    if ($current -ne $page) {
        Write-Warning "Coverage page is out of date: $OutFile"
        Write-Warning 'Run `npm run matrix` and commit the result.'
        exit 1
    }

    Write-Host "Coverage page is up to date: $OutFile"
    return
}

$outDir = Split-Path -Parent $OutFile
if ($outDir -and -not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}

[System.IO.File]::WriteAllText($OutFile, $page, $utf8NoBom)
Write-Host "Wrote coverage page: $OutFile"
