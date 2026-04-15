param(
    [string]$Root = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
)

$ErrorActionPreference = 'Stop'

function Assert-Exists {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path)) {
        throw "Missing required file: $Path"
    }
}

function Assert-Contains {
    param(
        [string]$Path,
        [string]$Pattern
    )
    $content = Get-Content -LiteralPath $Path -Raw
    if ($content -notmatch $Pattern) {
        throw "Expected $Path to contain pattern: $Pattern"
    }
}

$toolRoot = Join-Path $Root 'tools\frida\dev25'
$wrapper = Join-Path $toolRoot 'Invoke-Dev25Probe.ps1'
$profile = Join-Path $toolRoot 'profiles\dev25_0_48.json'
$agent = Join-Path $toolRoot 'agent\main.js'
$readme = Join-Path $toolRoot 'README.md'

Assert-Exists $wrapper
Assert-Exists $profile
Assert-Exists $agent
Assert-Exists $readme

$profileData = Get-Content -LiteralPath $profile -Raw | ConvertFrom-Json
if ($profileData.sha256 -ne 'C4BC2724165730B9CB0EFB8C481D05783BF3DBD7929698C3F863AC06ABEAD4B2') {
    throw "Unexpected Dev.exe SHA256 in profile: $($profileData.sha256)"
}
if ($profileData.idaBase -ne '0x400000') {
    throw "Unexpected IDA base in profile: $($profileData.idaBase)"
}

Assert-Contains $wrapper 'trigger-rebuild'
Assert-Contains $wrapper 'AllowMutation'
Assert-Contains $agent '__DEV25_EVENT__'
Assert-Contains $agent 'CUIKSchematicView_ReconstructScriptUIFromRuntimeGraph'
Assert-Contains $readme 'Mode trigger-rebuild'

Write-Host 'Dev25 Frida probe static checks passed.'
