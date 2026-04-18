param(
    [string]$ExporterPath = $env:VIRTOOLS_DATA_EXPORTER,
    [string]$GameRoot = $env:VIRTOOLS_GAME_ROOT,
    [string]$OutputDir = "",
    [string[]]$ExtraPluginDirs = @(),
    [switch]$Check
)

$ErrorActionPreference = "Stop"

function Resolve-Root {
    $scriptDir = Split-Path -Parent $PSCommandPath
    return (Resolve-Path (Join-Path $scriptDir "..\..")).Path
}

function Resolve-Exporter {
    param([string]$Path)

    if ($Path -and (Test-Path -LiteralPath $Path)) {
        return (Resolve-Path -LiteralPath $Path).Path
    }

    $command = Get-Command "VirtoolsDataExporter.exe" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw "VirtoolsDataExporter.exe not found. Pass -ExporterPath, set VIRTOOLS_DATA_EXPORTER, or add it to PATH."
}

function Resolve-GameRoot {
    param([string]$Root, [string]$Exporter)

    if ($Root -and (Test-Path -LiteralPath $Root)) {
        return (Resolve-Path -LiteralPath $Root).Path
    }

    $parent = Split-Path -Parent $Exporter
    if ((Split-Path -Leaf $parent) -ieq "Bin") {
        $candidate = Split-Path -Parent $parent
        if (Test-Path -LiteralPath $candidate) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw "Ballance root not found. Pass -GameRoot or set VIRTOOLS_GAME_ROOT."
}

function Get-Dlls {
    param([string[]]$Dirs)

    $result = @()
    foreach ($dir in $Dirs) {
        if ($dir -and (Test-Path -LiteralPath $dir)) {
            $result += Get-ChildItem -LiteralPath $dir -Filter "*.dll" -File |
                Sort-Object FullName |
                ForEach-Object { $_.FullName }
        }
    }
    return $result
}

function Write-NormalizedJson {
    param([string]$Path)

    $value = Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
    $json = $value | ConvertTo-Json -Depth 100
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $json + "`n", $utf8NoBom)
}

function Format-GuidHex {
    param($Guid)

    return "0x{0:X8}-0x{1:X8}" -f [uint32]$Guid[0], [uint32]$Guid[1]
}

function Get-ParamSignature {
    param($Entry)

    $parts = @($Entry.name, $Entry.category, $Entry.size)
    if ($Entry.values) {
        $valueText = ($Entry.values | ForEach-Object { "$($_.name)=$($_.value)" }) -join ";"
        $parts += "values:$valueText"
    }
    if ($Entry.members) {
        $memberText = ($Entry.members | ForEach-Object { "$($_.name):$($_.type_name)" }) -join ";"
        $parts += "members:$memberText"
    }
    return ($parts -join "|")
}

function Stabilize-ParameterGuids {
    param([string]$Path, [string]$ExistingPath)

    if (-not (Test-Path -LiteralPath $ExistingPath)) {
        return
    }

    $current = Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
    $existing = Get-Content -LiteralPath $ExistingPath -Raw -Encoding UTF8 | ConvertFrom-Json
    $stableBySignature = @{}
    foreach ($entry in $existing) {
        if ($entry.values -or $entry.members) {
            $stableBySignature[(Get-ParamSignature $entry)] = @($entry.guid[0], $entry.guid[1])
        }
    }

    $guidMap = @{}
    foreach ($entry in $current) {
        $signature = Get-ParamSignature $entry
        if ($stableBySignature.ContainsKey($signature)) {
            $oldGuid = @($entry.guid[0], $entry.guid[1])
            $newGuid = $stableBySignature[$signature]
            $guidMap["$($oldGuid[0]):$($oldGuid[1])"] = $newGuid
            $entry.guid = @([uint32]$newGuid[0], [uint32]$newGuid[1])
            $entry.guid_hex = Format-GuidHex $newGuid
        }
    }

    foreach ($entry in $current) {
        if ($entry.members) {
            foreach ($member in $entry.members) {
                if ($member.type_guid -and $member.type_guid.Count -ge 2) {
                    $key = "$($member.type_guid[0]):$($member.type_guid[1])"
                    if ($guidMap.ContainsKey($key)) {
                        $member.type_guid = @([uint32]$guidMap[$key][0], [uint32]$guidMap[$key][1])
                    }
                }
            }
        }
    }

    $json = $current | ConvertTo-Json -Depth 100
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllText($Path, $json + "`n", $utf8NoBom)
}

function Get-JsonEntryKey {
    param($Entry)

    $guid = ""
    if ($Entry.guid -and $Entry.guid.Count -ge 2) {
        $guid = "{0:X8}-{1:X8}" -f [uint32]$Entry.guid[0], [uint32]$Entry.guid[1]
    }
    $name = if ($Entry.name) { $Entry.name } elseif ($Entry.description) { $Entry.description } else { "" }
    $dll = if ($Entry.dll) { $Entry.dll } else { "" }
    $json = $Entry | ConvertTo-Json -Depth 100 -Compress
    return "$guid|$name|$dll|$json"
}

function Read-SemanticJsonText {
    param([string]$Path)

    $value = Get-Content -LiteralPath $Path -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($value -is [System.Array]) {
        return (($value | ForEach-Object { Get-JsonEntryKey $_ } | Sort-Object) -join "`n") + "`n"
    }
    return (($value | ConvertTo-Json -Depth 100 -Compress) + "`n")
}

function Copy-Or-Check {
    param([string]$Source, [string]$Destination, [switch]$CheckOnly)

    Write-NormalizedJson -Path $Source
    if ($CheckOnly) {
        if (-not (Test-Path -LiteralPath $Destination)) {
            throw "Missing generated data file: $Destination"
        }
        $newText = Read-SemanticJsonText -Path $Source
        $oldText = Read-SemanticJsonText -Path $Destination
        if ($newText -cne $oldText) {
            throw "Generated data is stale: $Destination"
        }
    } else {
        Copy-Item -LiteralPath $Source -Destination $Destination -Force
    }
}

$repoRoot = Resolve-Root
$exporter = Resolve-Exporter -Path $ExporterPath
$game = Resolve-GameRoot -Root $GameRoot -Exporter $exporter
$gameBin = Join-Path $game "Bin"
if (Test-Path -LiteralPath $gameBin) {
    $env:PATH = $gameBin + [System.IO.Path]::PathSeparator + $env:PATH
}

if (-not $OutputDir) {
    $OutputDir = Join-Path $repoRoot "data"
}
$OutputDir = (Resolve-Path -LiteralPath $OutputDir).Path

$help = & $exporter --help 2>&1
if (($help -join "`n") -notmatch "\s-g\s+plugins\.json") {
    throw "Exporter does not support '-g plugins.json': $exporter"
}

$managerDlls = Get-Dlls -Dirs @((Join-Path $game "Managers"))
$pluginDlls = Get-Dlls -Dirs @(
    (Join-Path $game "Plugins"),
    (Join-Path $game "RenderEngines"),
    (Join-Path $game "BuildingBlocks"),
    (Join-Path $game "BuildingBlocksX")
)
if ($ExtraPluginDirs.Count -gt 0) {
    $pluginDlls += Get-Dlls -Dirs $ExtraPluginDirs
    $pluginDlls = $pluginDlls | Sort-Object -Unique
}

if ($managerDlls.Count -eq 0) {
    throw "No manager DLLs found under $game\Managers"
}
if ($pluginDlls.Count -eq 0) {
    throw "No plugin DLLs found under $game"
}

$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("libnmo-virtools-export-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null

try {
    $paramJson = Join-Path $tempRoot "virtools_parameter_types.json"
    $opsJson = Join-Path $tempRoot "virtools_operation_types.json"
    $bbsJson = Join-Path $tempRoot "virtools_building_blocks.json"
    $pluginsJson = Join-Path $tempRoot "virtools_plugins.json"

    $args = @("-p", $paramJson, "-o", $opsJson, "-b", $bbsJson, "-g", $pluginsJson)
    foreach ($manager in $managerDlls) {
        $args += @("--manager", $manager)
    }
    $args += $pluginDlls

    & $exporter @args
    if ($LASTEXITCODE -ne 0) {
        throw "VirtoolsDataExporter failed with exit code $LASTEXITCODE"
    }

    foreach ($file in @($paramJson, $opsJson, $bbsJson, $pluginsJson)) {
        if (-not (Test-Path -LiteralPath $file)) {
            throw "Exporter did not produce $(Split-Path -Leaf $file)"
        }
    }

    $paramDest = Join-Path $OutputDir "virtools_parameter_types.json"
    Stabilize-ParameterGuids -Path $paramJson -ExistingPath $paramDest

    Copy-Or-Check -Source $paramJson -Destination $paramDest -CheckOnly:$Check
    Copy-Or-Check -Source $opsJson -Destination (Join-Path $OutputDir "virtools_operation_types.json") -CheckOnly:$Check
    Copy-Or-Check -Source $bbsJson -Destination (Join-Path $OutputDir "virtools_building_blocks.json") -CheckOnly:$Check
    Copy-Or-Check -Source $pluginsJson -Destination (Join-Path $OutputDir "virtools_plugins.json") -CheckOnly:$Check

    $mode = if ($Check) { "checked" } else { "updated" }
    Write-Host "Virtools data $mode in $OutputDir"
    Write-Host "Exporter: $exporter"
    Write-Host "Game root: $game"
    Write-Host "Managers: $($managerDlls.Count)"
    Write-Host "Plugins and building blocks: $($pluginDlls.Count)"
} finally {
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}
