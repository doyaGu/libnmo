param(
    [Parameter(Mandatory = $true)]
    [string]$Version,

    [string]$BuildDir = "build_package_release_static",
    [string]$DistDir = "dist",
    [string]$SourceDir = ""
)

$ErrorActionPreference = "Stop"

function Invoke-Step {
    param(
        [string]$Name,
        [scriptblock]$Body
    )
    Write-Host "==> $Name"
    & $Body
}

function Invoke-Checked {
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$WorkingDirectory = $SourceDir
    )
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed ($LASTEXITCODE): $FilePath $($Arguments -join ' ')"
    }
}

function Assert-UnderPath {
    param(
        [string]$Parent,
        [string]$Child
    )
    $parentFull = [System.IO.Path]::GetFullPath($Parent).TrimEnd('\', '/')
    $childFull = [System.IO.Path]::GetFullPath($Child).TrimEnd('\', '/')
    if (-not $childFull.StartsWith($parentFull, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to operate outside expected directory: $childFull"
    }
}

if (-not $SourceDir) {
    $SourceDir = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
}
$SourceDir = [System.IO.Path]::GetFullPath($SourceDir)
$BuildDir = [System.IO.Path]::GetFullPath((Join-Path $SourceDir $BuildDir))
$DistDir = [System.IO.Path]::GetFullPath((Join-Path $SourceDir $DistDir))
$PackageName = "libnmo-$Version-windows-mingw-x64"
$InstallPrefix = Join-Path $BuildDir "_install"
$PackageRoot = Join-Path $DistDir $PackageName
$ZipPath = Join-Path $DistDir "$PackageName.zip"

Assert-UnderPath -Parent $SourceDir -Child $BuildDir
Assert-UnderPath -Parent $SourceDir -Child $DistDir

Invoke-Step "Configure Release build" {
    Invoke-Checked "cmake" @(
        "-S", $SourceDir,
        "-B", $BuildDir,
        "-DCMAKE_BUILD_TYPE=Release",
        "-DNMO_BUILD_TESTS=ON",
        "-DNMO_BUILD_TOOLS=ON",
        "-DNMO_BUILD_EXAMPLES=OFF",
        "-DNMO_BUILD_SHARED=OFF",
        "-DNMO_MINGW_STATIC_RUNTIME=ON",
        "-DCMAKE_INSTALL_PREFIX=$InstallPrefix"
    )
}

Invoke-Step "Build" {
    Invoke-Checked "cmake" @("--build", $BuildDir, "--config", "Release", "-j")
}

Invoke-Step "Run tests" {
    Invoke-Checked "ctest" @("--test-dir", $BuildDir, "--output-on-failure")
}

Invoke-Step "Install staging tree" {
    if (Test-Path $InstallPrefix) {
        Assert-UnderPath -Parent $BuildDir -Child $InstallPrefix
        Remove-Item -LiteralPath $InstallPrefix -Recurse -Force
    }
    Invoke-Checked "cmake" @("--build", $BuildDir, "--config", "Release", "--target", "install")
}

Invoke-Step "Assemble package tree" {
    New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
    if (Test-Path $PackageRoot) {
        Assert-UnderPath -Parent $DistDir -Child $PackageRoot
        Remove-Item -LiteralPath $PackageRoot -Recurse -Force
    }
    Copy-Item -LiteralPath $InstallPrefix -Destination $PackageRoot -Recurse

    $DocDir = Join-Path $PackageRoot "share\doc\libnmo"
    $ThirdPartyDir = Join-Path $DocDir "third-party"
    New-Item -ItemType Directory -Force -Path $ThirdPartyDir | Out-Null
    foreach ($doc in @("README.md", "CONTRIBUTING.md", "CHANGELOG.md", "LICENSE")) {
        $src = Join-Path $SourceDir $doc
        if (Test-Path $src) {
            Copy-Item -LiteralPath $src -Destination $DocDir -Force
        }
    }
    Copy-Item -LiteralPath (Join-Path $SourceDir "deps\miniz\LICENSE") -Destination (Join-Path $ThirdPartyDir "miniz-LICENSE") -Force
    Copy-Item -LiteralPath (Join-Path $SourceDir "deps\yyjson\LICENSE") -Destination (Join-Path $ThirdPartyDir "yyjson-LICENSE") -Force

    $LibDir = Join-Path $PackageRoot "lib"
    New-Item -ItemType Directory -Force -Path $LibDir | Out-Null
    $minizLib = Get-ChildItem -LiteralPath $BuildDir -Recurse -Filter "libminiz.a" | Select-Object -First 1
    if (-not $minizLib) {
        throw "Could not find libminiz.a under $BuildDir"
    }
    Copy-Item -LiteralPath $minizLib.FullName -Destination $LibDir -Force

    $ManifestPath = Join-Path $PackageRoot "MANIFEST.txt"
    Get-ChildItem -LiteralPath $PackageRoot -Recurse -File |
        ForEach-Object { $_.FullName.Substring($PackageRoot.Length + 1).Replace('\', '/') } |
        Sort-Object |
        Set-Content -LiteralPath $ManifestPath -Encoding UTF8
}

Invoke-Step "Verify package" {
    $exe = Join-Path $PackageRoot "bin\nmo.exe"
    if (-not (Test-Path $exe)) {
        throw "Missing packaged nmo.exe"
    }
    $dataDir = Join-Path $PackageRoot "share\libnmo\data"
    $pluginData = Join-Path $dataDir "virtools_plugins.json"
    if (-not (Test-Path $pluginData)) {
        throw "Missing Virtools plugin metadata: $pluginData"
    }

    $pthreadDll = Get-ChildItem -LiteralPath $PackageRoot -Recurse -File -Filter "libwinpthread-1.dll"
    if ($pthreadDll) {
        throw "Package contains libwinpthread-1.dll"
    }

    $objdump = (Get-Command "objdump" -ErrorAction Stop).Source
    $imports = & $objdump -p $exe
    if ($LASTEXITCODE -ne 0) {
        throw "objdump failed for $exe"
    }
    if (($imports -join "`n") -match "libwinpthread") {
        throw "nmo.exe imports libwinpthread"
    }

    & $exe --help | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Packaged nmo.exe --help failed"
    }

    $sampleFile = Join-Path $SourceDir "data\Demo\Tunnel.cmo"
    if (Test-Path $sampleFile) {
        $oldDataDir = $env:NMO_DATA_DIR
        try {
            $env:NMO_DATA_DIR = $dataDir
            $pluginsJson = & $exe -f json file plugins $sampleFile
            if ($LASTEXITCODE -ne 0) {
                throw "Packaged nmo.exe file plugins failed"
            }
            $plugins = ($pluginsJson -join "`n") | ConvertFrom-Json
            if ($plugins.data.missing_count -ne 0) {
                throw "Packaged nmo.exe reports missing plugin dependencies: $($plugins.data.missing_count)"
            }
        } finally {
            $env:NMO_DATA_DIR = $oldDataDir
        }
    }

    $completionMap = @{
        "bash" = "nmo.bash"
        "fish" = "nmo.fish"
        "zsh" = "_nmo"
        "powershell" = "nmo.ps1"
        "ps1" = "nmo.ps1"
    }
    foreach ($shell in $completionMap.Keys) {
        $installedPath = Join-Path $PackageRoot "share\completions\$($completionMap[$shell])"
        if (-not (Test-Path $installedPath)) {
            throw "Missing completion file: $installedPath"
        }
        $cliText = (& $exe completion $shell) -join "`n"
        if ($LASTEXITCODE -ne 0) {
            throw "nmo completion $shell failed"
        }
        $fileText = (Get-Content -LiteralPath $installedPath -Raw).TrimEnd("`r", "`n")
        if ($cliText.TrimEnd("`r", "`n") -ne $fileText) {
            throw "nmo completion $shell does not match $installedPath"
        }
    }

    $SmokeDir = Join-Path $BuildDir "_package_smoke"
    if (Test-Path $SmokeDir) {
        Assert-UnderPath -Parent $BuildDir -Child $SmokeDir
        Remove-Item -LiteralPath $SmokeDir -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $SmokeDir | Out-Null
    $SmokeC = Join-Path $SmokeDir "smoke.c"
    @'
#include "nmo.h"

int main(void) {
    return nmo_version_int() == 0 ? 1 : 0;
}
'@ | Set-Content -LiteralPath $SmokeC -Encoding ASCII
    Invoke-Checked "gcc" @(
        $SmokeC,
        "-I", (Join-Path $PackageRoot "include"),
        "-L", (Join-Path $PackageRoot "lib"),
        "-static",
        "-lnmo",
        "-lminiz",
        "-o", (Join-Path $SmokeDir "smoke.exe")
    )
    & (Join-Path $SmokeDir "smoke.exe")
    if ($LASTEXITCODE -ne 0) {
        throw "Static link smoke executable failed"
    }
}

Invoke-Step "Create zip" {
    if (Test-Path $ZipPath) {
        Assert-UnderPath -Parent $DistDir -Child $ZipPath
        Remove-Item -LiteralPath $ZipPath -Force
    }
    Compress-Archive -LiteralPath $PackageRoot -DestinationPath $ZipPath -Force
}

Write-Host "Package created: $ZipPath"
