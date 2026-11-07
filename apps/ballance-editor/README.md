# Ballance editor

Development is paused. The editor is a separate application in this repository,
with its own launcher, browser assets and tests. It imports the Ballance
Python toolchain and calls `nmo`; the toolchain does not import, launch, test or
fingerprint the editor. The application can be moved to another directory and
pointed at a toolchain checkout.

## Run

Use Python 3.10 or newer with Pillow installed, and a browser with WebGL. Native
materials and mechanisms need a Ballance installation and extracted element
resources. Local play also needs an `nmo` build with libivp. The native game
window additionally needs the CLI's viewer support.

From the repository root, with `BALLANCE`, `BALLANCE_ELEMENTS` and `NMO` set to
absolute paths:

```sh
python3 apps/ballance-editor/run.py .local/ballance/route.json out/ballance-editor \
  --elements "$BALLANCE_ELEMENTS" --game "$BALLANCE" --nmo "$NMO" --open
```

Supply your own route at the input path; a fresh clone does not include an
authored map. Keep map sources in an ignored local directory and use a separate
ignored workspace for drafts, generated meshes and playback traces.

The server prints its loopback URL; `--port` selects a fixed port. Without
`--open`, open the printed URL yourself. Stop the server with Ctrl-C.

The default toolchain is the sibling `apps/ballance` directory. A relocated
application accepts `--toolchain /path/to/apps/ballance` or the
`BALLANCE_TOOLCHAIN` environment variable; the command-line option takes
precedence. Paths to the route, workspace and assets are resolved from the
working directory. To inspect the options from the repository root:

```sh
python3 apps/ballance-editor/run.py --help
```

The former `apps/ballance/scripts/ballance_edit_level.py` launcher and
`ballance.editor` package have been removed. Use `run.py` and
`ballance_editor`; there is no compatibility launcher in the toolchain.
See the [level builder guide](../../docs/ballance-level-builder.md) for native
asset extraction and CLI build requirements.

## Drafts and local play

Use a dedicated workspace, such as `out/ballance-editor`. `draft.json` holds
edits, `session.json` records history and source identity, and `current/` holds
the compiled draft. Reopening the workspace preserves its draft. Moving the
application does not move the workspace. If the source has changed since the
draft was created, use a new workspace. **Save source** writes the draft back
to the local source route; it does not stage or commit the map.

Local play accepts a point on a rail or floor, a selected mechanism, or a compiled
checkpoint. It supports pause, stepping and retry from the same start. Retry
creates a fresh native world. Native Ball Off events and a fall below the map's
local recovery boundary have separate death reasons. An edit invalidates the
running probe; reset it against the new draft before continuing.

`local-play/` contains the probe's level, generated runtime overlays, log and
spawn record. A local probe does not produce full-level completion evidence.
Use the toolchain's level verifier for checkpoint, reward, gameplay-condition
and finish coverage. The [control protocol](../../docs/cli-reference.md#controlled-runs)
explains the native process used for stepping.

## Development and checks

With a Python virtual environment active, install the development dependencies.
Use Node.js 24 for the pinned frontend tools:

```sh
python3 -m pip install Pillow -r apps/ballance-editor/requirements-dev.txt
npm --prefix apps/ballance-editor ci
make -C apps/ballance-editor check-style
make -C apps/ballance-editor test
```

`check-style` runs Ruff, ESLint and Prettier. `make -C apps/ballance-editor format`
formats Python and frontend files and applies ESLint's automatic control-flow
fixes. Tests can also be run independently:

```sh
python3 -m unittest discover -s apps/ballance-editor/tests -v
npm --prefix apps/ballance-editor test
```

Native integration tests are skipped when their assets are unavailable. To run
them, supply absolute paths because tests and native processes change working
directories:

```sh
BALLANCE="$BALLANCE" BALLANCE_ELEMENTS="$BALLANCE_ELEMENTS" \
NMO_TEST_CLI="$NMO" NMO_TEST_PHYSICS=1 \
make -C apps/ballance-editor test
```

`BALLANCE_TOOLCHAIN` selects the external package for tests too. The independence
tests relocate the application and then modify and remove it in a temporary
copy, checking that toolchain builds and evidence remain valid. Application-only
changes do not invalidate toolchain evidence; changes to the consumed toolchain,
CLI, route or recorded assets still do.

The tracked suite uses independent fixtures. Tests that require a particular
authored map stay with its local files and are outside normal discovery.
A prepared workspace may provide `.local/ballance/run_tests.py --editor` for
those preserved tests; that runner and its map inputs are not part of a fresh
clone. See [authored maps and shared tooling](../../CONTRIBUTING.md#authored-maps-and-shared-tooling)
for storage rules and the optional local commit guard.

## Code layout

| Files | Responsibility |
|---|---|
| `run.py`, `ballance_editor/server.py` | Resolve the external toolchain, serve assets and dispatch local HTTP requests |
| `session.py`, `commands.py`, `model.py`, `transforms.py`, `spatial.py` | Draft history, document operations, selection, placement and constraint editing |
| `native.py`, `materials.py`, `local_play.py` | Native mesh/material previews and the paused physics process |
| `editor.html`, `viewer.js`, `bootstrap.js`, the other JavaScript files and `studio.css` | Page structure, shared view state, startup and interaction/rendering code |

The page loads `viewer.js` first and `bootstrap.js` last. Browser scripts share
page state, so keep their load order when changing the asset list. Keep request
validation ahead of native process replacement, and keep probe artifacts outside
the authored build. General route compilation and verification belong in the
external toolchain; draft history and browser interaction belong here.
