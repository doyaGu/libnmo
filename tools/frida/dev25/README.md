# Dev25 Frida Schematic Rebuild Probe

This probe validates Virtools Dev 2.5.0.48 schematic UI reconstruction against a
live `Dev.exe` process. Default modes are read-only. Modes that clear the
`CKObject -> UIElement` map and call Dev's rebuild function require
`-AllowMutation`.

The reconstructed algorithm notes are in
`docs/RE/Dev25-Schematic-UI-Rebuild.md`.

## Confirmed Target

- `Dev.exe` SHA256: `C4BC2724165730B9CB0EFB8C481D05783BF3DBD7929698C3F863AC06ABEAD4B2`
- IDA image base: `0x400000`
- Rebuild entry: `CUIKSchematicView_ReconstructScriptUIFromRuntimeGraph` at `0x538f00`

## Modes

Mode `trace` records schematic load, UI map updates, language loads, status bar
messages, rebuild entry calls, and link-record collector calls.

```powershell
.\tools\frida\dev25\Invoke-Dev25Probe.ps1 `
  -Mode trace `
  -Sample 'C:\Users\kakut\Works\Virtools\VirtoolsScriptDeobfuscation\data\BBSamples\3D Transformations\Activate Link.cmo' `
  -TimeoutSec 45 `
  -KillOnExit
```

Mode `inventory` records CKBehavior candidates seen through
`SetUIElementForCKObject`, including name, behavior type, sub-behavior count,
input count, output count, sub-behavior link count, and UI element pointer.

Mode `trigger-rebuild` removes one target script from the UI map and calls Dev's
own rebuild entry. This mutates the live Dev process, but does not save the file.

```powershell
.\tools\frida\dev25\Invoke-Dev25Probe.ps1 `
  -Mode trigger-rebuild `
  -Sample 'C:\Users\kakut\Works\Virtools\VirtoolsScriptDeobfuscation\data\BBSamples\3D Transformations\Activate Link.cmo' `
  -TargetName 'Topic - Activate Link' `
  -AllowMutation `
  -TimeoutSec 60 `
  -KillOnExit
```

Mode `link-records` runs the same controlled trigger and dumps the 19-DWORD
temporary records passed to `CUIKSchematicView_CreateUILinksFromRebuildRecords`.
Use this mode to reverse engineer Dev's runtime graph to InterfaceChunk link
record mapping.

`-TriggerCallStyle Deferred` is the default because it lets the status-bar hook
return before calling rebuild. Use `-TriggerCallStyle Inline` only when you need
the older, main-callback call path for a narrow experiment.

Mode `language` records `CUIKLanguageManager::Load` calls and the returned text.

## Output

Each run writes to `.ida-analysis/frida/runs/<timestamp>-<mode>/`:

- `frida.log`: raw Frida output.
- `events.jsonl`: machine-readable probe events.
- `summary.json`: mode, event counts, trigger result, and output paths.
- `agent.generated.js`: generated agent with the run config prepended.
- `config.json`: wrapper configuration for the run.

For a successful controlled rebuild, `summary.json` should include a trigger
object like:

```json
{
  "targetName": "Topic - Activate Link",
  "uiBefore": "0xbd49288",
  "uiAfterRemove": "0x0",
  "rebuildReturn": 0,
  "uiAfterRebuild": "0xbe1a668"
}
```

The important invariant is that `uiAfterRemove` is `0x0`, `rebuildReturn` is
`0`, and `uiAfterRebuild` is a non-null pointer different from `uiBefore`.

## Function Names Used by the Probe

- `0x52bc80`: `CUIKSchematicView_LoadBehaviorArrayIntoSchematic`
- `0x53f2d0`: `CUIKSchematicView_RebuildMissingBehaviorWindowsFromList`
- `0x538f00`: `CUIKSchematicView_ReconstructScriptUIFromRuntimeGraph`
- `0x5391c0`: `CUIKSchematicView_CollectRuntimeGraphLinkRecords`
- `0x539df0`: `CUIKSchematicView_AppendRuntimeGraphLinkRecord`
- `0x53a450`: `CUIKSchematicView_CreateUILinksFromRebuildRecords`

`0x52bc80` is intentionally named as the behavior-array schematic loader. It
tries to load existing InterfaceChunk UI first and only enters runtime rebuild
when UI data is missing or unusable.
