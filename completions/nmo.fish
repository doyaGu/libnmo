# Fish completion for nmo - Virtools NMO file tool
#
# Installation:
#   Option 1: Copy to Fish completions directory:
#     cp nmo.fish ~/.config/fish/completions/nmo.fish
#
#   Option 2: System-wide:
#     sudo cp nmo.fish /usr/share/fish/vendor_completions.d/nmo.fish
#
#   Fish will automatically load completions from these directories.

# Disable file completions by default (we add them explicitly where needed)
complete -c nmo -f

# --- Helper functions ---

# True when no group has been given yet
function __nmo_needs_group
    set -l cmd (commandline -opc)
    # Only "nmo" on the line so far (skip flags)
    for i in (seq 2 (count $cmd))
        switch $cmd[$i]
            case '-*'
                continue
            case '*'
                return 1
        end
    end
    return 0
end

# True when a group has been given but no action yet
function __nmo_needs_action
    set -l cmd (commandline -opc)
    set -l positional 0
    for i in (seq 2 (count $cmd))
        switch $cmd[$i]
            case '-*'
                continue
            case '*'
                set positional (math $positional + 1)
        end
    end
    test $positional -eq 1
end

# True when the given group (or its alias) is on the command line
function __nmo_group_is
    set -l target $argv[1]
    set -l cmd (commandline -opc)
    for i in (seq 2 (count $cmd))
        switch $cmd[$i]
            case '-*'
                continue
            case '*'
                switch $cmd[$i]
                    case $target
                        return 0
                    case '*'
                        return 1
                end
        end
    end
    return 1
end

# --- Group completions ---

complete -c nmo -n __nmo_needs_group -a file    -d 'Inspect NMO file metadata'
complete -c nmo -n __nmo_needs_group -a f       -d 'Alias for file'
complete -c nmo -n __nmo_needs_group -a chunk   -d 'Work with file chunks'
complete -c nmo -n __nmo_needs_group -a ch      -d 'Alias for chunk'
complete -c nmo -n __nmo_needs_group -a object  -d 'Work with objects'
complete -c nmo -n __nmo_needs_group -a obj     -d 'Alias for object'
complete -c nmo -n __nmo_needs_group -a behavior -d 'Work with behaviors'
complete -c nmo -n __nmo_needs_group -a beh     -d 'Alias for behavior'
complete -c nmo -n __nmo_needs_group -a parameter -d 'Work with parameters'
complete -c nmo -n __nmo_needs_group -a param   -d 'Alias for parameter'
complete -c nmo -n __nmo_needs_group -a resource -d 'Work with resources'
complete -c nmo -n __nmo_needs_group -a res     -d 'Alias for resource'
complete -c nmo -n __nmo_needs_group -a texture -d 'Work with textures'
complete -c nmo -n __nmo_needs_group -a tex     -d 'Alias for texture'
complete -c nmo -n __nmo_needs_group -a type    -d 'Work with types'
complete -c nmo -n __nmo_needs_group -a t       -d 'Alias for type'
complete -c nmo -n __nmo_needs_group -a validate -d 'Validate file integrity'
complete -c nmo -n __nmo_needs_group -a val     -d 'Alias for validate'
complete -c nmo -n __nmo_needs_group -a convert -d 'Convert and transform files'
complete -c nmo -n __nmo_needs_group -a conv    -d 'Alias for convert'
complete -c nmo -n __nmo_needs_group -a diff    -d 'Compare NMO files'
complete -c nmo -n __nmo_needs_group -a d       -d 'Alias for diff'
complete -c nmo -n __nmo_needs_group -a query   -d 'Query and filter data'
complete -c nmo -n __nmo_needs_group -a q       -d 'Alias for query'
complete -c nmo -n __nmo_needs_group -a extension -d 'Manage extensions'
complete -c nmo -n __nmo_needs_group -a ext     -d 'Alias for extension'
complete -c nmo -n __nmo_needs_group -a debug   -d 'Debug and diagnostics'
complete -c nmo -n __nmo_needs_group -a dbg     -d 'Alias for debug'
complete -c nmo -n __nmo_needs_group -a repl    -d 'Interactive REPL'

# --- Action completions per group ---

# file / f
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is file; or __nmo_group_is f' -a info    -d 'Show file information'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is file; or __nmo_group_is f' -a header  -d 'Show file header'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is file; or __nmo_group_is f' -a stats   -d 'Show file statistics'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is file; or __nmo_group_is f' -a classes -d 'List classes in file'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is file; or __nmo_group_is f' -a plugins -d 'List plugins in file'

# chunk / ch
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is chunk; or __nmo_group_is ch' -a list -d 'List chunks'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is chunk; or __nmo_group_is ch' -a tree -d 'Show chunk tree'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is chunk; or __nmo_group_is ch' -a show -d 'Show chunk details'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is chunk; or __nmo_group_is ch' -a find -d 'Find chunks'

# object / obj
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is object; or __nmo_group_is obj' -a list   -d 'List objects'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is object; or __nmo_group_is obj' -a tree   -d 'Show object tree'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is object; or __nmo_group_is obj' -a show   -d 'Show object details'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is object; or __nmo_group_is obj' -a find   -d 'Find objects'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is object; or __nmo_group_is obj' -a refs   -d 'Show object references'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is object; or __nmo_group_is obj' -a rename -d 'Rename an object'

# behavior / beh
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is behavior; or __nmo_group_is beh' -a list  -d 'List behaviors'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is behavior; or __nmo_group_is beh' -a stats -d 'Show behavior statistics'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is behavior; or __nmo_group_is beh' -a show  -d 'Show behavior details'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is behavior; or __nmo_group_is beh' -a graph -d 'Show behavior graph'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is behavior; or __nmo_group_is beh' -a dump  -d 'Dump behavior data'

# parameter / param
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is parameter; or __nmo_group_is param' -a list -d 'List parameters'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is parameter; or __nmo_group_is param' -a show -d 'Show parameter details'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is parameter; or __nmo_group_is param' -a dump -d 'Dump parameter data'

# resource / res
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is resource; or __nmo_group_is res' -a list    -d 'List resources'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is resource; or __nmo_group_is res' -a show    -d 'Show resource details'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is resource; or __nmo_group_is res' -a extract -d 'Extract resources'

# texture / tex
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is texture; or __nmo_group_is tex' -a list    -d 'List textures'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is texture; or __nmo_group_is tex' -a show    -d 'Show texture details'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is texture; or __nmo_group_is tex' -a extract -d 'Extract textures'

# type / t
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is type; or __nmo_group_is t' -a list       -d 'List types'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is type; or __nmo_group_is t' -a show       -d 'Show type details'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is type; or __nmo_group_is t' -a class-tree -d 'Show class hierarchy'

# validate / val
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is validate; or __nmo_group_is val' -a all        -d 'Run all validations'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is validate; or __nmo_group_is val' -a structure  -d 'Validate file structure'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is validate; or __nmo_group_is val' -a references -d 'Validate references'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is validate; or __nmo_group_is val' -a resources  -d 'Validate resources'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is validate; or __nmo_group_is val' -a orphans    -d 'Find orphan objects'

# convert / conv
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is convert; or __nmo_group_is conv' -a copy    -d 'Copy file'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is convert; or __nmo_group_is conv' -a version -d 'Convert file version'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is convert; or __nmo_group_is conv' -a strip   -d 'Strip data from file'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is convert; or __nmo_group_is conv' -a merge   -d 'Merge files'

# diff / d
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is diff; or __nmo_group_is d' -a summary -d 'Show diff summary'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is diff; or __nmo_group_is d' -a objects -d 'Diff objects'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is diff; or __nmo_group_is d' -a chunks  -d 'Diff chunks'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is diff; or __nmo_group_is d' -a full    -d 'Show full diff'

# query / q
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is query; or __nmo_group_is q' -a eval   -d 'Evaluate expression'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is query; or __nmo_group_is q' -a script -d 'Run query script'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is query; or __nmo_group_is q' -a schema -d 'Show schema'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is query; or __nmo_group_is q' -a module -d 'Manage query modules'

# extension / ext
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is extension; or __nmo_group_is ext' -a list  -d 'List extensions'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is extension; or __nmo_group_is ext' -a load  -d 'Load extension'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is extension; or __nmo_group_is ext' -a info  -d 'Show extension info'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is extension; or __nmo_group_is ext' -a check -d 'Check extension'

# debug / dbg
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is debug; or __nmo_group_is dbg' -a load-phases -d 'Show load phases'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is debug; or __nmo_group_is dbg' -a chunks      -d 'Debug chunks'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is debug; or __nmo_group_is dbg' -a objects     -d 'Debug objects'
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is debug; or __nmo_group_is dbg' -a export      -d 'Export debug data'

# repl
complete -c nmo -n '__nmo_needs_action; and __nmo_group_is repl' -a start -d 'Start interactive REPL'

# --- Common flags ---

complete -c nmo -l json      -d 'Output in JSON format'
complete -c nmo -l no-color  -d 'Disable colored output'
complete -c nmo -l no-pager  -d 'Disable paging'
complete -c nmo -s v         -d 'Verbose output'
complete -c nmo -l batch     -d 'Batch mode'
complete -c nmo -l sort      -r -d 'Sort output'
complete -c nmo -l top       -r -d 'Show top N results'
complete -c nmo -l reverse   -d 'Reverse sort order'
complete -c nmo -l class     -r -d 'Filter by class'
complete -c nmo -l name      -r -d 'Filter by name'
complete -c nmo -l id        -r -d 'Filter by ID'
complete -c nmo -s o         -r -d 'Output file'
complete -c nmo -l out-dir   -r -d 'Output directory'
complete -c nmo -l format    -r -d 'Output format'
complete -c nmo -l dry-run   -d 'Show what would be done'
complete -c nmo -l overwrite -d 'Overwrite existing files'
complete -c nmo -l strict    -d 'Enable strict mode'

# --- File completions for .nmo/.cmo/.vmo ---

complete -c nmo -F -k -a '(for f in *.nmo *.cmo *.vmo; test -f $f; and echo $f; end 2>/dev/null)'
