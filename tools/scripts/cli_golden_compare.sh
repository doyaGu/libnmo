#!/bin/zsh
# Compare the output of two nmo builds for a command group's list and show
# actions across every Virtools file in a directory. Used to verify that CLI
# output refactors are byte-identical (envelope timestamps are ignored).
#
# Usage:
#   NMO_GOLDEN=/path/to/old/nmo NMO_NEW=/path/to/new/nmo \
#   tools/scripts/cli_golden_compare.sh <group> <list-action> <show-action> <json-array-key> [extra show args...]
#
# Example:
#   NMO_GOLDEN=/tmp/nmo-golden NMO_NEW=build/tools/nmo \
#   tools/scripts/cli_golden_compare.sh mesh list show meshes
#
# Pass an empty string for <show-action> to compare the list action only.
# NMO_CORPUS (default: data/Ballance) selects the fixture directory.
set -u
setopt null_glob
G=${NMO_GOLDEN:?set NMO_GOLDEN to the reference nmo binary}
N=${NMO_NEW:?set NMO_NEW to the nmo binary under test}
corpus=${NMO_CORPUS:-data/Ballance}
group=$1; list=$2; show=$3; arrkey=$4; shift 4
extra_show_args=("$@")
diffs=0; runs=0
strip_ts() { LC_ALL=C sed -E 's/"timestamp": ?"[^"]*"/"timestamp":"T"/'; }
cmp_cmd() {
  local desc="$1"; shift
  local go ge gx no ne nx
  go=$("$G" "$@" 2>/tmp/nmo_cmp_ge.txt | strip_ts); gx=${pipestatus[1]}; ge=$(cat /tmp/nmo_cmp_ge.txt)
  no=$("$N" "$@" 2>/tmp/nmo_cmp_ne.txt | strip_ts); nx=${pipestatus[1]}; ne=$(cat /tmp/nmo_cmp_ne.txt)
  runs=$((runs+1))
  if [ "$go" != "$no" ] || [ "$ge" != "$ne" ] || [ "$gx" -ne "$nx" ]; then
    diffs=$((diffs+1))
    echo "DIFF: $desc :: $*"
    diff -a <(echo "$go") <(echo "$no") | head -8
    diff -a <(echo "$ge") <(echo "$ne") | head -4
  fi
}
for f in "$corpus"/*.nmo "$corpus"/*.cmo "$corpus"/*.vmo; do
  [ -f "$f" ] || continue
  if [ -n "$list" ]; then
    for fmt in text json json-pretty; do cmp_cmd "$group $list $fmt" -f $fmt $group $list "$f"; done
    cmp_cmd "$group $list color" --color always $group $list "$f"
  fi
  if [ -n "$show" ]; then
    ids=$("$G" -f json $group $list "$f" 2>/dev/null | python3 -c "import json,sys
d=json.load(sys.stdin)['data']; arr=d.get('$arrkey') or []
print(' '.join(str(x['id']) for x in arr if 'id' in x))" 2>/dev/null)
    for id in ${=ids}; do
      for fmt in text json json-pretty; do cmp_cmd "$group $show $fmt" -f $fmt $group $show --id $id "${extra_show_args[@]}" "$f"; done
      cmp_cmd "$group $show color" --color always $group $show --id $id "${extra_show_args[@]}" "$f"
    done
    cmp_cmd "$group $show missing" $group $show --name DefinitelyMissing "$f"
  fi
done
echo "runs=$runs diffs=$diffs"
[ "$diffs" -eq 0 ]
