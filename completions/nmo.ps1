# PowerShell completion for nmo - Virtools NMO file tool
#
# Installation:
#   Add the following line to your PowerShell profile ($PROFILE):
#     . /path/to/completions/nmo.ps1
#
#   To find your profile path, run: echo $PROFILE
#   If the file does not exist, create it: New-Item -Path $PROFILE -ItemType File -Force

$_nmo_groups = @{
    'file'      = @('info', 'header', 'stats', 'classes', 'plugins')
    'chunk'     = @('list', 'tree', 'show', 'find')
    'object'    = @('list', 'tree', 'show', 'find', 'refs', 'rename')
    'behavior'  = @('list', 'stats', 'show', 'graph', 'dump')
    'parameter' = @('list', 'show', 'dump')
    'resource'  = @('list', 'show', 'extract')
    'texture'   = @('list', 'show', 'extract')
    'type'      = @('list', 'show', 'class-tree')
    'validate'  = @('all', 'structure', 'references', 'resources', 'orphans')
    'convert'   = @('copy', 'version', 'strip', 'merge')
    'diff'      = @('summary', 'objects', 'chunks', 'full')
    'query'     = @('eval', 'script', 'schema', 'module')
    'extension' = @('list', 'load', 'info', 'check')
    'debug'     = @('load-phases', 'chunks', 'objects', 'export')
    'repl'      = @('start')
}

$_nmo_aliases = @{
    'f'    = 'file'
    'ch'   = 'chunk'
    'obj'  = 'object'
    'beh'  = 'behavior'
    'param'= 'parameter'
    'res'  = 'resource'
    'tex'  = 'texture'
    't'    = 'type'
    'val'  = 'validate'
    'conv' = 'convert'
    'd'    = 'diff'
    'q'    = 'query'
    'ext'  = 'extension'
    'dbg'  = 'debug'
}

$_nmo_group_descriptions = @{
    'file'      = 'Inspect NMO file metadata'
    'chunk'     = 'Work with file chunks'
    'object'    = 'Work with objects'
    'behavior'  = 'Work with behaviors'
    'parameter' = 'Work with parameters'
    'resource'  = 'Work with resources'
    'texture'   = 'Work with textures'
    'type'      = 'Work with types'
    'validate'  = 'Validate file integrity'
    'convert'   = 'Convert and transform files'
    'diff'      = 'Compare NMO files'
    'query'     = 'Query and filter data'
    'extension' = 'Manage extensions'
    'debug'     = 'Debug and diagnostics'
    'repl'      = 'Interactive REPL'
}

$_nmo_flags = @(
    @{ Name = '--json';      Desc = 'Output in JSON format' }
    @{ Name = '--no-color';  Desc = 'Disable colored output' }
    @{ Name = '--no-pager';  Desc = 'Disable paging' }
    @{ Name = '-v';          Desc = 'Verbose output' }
    @{ Name = '--batch';     Desc = 'Batch mode' }
    @{ Name = '--sort';      Desc = 'Sort output' }
    @{ Name = '--top';       Desc = 'Show top N results' }
    @{ Name = '--reverse';   Desc = 'Reverse sort order' }
    @{ Name = '--class';     Desc = 'Filter by class' }
    @{ Name = '--name';      Desc = 'Filter by name' }
    @{ Name = '--id';        Desc = 'Filter by ID' }
    @{ Name = '-o';          Desc = 'Output file' }
    @{ Name = '--out-dir';   Desc = 'Output directory' }
    @{ Name = '--format';    Desc = 'Output format' }
    @{ Name = '--dry-run';   Desc = 'Show what would be done' }
    @{ Name = '--overwrite'; Desc = 'Overwrite existing files' }
    @{ Name = '--strict';    Desc = 'Enable strict mode' }
)

Register-ArgumentCompleter -CommandName nmo -Native -ScriptBlock {
    param($wordToComplete, $commandAst, $cursorPosition)

    $tokens = $commandAst.ToString().Substring(0, $cursorPosition).Trim() -split '\s+'

    # Collect positional arguments (skip flags)
    $positional = @()
    for ($i = 1; $i -lt $tokens.Count; $i++) {
        if (-not $tokens[$i].StartsWith('-')) {
            $positional += $tokens[$i]
        }
    }

    # If currently typing something, the last token is the word to complete
    # and should not count as a completed positional arg
    if ($wordToComplete -ne '' -and $positional.Count -gt 0) {
        $positional = $positional[0..($positional.Count - 2)]
    }

    # Completing flags
    if ($wordToComplete.StartsWith('-')) {
        $_nmo_flags | Where-Object { $_.Name -like "$wordToComplete*" } | ForEach-Object {
            [System.Management.Automation.CompletionResult]::new(
                $_.Name, $_.Name, 'ParameterValue', $_.Desc
            )
        }
        return
    }

    # No group yet - complete group names and aliases
    if ($positional.Count -eq 0) {
        $allNames = @()
        foreach ($g in $_nmo_groups.Keys) {
            $allNames += @{ Name = $g; Desc = $_nmo_group_descriptions[$g] }
        }
        foreach ($a in $_nmo_aliases.Keys) {
            $canonical = $_nmo_aliases[$a]
            $allNames += @{ Name = $a; Desc = "Alias for $canonical" }
        }
        $allNames | Where-Object { $_.Name -like "$wordToComplete*" } | Sort-Object { $_.Name } | ForEach-Object {
            [System.Management.Automation.CompletionResult]::new(
                $_.Name, $_.Name, 'ParameterValue', $_.Desc
            )
        }
        return
    }

    # Have a group, resolve alias
    $group = $positional[0]
    if ($_nmo_aliases.ContainsKey($group)) {
        $group = $_nmo_aliases[$group]
    }

    # No action yet - complete actions for the group
    if ($positional.Count -eq 1 -and $_nmo_groups.ContainsKey($group)) {
        $_nmo_groups[$group] | Where-Object { $_ -like "$wordToComplete*" } | ForEach-Object {
            [System.Management.Automation.CompletionResult]::new(
                $_, $_, 'ParameterValue', "$group $_"
            )
        }
        return
    }

    # After group and action - complete file paths (.nmo/.cmo/.vmo) and flags
    $pattern = if ($wordToComplete) { "$wordToComplete*" } else { '*' }
    Get-ChildItem -Path . -Filter $pattern -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -in '.nmo', '.cmo', '.vmo' } |
        ForEach-Object {
            [System.Management.Automation.CompletionResult]::new(
                $_.Name, $_.Name, 'ProviderItem', $_.Name
            )
        }

    # Also suggest flags
    $_nmo_flags | Where-Object { $_.Name -like "$wordToComplete*" } | ForEach-Object {
        [System.Management.Automation.CompletionResult]::new(
            $_.Name, $_.Name, 'ParameterValue', $_.Desc
        )
    }
}
