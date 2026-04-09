# Bash completion for nmo - Virtools NMO file tool
#
# Installation:
#   Option 1: Source directly in your .bashrc:
#     source /path/to/completions/nmo.bash
#
#   Option 2: Copy to system completions directory:
#     sudo cp nmo.bash /etc/bash_completion.d/nmo
#
#   Option 3: Copy to user completions directory:
#     mkdir -p ~/.local/share/bash-completion/completions
#     cp nmo.bash ~/.local/share/bash-completion/completions/nmo

_nmo() {
    local cur prev words cword
    _init_completion || return

    # Group names and aliases
    local groups="file chunk object behavior parameter resource texture type validate convert diff query extension debug repl"
    local aliases="f ch obj beh param res tex t val conv d q ext dbg"
    local all_groups="$groups $aliases"

    # Common flags
    local flags="--json --no-color --no-pager -v --batch --sort --top --reverse --class --name --id -o --out-dir --format --dry-run --overwrite --strict"

    # Resolve alias to canonical group name
    _nmo_resolve_group() {
        case "$1" in
            f)     echo "file" ;;
            ch)    echo "chunk" ;;
            obj)   echo "object" ;;
            beh)   echo "behavior" ;;
            param) echo "parameter" ;;
            res)   echo "resource" ;;
            tex)   echo "texture" ;;
            t)     echo "type" ;;
            val)   echo "validate" ;;
            conv)  echo "convert" ;;
            d)     echo "diff" ;;
            q)     echo "query" ;;
            ext)   echo "extension" ;;
            dbg)   echo "debug" ;;
            *)     echo "$1" ;;
        esac
    }

    # Get actions for a group
    _nmo_actions() {
        case "$1" in
            file)      echo "info header stats classes plugins" ;;
            chunk)     echo "list tree show find" ;;
            object)    echo "list tree show find refs rename" ;;
            behavior)  echo "list stats show graph dump" ;;
            parameter) echo "list show dump" ;;
            resource)  echo "list show extract" ;;
            texture)   echo "list show extract" ;;
            type)      echo "list show class-tree" ;;
            validate)  echo "all structure references resources orphans" ;;
            convert)   echo "copy version strip merge" ;;
            diff)      echo "summary objects chunks full" ;;
            query)     echo "eval script schema module" ;;
            extension) echo "list load info check" ;;
            debug)     echo "load-phases chunks objects export" ;;
            repl)      echo "start" ;;
            *)         echo "" ;;
        esac
    }

    # Find the group argument (first non-flag argument after "nmo")
    local group=""
    local action=""
    local i
    for (( i=1; i < cword; i++ )); do
        local word="${words[$i]}"
        # Skip flags
        if [[ "$word" == -* ]]; then
            continue
        fi
        if [[ -z "$group" ]]; then
            group="$word"
        elif [[ -z "$action" ]]; then
            action="$word"
        fi
    done

    # Complete group names (first positional argument)
    if [[ -z "$group" ]]; then
        if [[ "$cur" == -* ]]; then
            COMPREPLY=( $(compgen -W "$flags" -- "$cur") )
        else
            COMPREPLY=( $(compgen -W "$all_groups" -- "$cur") )
        fi
        return
    fi

    # Resolve alias
    local canonical
    canonical=$(_nmo_resolve_group "$group")

    # Complete actions (second positional argument)
    if [[ -z "$action" ]]; then
        if [[ "$cur" == -* ]]; then
            COMPREPLY=( $(compgen -W "$flags" -- "$cur") )
        else
            local actions
            actions=$(_nmo_actions "$canonical")
            COMPREPLY=( $(compgen -W "$actions" -- "$cur") )
        fi
        return
    fi

    # Complete flags and file paths (remaining arguments)
    if [[ "$cur" == -* ]]; then
        COMPREPLY=( $(compgen -W "$flags" -- "$cur") )
    else
        # Complete .nmo/.cmo/.vmo files
        local IFS=$'\n'
        COMPREPLY=( $(compgen -f -X '!*.@(nmo|cmo|vmo)' -- "$cur") )
        # Also allow directories for navigation
        COMPREPLY+=( $(compgen -d -- "$cur") )
        compopt -o filenames 2>/dev/null
    fi
}

complete -o default -F _nmo nmo
