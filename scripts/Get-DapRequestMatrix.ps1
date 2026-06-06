param(
    [string]$RootPath = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'

# C++ layout:
#   requests : generated dispatch in src/protocol/dap_service.h
#              (one `virtual void handle_<name>_request(const <Type>Request &request)`
#              per DAP request; the wire command string is the argument to the
#              `unhandled_dap_request(request.seq, "<command>")` default-throw)
#   handlers : src/service/dap_server_<command>.cpp partials, each defining
#              `void dap_server::handle_<name>_request(...)`
#   fixtures : tests/replay/data/*.json (recorded replay sessions)
$root = (Resolve-Path $RootPath).Path
$servicePath = Join-Path $root 'src\protocol\dap_service.h'
$handlersPath = Join-Path $root 'src\service'
$fixturesPath = Join-Path $root 'tests\replay\data'

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

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add('| Request | Implemented | Session replay fixtures |')
$lines.Add('| --- | --- | --- |')

foreach ($request in $requestCommandMap.GetEnumerator() | Sort-Object Value) {
    $command = $request.Value
    $implemented = if ($handlerRows.ContainsKey($command)) {
        $handlerPath = $handlerRows[$command]
        $handlerFile = Split-Path -Leaf $handlerPath
        "[$handlerFile]($handlerPath)"
    }
    else {
        'No'
    }

    $fixtures = foreach ($fixture in $fixtureContents.GetEnumerator() | Sort-Object Name) {
        if ($fixture.Value -match ('"command"\s*:\s*"' + [regex]::Escape($command) + '"')) {
            "[$($fixture.Key)](tests/replay/data/$($fixture.Key))"
        }
    }

    $fixtureText = if ($fixtures.Count -gt 0) { $fixtures -join ', ' } else { '' }
    $lines.Add("| ``$command`` | $implemented | $fixtureText |")
}

$lines -join [Environment]::NewLine
