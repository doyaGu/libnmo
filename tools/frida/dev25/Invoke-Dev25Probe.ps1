[CmdletBinding()]
param(
    [ValidateSet('trace', 'inventory', 'trigger-rebuild', 'link-records', 'language')]
    [string]$Mode = 'trace',

    [string]$DevPath = 'C:\Users\kakut\Works\Virtools\Virtools 2.5.0.48\Dev.exe',

    [string]$Sample,

    [int]$AttachPid = 0,

    [string]$TargetName,

    [switch]$AllowMutation,

    [switch]$DumpLinkRecords,

    [ValidateSet('after-file-loaded', 'after-schematic-load')]
    [string]$TriggerPoint = 'after-file-loaded',

    [ValidateSet('Inline', 'Deferred')]
    [string]$TriggerCallStyle = 'Deferred',

    [int]$TimeoutSec = 45,

    [switch]$KillOnExit,

    [switch]$KillExistingDev,

    [string]$LogRoot
)

$ErrorActionPreference = 'Stop'

function Resolve-RepoRoot {
    return (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
}

function ConvertTo-CompressedJson {
    param([object]$Value)
    return ($Value | ConvertTo-Json -Depth 32 -Compress)
}

function Read-EventsFromLog {
    param([string]$Path)

    if (-not (Test-Path -LiteralPath $Path)) {
        return @()
    }

    $events = New-Object System.Collections.Generic.List[object]
    foreach ($line in Get-Content -LiteralPath $Path) {
        $idx = $line.IndexOf('__DEV25_EVENT__')
        if ($idx -lt 0) {
            continue
        }
        $json = $line.Substring($idx + '__DEV25_EVENT__'.Length)
        try {
            $events.Add(($json | ConvertFrom-Json))
        } catch {
            Write-Warning "Failed to parse event line: $line"
        }
    }
    return $events
}

function Write-JsonFile {
    param(
        [string]$Path,
        [object]$Value
    )
    $Value | ConvertTo-Json -Depth 32 | Set-Content -LiteralPath $Path -Encoding UTF8
}

$repoRoot = Resolve-RepoRoot
if (-not $LogRoot) {
    $LogRoot = Join-Path $repoRoot '.ida-analysis\frida\runs'
}

$profilePath = Join-Path $PSScriptRoot 'profiles\dev25_0_48.json'
$agentPath = Join-Path $PSScriptRoot 'agent\main.js'
$profile = Get-Content -LiteralPath $profilePath -Raw | ConvertFrom-Json

if ($AttachPid -eq 0) {
    if (-not (Test-Path -LiteralPath $DevPath)) {
        throw "Dev.exe not found: $DevPath"
    }
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $DevPath).Hash
    if ($hash -ne $profile.sha256) {
        throw "Dev.exe SHA256 mismatch. Expected $($profile.sha256), got $hash for $DevPath"
    }
    if (-not $Sample) {
        throw 'Sample is required when spawning Dev.exe.'
    }
    if (-not (Test-Path -LiteralPath $Sample)) {
        throw "Sample not found: $Sample"
    }
}

if (($Mode -eq 'trigger-rebuild' -or $Mode -eq 'link-records') -and -not $AllowMutation) {
    Write-Warning "$Mode can observe hooks without mutation, but will not clear UI map or call rebuild unless -AllowMutation is set."
}

if ($KillExistingDev -and $AttachPid -eq 0) {
    Get-Process Dev -ErrorAction SilentlyContinue | Stop-Process -Force
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$runName = "$stamp-$Mode"
$runDir = Join-Path $LogRoot $runName
New-Item -ItemType Directory -Force -Path $runDir | Out-Null

$config = [ordered]@{
    mode = $Mode
    moduleName = $profile.moduleName
    idaBase = $profile.idaBase
    sha256 = $profile.sha256
    addresses = $profile.addresses
    sample = $Sample
    targetName = $TargetName
    allowMutation = [bool]$AllowMutation
    dumpLinkRecords = [bool]$DumpLinkRecords -or ($Mode -eq 'link-records')
    triggerPoint = $TriggerPoint
    triggerCallStyle = $TriggerCallStyle
    maxLinkRecords = 2000
    rowIndex = -1
}

$generatedAgent = Join-Path $runDir 'agent.generated.js'
$configJson = ConvertTo-CompressedJson $config
$agentBody = Get-Content -LiteralPath $agentPath -Raw
@(
    "const DEV25_PROBE_CONFIG = $configJson;"
    $agentBody
) | Set-Content -LiteralPath $generatedAgent -Encoding ASCII

$fridaLog = Join-Path $runDir 'frida.log'
$eventsJsonl = Join-Path $runDir 'events.jsonl'
$summaryPath = Join-Path $runDir 'summary.json'
$configPath = Join-Path $runDir 'config.json'
Write-JsonFile -Path $configPath -Value $config

$fridaArgs = New-Object System.Collections.Generic.List[string]
$fridaArgs.Add('-q')
$fridaArgs.Add('--timeout')
$fridaArgs.Add([string]$TimeoutSec)
$fridaArgs.Add('--runtime')
$fridaArgs.Add('v8')
if ($KillOnExit) {
    $fridaArgs.Add('--kill-on-exit')
}
if ($AttachPid -ne 0) {
    $fridaArgs.Add('-p')
    $fridaArgs.Add([string]$AttachPid)
} else {
    $fridaArgs.Add('-f')
    $fridaArgs.Add($DevPath)
}
$fridaArgs.Add('-l')
$fridaArgs.Add($generatedAgent)
$fridaArgs.Add('-o')
$fridaArgs.Add($fridaLog)
if ($AttachPid -eq 0) {
    $fridaArgs.Add('--')
    $fridaArgs.Add($Sample)
}

Write-Host "Running Frida probe: mode=$Mode runDir=$runDir"
& frida @fridaArgs
$fridaExitCode = $LASTEXITCODE

$events = @(Read-EventsFromLog -Path $fridaLog)
$eventLines = foreach ($event in $events) {
    ConvertTo-CompressedJson $event
}
$eventLines | Set-Content -LiteralPath $eventsJsonl -Encoding UTF8

$typeCounts = @{}
foreach ($event in $events) {
    $key = [string]$event.type
    if (-not $typeCounts.ContainsKey($key)) {
        $typeCounts[$key] = 0
    }
    $typeCounts[$key]++
}

$triggerEvents = @($events | Where-Object { $_.type -eq 'trigger-rebuild' })
$readyEvents = @($events | Where-Object { $_.type -eq 'ready' })
$behaviorEvents = @($events | Where-Object { $_.type -eq 'behavior' })
$linkRecordEvents = @($events | Where-Object { $_.type -eq 'link-record' })
$blockedEvents = @($events | Where-Object { $_.type -eq 'trigger-blocked' -or $_.type -eq 'trigger-skipped' })

$summary = [ordered]@{
    mode = $Mode
    runDir = $runDir
    fridaExitCode = $fridaExitCode
    eventCount = $events.Count
    behaviorEventCount = $behaviorEvents.Count
    linkRecordEventCount = $linkRecordEvents.Count
    typeCounts = $typeCounts
    ready = if ($readyEvents.Count -gt 0) { $readyEvents[-1].data } else { $null }
    trigger = if ($triggerEvents.Count -gt 0) { $triggerEvents[-1].data } else { $null }
    triggerBlockedOrSkipped = if ($blockedEvents.Count -gt 0) { $blockedEvents[-1].data } else { $null }
    files = [ordered]@{
        config = $configPath
        generatedAgent = $generatedAgent
        fridaLog = $fridaLog
        eventsJsonl = $eventsJsonl
        summary = $summaryPath
    }
}
Write-JsonFile -Path $summaryPath -Value $summary

Write-Host "Probe complete: exit=$fridaExitCode events=$($events.Count) summary=$summaryPath"
if ($triggerEvents.Count -gt 0) {
    $trigger = $triggerEvents[-1].data
    Write-Host "Rebuild trigger: target=$($trigger.targetName) ret=$($trigger.rebuildReturn) uiBefore=$($trigger.uiBefore) uiAfterRemove=$($trigger.uiAfterRemove) uiAfterRebuild=$($trigger.uiAfterRebuild)"
}
if ($blockedEvents.Count -gt 0) {
    $blocked = $blockedEvents[-1].data
    Write-Host "Trigger not executed: $($blocked.reason)"
}

exit $fridaExitCode
