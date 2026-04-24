param(
    [Parameter(Mandatory = $true)]
    [string] $InputCmo,

    [Parameter(Mandatory = $true)]
    [string] $OutputCmo,

    [string] $NmoExe = "",

    [string] $Manifest = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Resolve-RepoRoot {
    $scriptDir = Split-Path -Parent $PSCommandPath
    return (Resolve-Path -LiteralPath (Join-Path $scriptDir "..\..")).Path
}

function Resolve-ToolPath {
    param([string] $RepoRoot, [string] $RequestedPath)

    if ($RequestedPath -ne "") {
        return (Resolve-Path -LiteralPath $RequestedPath).Path
    }

    $defaultPath = Join-Path $RepoRoot "build\tools\nmo.exe"
    return (Resolve-Path -LiteralPath $defaultPath).Path
}

function Invoke-Nmo {
    param(
        [string] $Exe,
        [string[]] $Arguments
    )

    & $Exe @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "nmo failed with exit code ${LASTEXITCODE}: $($Arguments -join ' ')"
    }
}

$repoRoot = Resolve-RepoRoot
$nmo = Resolve-ToolPath -RepoRoot $repoRoot -RequestedPath $NmoExe

if ($Manifest -eq "") {
    $Manifest = Join-Path $repoRoot "tools\ballance\base_ballance_bb_manifest.json"
}

$manifestPath = (Resolve-Path -LiteralPath $Manifest).Path
$inputPath = (Resolve-Path -LiteralPath $InputCmo).Path
$outputPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputCmo)
$outputDir = Split-Path -Parent $outputPath
if ($outputDir -and !(Test-Path -LiteralPath $outputDir)) {
    New-Item -ItemType Directory -Path $outputDir | Out-Null
}

$manifestData = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$operations = @()
foreach ($replacement in $manifestData.replacements) {
    $operations += [ordered] @{
        op = "replace_bb"
        behavior_id = [int] $replacement.behavior_id
        name = [string] $replacement.name
        guid = [string] $replacement.guid
        version = [int] $replacement.version
        preserve_links = [bool] $replacement.preserve_links
        preserve_params = [bool] $replacement.preserve_params
    }
}

$patch = [ordered] @{
    version = 1
    input = $inputPath
    output = $outputPath
    operations = $operations
}

$tempPatch = Join-Path ([System.IO.Path]::GetTempPath()) ("nmo-ballance-base-bb-{0}.json" -f ([System.Guid]::NewGuid().ToString("N")))
try {
    $json = $patch | ConvertTo-Json -Depth 8
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($tempPatch, $json, $utf8NoBom)

    Invoke-Nmo -Exe $nmo -Arguments @("patch", "diff", $tempPatch)
    Invoke-Nmo -Exe $nmo -Arguments @("patch", "apply", $tempPatch)
    Invoke-Nmo -Exe $nmo -Arguments @("validate", "all", $outputPath)

    foreach ($replacement in $manifestData.replacements) {
        Invoke-Nmo -Exe $nmo -Arguments @("behavior", "show", "--id", ([string] $replacement.behavior_id), $outputPath)
    }
} finally {
    Remove-Item -LiteralPath $tempPatch -Force -ErrorAction SilentlyContinue
}
