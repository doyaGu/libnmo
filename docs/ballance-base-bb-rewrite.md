# Ballance base.cmo Building Block Rewrite

This document records the current safe workflow for converting Ballance
`base.cmo` behavior logic to Ballance-specific building blocks with libnmo.

## Goal

Generate a `base.cmo` variant that loads in the original Ballance runtime and
uses selected building blocks from `Ballance.dll`, without breaking the graph
shape that Ballance Mod Loader Plus and original scripts depend on at runtime.

The current reproducible output is:

```text
C:\Users\kakut\Games\Ballance\base_ballance_bb.cmo
```

## Required Runtime

Use the original game installation:

```text
C:\Users\kakut\Games\Ballance
```

Build `Ballance.dll` with:

```text
C:\Users\kakut\Works\Virtools\Virtools-SDK-2.1
```

Do not use the Ballanced CK2/VxMath build when testing original Ballance. The
game runtime must keep the original `CK2.dll` and `VxMath.dll`.

## Current Safe Rewrite

The current manifest is:

```text
tools\ballance\base_ballance_bb_manifest.json
```

It performs one safe leaf replacement:

```text
behavior #3516
runtime name: Switch On Message
replacement GUID: {42414C07-10000007}
replacement DLL: Ballance.dll
```

The runtime prototype name must remain `Switch On Message`. BMLPlus searches the
base event graph by behavior name and then follows the original output chains
during `OnCKPlay`. Renaming the prototype to `Ballance Base Event Router` causes
`[Mod Manager] Error : Play`.

## Do Not Fold

Do not fold the whole `Event_handler` script into a high-level node.

Known runtime dependency:

```text
BallanceModLoaderPlus\src\EventHook.cpp
```

BMLPlus calls `FindFirstBB(script, "Switch On Message", false, 2, 11, 11, 0)`
and then inserts hooks into the original outgoing chains. Removing those chains
or changing the runtime prototype name breaks BML initialization.

The safe rule for now:

```text
Event_handler: leaf replacement only
Switch On Message: keep graph structure and runtime name
```

## Generate CMO

From the libnmo repository:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\ballance\build-base-ballance-bb.ps1 `
  -InputCmo C:\Users\kakut\Games\Ballance\base_fold_loading_deletes.cmo `
  -OutputCmo C:\Users\kakut\Games\Ballance\base_ballance_bb.cmo
```

The script:

1. Reads `tools\ballance\base_ballance_bb_manifest.json`.
2. Writes a temporary strict patch JSON.
3. Runs `nmo patch diff`.
4. Runs `nmo patch apply`.
5. Runs `nmo validate all`.
6. Shows the rewritten behavior signatures.

Expected key output:

```text
replace_bb #3516: guid 1BB23F1D-17FF14B9 -> 42414C07-10000007, name -> Switch On Message
Result: VALID
Behavior #3516: Switch On Message
GUID: {42414C07-10000007}
```

## Build Ballance.dll

Use the existing SDK 2.1 build directory:

```powershell
cmake --build C:\Users\kakut\Works\Ballanced\build-virtools-sdk-bb `
  --config Release `
  --target Ballance `
  -- /m

Copy-Item `
  C:\Users\kakut\Works\Ballanced\build-virtools-sdk-bb\bin\Release\Ballance.dll `
  C:\Users\kakut\Games\Ballance\BuildingBlocks\Ballance.dll `
  -Force
```

Confirm the CMake cache points at:

```text
VIRTOOLS_SDK_PATH=C:/Users/kakut/Works/Virtools/Virtools-SDK-2.1
CMAKE_GENERATOR_PLATFORM=Win32
```

## Player Smoke Test

Run from the game `Bin` directory:

```powershell
$game = 'C:\Users\kakut\Games\Ballance'
$log = 'Player-base-ballance-bb.log'

Get-Process Player -ErrorAction SilentlyContinue | Stop-Process -Force
Remove-Item -LiteralPath (Join-Path $game "Bin\$log") -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path $game 'ModLoader\ModLoader.log') -Force -ErrorAction SilentlyContinue

$p = Start-Process `
  -FilePath (Join-Path $game 'Bin\Player.exe') `
  -ArgumentList @('--cmo', '..\base_ballance_bb.cmo', '--log', $log, '--verbose', '--debug') `
  -WorkingDirectory (Join-Path $game 'Bin') `
  -PassThru

Start-Sleep -Seconds 7
if (!$p.HasExited) {
  $p.Kill()
  $p.WaitForExit()
}

Select-String -Path (Join-Path $game "Bin\$log") `
  -Pattern 'Mod Manager|Error : Play|Cam_MenuLevel|frame 300'

Select-String -Path (Join-Path $game 'ModLoader\ModLoader.log') `
  -Pattern 'Insert message|ERROR|Exception|Loading Mod'
```

Expected:

```text
Render frame 300 ... cameraName="Cam_MenuLevel"
Insert message Start Menu Hook
Insert message End Level Hook
```

Unexpected:

```text
[Mod Manager] Error : Play
```

If this appears, first check whether the replacement prototype runtime name is
still `Switch On Message` and whether `Event_handler` output chains were folded.

## Next Rewrite Candidates

Prefer this order:

1. Leaf replacements that keep original graph shape.
2. Small local folds not referenced by BMLPlus or original managers.
3. High-level Ballance blocks only after a source/code search proves no runtime
   component depends on the original behavior names or link topology.

Before adding any replacement or fold:

```powershell
.\build\tools\nmo.exe behavior show --id <id> <cmo>
.\build\tools\nmo.exe object refs <id> <cmo>
.\build\tools\nmo.exe behavior graph-boundary --id <parent-or-node> <cmo>
```

Also search BMLPlus and Ballanced code for the behavior name:

```powershell
rg -n "<Behavior Name>" C:\Users\kakut\Works\Ballance\BallanceModLoaderPlus C:\Users\kakut\Works\Ballanced
```

If runtime code searches by name or follows neighboring links, keep the graph
shape and use leaf replacement only.
