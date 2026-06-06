[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Path,

    [switch]$IncludeOutput,

    [ValidateRange(40, 1000)]
    [int]$MaxOutputLength = 160
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Format-Preview {
    param(
        [AllowNull()]
        [string]$Text,
        [int]$Limit
    )

    if ([string]::IsNullOrWhiteSpace($Text)) {
        return ''
    }

    $singleLine = ($Text -replace '\s+', ' ').Trim()
    if ($singleLine.Length -le $Limit) {
        return $singleLine
    }

    return $singleLine.Substring(0, $Limit - 3) + '...'
}

$resolvedPath = Resolve-Path -LiteralPath $Path
$session = Get-Content -LiteralPath $resolvedPath -Raw | ConvertFrom-Json -Depth 100

if (-not $session.messages) {
    throw "Session file '$resolvedPath' does not contain a 'messages' array."
}

$requestCommands = @{}
$index = 0

foreach ($entry in $session.messages) {
    $index++
    $message = $entry.message
    $direction = if ($entry.direction -eq 'in') { '>>' } else { '<<' }

    switch ($message.type) {
        'request' {
            $requestCommands[[int]$message.seq] = [string]$message.command
            $argumentKeys = @()
            if ($message.PSObject.Properties.Name -contains 'arguments' -and $null -ne $message.arguments) {
                $argumentKeys = @($message.arguments.PSObject.Properties.Name | Sort-Object)
            }

            $details = if ($argumentKeys.Count -gt 0) {
                ' args=[' + ($argumentKeys -join ', ') + ']'
            }
            else {
                ''
            }

            '{0,3} {1} request  seq={2,-4} command={3}{4}' -f $index, $direction, $message.seq, $message.command, $details
            continue
        }

        'response' {
            $status = if ($message.success) { 'ok  ' } else { 'FAIL' }
            $command = [string]$message.command
            if ([string]::IsNullOrWhiteSpace($command) -and $requestCommands.ContainsKey([int]$message.request_seq)) {
                $command = $requestCommands[[int]$message.request_seq]
            }

            $details = ''
            if (-not $message.success -and $null -ne $message.body -and $null -ne $message.body.error) {
                $details = ' error=' + (Format-Preview -Text ([string]$message.body.error.format) -Limit $MaxOutputLength)
            }

            '{0,3} {1} response req={2,-4} command={3,-20} status={4}{5}' -f $index, $direction, $message.request_seq, $command, $status, $details
            continue
        }

        'event' {
            if ($message.event -eq 'output' -and -not $IncludeOutput) {
                continue
            }

            $details = switch ($message.event) {
                'stopped' {
                    ' reason=' + $message.body.reason + ' threadId=' + $message.body.threadId + ' desc=' + (Format-Preview -Text ([string]$message.body.description) -Limit $MaxOutputLength)
                }
                'continued' {
                    ' threadId=' + $message.body.threadId + ' allThreadsContinued=' + $message.body.allThreadsContinued
                }
                'thread' {
                    ' reason=' + $message.body.reason + ' threadId=' + $message.body.threadId
                }
                'process' {
                    ' name=' + (Format-Preview -Text ([string]$message.body.name) -Limit $MaxOutputLength) + ' startMethod=' + $message.body.startMethod
                }
                'exited' {
                    ' exitCode=' + $message.body.exitCode
                }
                'output' {
                    ' category=' + $message.body.category + ' text=' + (Format-Preview -Text ([string]$message.body.output) -Limit $MaxOutputLength)
                }
                default {
                    ''
                }
            }

            '{0,3} {1} event    seq={2,-4} name={3}{4}' -f $index, $direction, $message.seq, $message.event, $details
            continue
        }

        default {
            '{0,3} {1} {2}' -f $index, $direction, (ConvertTo-Json -InputObject $message -Compress -Depth 20)
        }
    }
}