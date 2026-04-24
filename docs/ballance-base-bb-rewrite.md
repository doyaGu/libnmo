# Ballance base.cmo Building Block Rewrite

This document records the current workflow for converting Ballance `base.cmo`
behavior logic to Ballance-specific high-level building blocks with libnmo.

## Goal

Generate a `base.cmo` variant that loads in the original Ballance runtime and
replaces selected behavior graph structure with building blocks from
`Ballance.dll`.

The current generated output is:

```text
C:\Users\kakut\Games\Ballance\base_ballance_bb.cmo
```

## Runtime Assumption

This flow targets the original Ballance runtime without BMLPlus installed as a
building block DLL.

The game directory used for validation is:

```text
C:\Users\kakut\Games\Ballance
```

Build `Ballance.dll` with:

```text
C:\Users\kakut\Works\Virtools\Virtools-SDK-2.1
```

Do not use the Ballanced CK2/VxMath build when testing original Ballance. The
game runtime must keep the original `CK2.dll` and `VxMath.dll`.

## Current High-Level Rewrite

The manifest is:

```text
tools\ballance\base_ballance_bb_manifest.json
```

It folds the base event router:

```text
parent script: #4692 Event_handler
anchor node:   #3516 Switch On Message
target name:   Ballance Base Event Router
target GUID:   {42414C07-10000007}
```

The fold removes the original `Switch On Message` plus eleven event-output Nop
relay nodes, then retargets the external control edges to the high-level
`Ballance Base Event Router` outputs.

Selected nodes:

```text
3516,2571,2568,3043,3032,2367,2370,2565,3519,3534,3528,3525
```

The eleven output boundary edges preserve the original external control-flow
targets, but the fold candidate order is not the same as the runtime output
port order. The manifest maps candidate output indexes to the verified
`Ballance Base Event Router` output indexes:

```text
0:1, 1:9, 2:6, 3:5, 4:10, 5:2, 6:0, 7:4, 8:7, 9:8, 10:3
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
6. Shows the rewritten anchor behavior signature.

Expected key output:

```text
fold #4692: anchor #3516, nodes=12, can_write=yes
Result: VALID
Behavior #3516: Ballance Base Event Router
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
  -Pattern 'Mod Manager|Error : Play|Cam_MenuLevel|frame 300|Chunk Read|failed|Load failed'
```

Expected:

```text
LoadNMORange ... done loaded=8
LoadNMORange ... done loaded=1
Render frame 300 ... cameraName="Cam_MenuLevel"
```

The first stable menu camera frame is timing-sensitive in the standalone Player
test harness. If frame 300 is still blank, rerun with a longer wait and check
frame 600. Treat `Chunk Read error`, `Load failed`, or `[Mod Manager] Error :
Play` as failures; a transient blank camera before the menu camera appears is
not by itself a serialization failure.

Unexpected:

```text
Chunk Read error
[Mod Manager] Error : Play
Load failed
```

## Notes About BMLPlus

Earlier validation with BMLPlus installed showed that BMLPlus treated the base
`Event_handler` graph as a runtime hook surface. It searched for
`Switch On Message` by name and followed the original output chains.

That constraint is intentionally not applied to this workflow. The current
target is the original runtime without BMLPlus, so the correct high-level
implementation is the `Ballance Base Event Router` fold.

If BMLPlus compatibility is needed again, use a separate manifest that performs
only a leaf replacement of behavior `#3516` and keeps the runtime prototype name
`Switch On Message`.

## Next Rewrite Candidates

Prefer this order:

1. Fold closed event-routing or fixed relay chains into Ballance-specific BBs.
2. Replace small leaf BB groups whose behavior is now implemented directly in
   `Ballance.dll`.
3. Fold larger subsystems only after validating the generated CMO in Player.

Before adding any replacement or fold:

```powershell
.\build\tools\nmo.exe behavior fold-candidates --parent <id> <cmo>
.\build\tools\nmo.exe behavior show --id <id> <cmo>
.\build\tools\nmo.exe object refs <id> <cmo>
.\build\tools\nmo.exe behavior graph-boundary --id <parent-or-node> <cmo>
```

Then add the operation to:

```text
tools\ballance\base_ballance_bb_manifest.json
```

Regenerate the CMO and run the Player smoke test before committing.
