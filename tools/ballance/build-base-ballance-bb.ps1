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
if ($manifestData.PSObject.Properties.Name -contains "replacements") {
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
}

if ($manifestData.PSObject.Properties.Name -contains "folds") {
    foreach ($fold in $manifestData.folds) {
        $operation = [ordered] @{
            op = "fold"
            parent = [int] $fold.parent
            nodes = @($fold.nodes | ForEach-Object { [int] $_ })
            anchor = [int] $fold.anchor
            name = [string] $fold.name
            guid = [string] $fold.guid
            version = [int] $fold.version
            preserve_boundary = [bool] $fold.preserve_boundary
        }
        if ($fold.PSObject.Properties.Name -contains "inputs") {
            $operation.inputs = @($fold.inputs | ForEach-Object {
                [ordered] @{
                    old_index = [int] $_.old_index
                    new_index = [int] $_.new_index
                }
            })
        }
        if ($fold.PSObject.Properties.Name -contains "outputs") {
            $operation.outputs = @($fold.outputs | ForEach-Object {
                [ordered] @{
                    old_index = [int] $_.old_index
                    new_index = [int] $_.new_index
                }
            })
        }
        if ($fold.PSObject.Properties.Name -contains "parameters") {
            $operation.parameters = @($fold.parameters | ForEach-Object {
                [ordered] @{
                    old_index = [int] $_.old_index
                    new_index = [int] $_.new_index
                }
            })
        }
        if ($fold.PSObject.Properties.Name -contains "interface") {
            $operation.interface = [string] $fold.interface
        }
        $operations += $operation
    }
}

$patch = [ordered] @{
    version = 2
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

    if ($manifestData.PSObject.Properties.Name -contains "replacements") {
        foreach ($replacement in $manifestData.replacements) {
            Invoke-Nmo -Exe $nmo -Arguments @("behavior", "show", "--id", ([string] $replacement.behavior_id), $outputPath)
        }
    }
    if ($manifestData.PSObject.Properties.Name -contains "folds") {
        foreach ($fold in $manifestData.folds) {
            Invoke-Nmo -Exe $nmo -Arguments @("behavior", "show", "--id", ([string] $fold.anchor), $outputPath)
        }
    }
} finally {
    Remove-Item -LiteralPath $tempPatch -Force -ErrorAction SilentlyContinue
}
