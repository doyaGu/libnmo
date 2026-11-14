// nmo's web player page: a library of Virtools games kept in this browser,
// added from a folder, a .zip, a single composition or a URL (?src=URL);
// each runs in the engine (nmo-player.wasm) on the canvas, with its own
// settings, keys and saves, and the developer tools underneath.

import createNmoPlayer from './nmo-player.mjs';
import { createDevtools } from './devtools.mjs';
import { element, fileNameOf, formatSize, download, setHidden, smallButton } from './dom.mjs';
import {
    GAME_FOLDER,
    SAVES_FOLDER,
    adoptLooseSaves,
    planGame,
    playerArguments,
    removeTree,
    saveFolder,
    writeGameFile,
} from './games.mjs';
import { Gamepads, KeyRemapper, buildTouchControls } from './input.mjs';
import { openLibrary } from './library.mjs';
import { PROFILES, loadProfiles, profileById } from './profiles.mjs';
import { createSavesDialog } from './saves.mjs';
import {
    DEFAULT_SETTINGS,
    createSettingsDialog,
    gameSettings,
    gamepadMapping,
} from './settings.mjs';
import { readZip } from './zip.mjs';

const SETTINGS_KEY = 'nmo-player-settings';

function loadSettings() {
    try {
        const saved = JSON.parse(localStorage.getItem(SETTINGS_KEY) ?? '{}');
        /* The published page before this one kept only its mute button's state */
        if (localStorage.getItem('nmo-player-muted') === '1' && saved.muted === undefined) {
            saved.muted = true;
        }
        return { ...DEFAULT_SETTINGS, ...saved };
    } catch {
        return { ...DEFAULT_SETTINGS };
    }
}

function saveSettings(settings) {
    try {
        localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings));
    } catch {
        /* remembered only where storage works */
    }
}

/* The player's C functions (player.c's player_*), and what they hand back read out of
   its memory */
function bindApi(module) {
    const NUMBER = 'number';
    const STRING = 'string';
    const wrap = (name, result, args = []) => module.cwrap(`player_${name}`, result, args);
    const traceRecords = wrap('trace_records', NUMBER);
    const traceCount = wrap('trace_count', NUMBER);
    const entityMatrix = wrap('entity_matrix', NUMBER, [NUMBER, NUMBER]);
    const paramValue = wrap('param_value', NUMBER, [NUMBER]);
    const paramBytes = wrap('param_bytes', NUMBER);
    const paramType = wrap('param_type', STRING);
    const setJoystick = wrap('set_joystick', null, Array(10).fill(NUMBER));
    const modeCount = wrap('screen_mode_count', NUMBER);
    const modeWidth = wrap('screen_mode_width', NUMBER, [NUMBER]);
    const modeHeight = wrap('screen_mode_height', NUMBER, [NUMBER]);
    const unsigned =
        (fn) =>
        (...args) =>
            fn(...args) >>> 0;
    return {
        start: wrap('start', NUMBER, [STRING]),
        stop: wrap('stop', null),
        running: wrap('running', NUMBER),
        pause: wrap('pause', null),
        resume: wrap('resume', null),
        setMute: wrap('set_mute', null, [NUMBER]),
        setVolume: wrap('set_volume', null, [NUMBER]),
        setFrameLimit: wrap('set_frame_limit', null, [NUMBER]),
        screenshot: wrap('screenshot', NUMBER, [STRING]),
        /* `state` as input.mjs's joystickOf makes it */
        setJoystick(slot, state) {
            setJoystick(
                slot,
                state.attached ? 1 : 0,
                ...state.position,
                ...state.rotation,
                state.pov,
                state.buttons
            );
        },
        /* The sizes a game can start at, 'WxH', each once */
        screenSizes() {
            const sizes = [];
            for (let mode = 0; mode < modeCount(); mode++) {
                sizes.push(`${modeWidth(mode)}x${modeHeight(mode)}`);
            }
            return [...new Set(sizes)];
        },
        frames: wrap('frames', NUMBER),
        stepMs: wrap('step_ms', NUMBER),
        drawMs: wrap('draw_ms', NUMBER),
        time: wrap('time', NUMBER),
        memory: wrap('memory', NUMBER),
        fileCount: wrap('file_count', NUMBER),
        fileName: wrap('file_name', STRING, [NUMBER]),
        objectCount: wrap('object_count', NUMBER, [NUMBER]),
        object: unsigned(wrap('object', NUMBER, [NUMBER, NUMBER])),
        objectName: wrap('object_name', STRING, [NUMBER]),
        objectClass: wrap('object_class', NUMBER, [NUMBER]),
        className: wrap('class_name', STRING, [NUMBER]),
        objectActive: wrap('object_active', NUMBER, [NUMBER]),
        objectVisible: wrap('object_visible', NUMBER, [NUMBER]),
        scriptCount: wrap('script_count', NUMBER),
        scriptId: unsigned(wrap('script_id', NUMBER, [NUMBER])),
        label: wrap('label', STRING, [NUMBER]),
        behaviorActive: wrap('behavior_active', NUMBER, [NUMBER]),
        behaviorExecutions: wrap('behavior_executions', NUMBER, [NUMBER]),
        behaviorFlags: unsigned(wrap('behavior_flags', NUMBER, [NUMBER])),
        entityParent: unsigned(wrap('entity_parent', NUMBER, [NUMBER])),
        /* A 3D entity's world (or local) matrix, 16 floats; null for anything else */
        entityMatrix(object, world) {
            const at = entityMatrix(object, world ? 1 : 0);
            return at === 0 ? null : module.HEAPF32.slice(at / 4, at / 4 + 16);
        },
        /* A parameter's value: {bytes, type (GUID)}; null when the object holds none */
        paramValue(object) {
            const size = paramValue(object);
            if (size < 0) {
                return null;
            }
            const at = paramBytes();
            return { bytes: module.HEAPU8.slice(at, at + size), type: paramType() };
        },
        messageName: wrap('message_name', STRING, [NUMBER]),
        trace: wrap('trace', null, [NUMBER]),
        tracing: wrap('tracing', NUMBER),
        /* The trace's records, 6 words each (player.c's player_trace_record_t) */
        traceRecords() {
            const at = traceRecords();
            const words = traceCount() * 6;
            return at === 0 ? new Int32Array(0) : module.HEAP32.slice(at / 4, at / 4 + words);
        },
        traceDropped: wrap('trace_dropped', NUMBER),
        traceKindName: wrap('trace_kind_name', STRING, [NUMBER]),
    };
}

/* The files of a drop, with paths relative to it; webkitGetAsEntry must be
   called before the drop handler first awaits, so `entries` come in ready */
async function droppedFiles(entries) {
    const files = [];
    const readEntry = async (entry, path) => {
        if (entry.isFile) {
            const file = await new Promise((resolve, reject) => entry.file(resolve, reject));
            files.push([path + entry.name, file]);
        } else if (entry.isDirectory) {
            const reader = entry.createReader();
            for (;;) {
                const batch = await new Promise((resolve, reject) =>
                    reader.readEntries(resolve, reject)
                );
                if (batch.length === 0) {
                    break;
                }
                for (const child of batch) {
                    await readEntry(child, path + entry.name + '/');
                }
            }
        }
    };
    for (const entry of entries) {
        await readEntry(entry, '');
    }
    return files;
}

/* `url`'s body, telling onProgress(received, total) as it comes (total 0 when unknown) */
async function fetchWithProgress(url, onProgress) {
    const response = await fetch(url);
    if (!response.ok) {
        throw new Error(`${response.status} ${response.statusText}`);
    }
    const total = Number(response.headers.get('content-length')) || 0;
    if (!response.body) {
        return response.blob();
    }
    const reader = response.body.getReader();
    const chunks = [];
    let received = 0;
    for (;;) {
        const { done, value } = await reader.read();
        if (done) {
            break;
        }
        chunks.push(value);
        received += value.length;
        onProgress(received, total);
    }
    return new Blob(chunks, { type: response.headers.get('content-type') ?? '' });
}

/* A file is a zip by its name or its first bytes */
async function isZip(name, blob) {
    if (/\.zip$/i.test(name)) {
        return true;
    }
    const head = new Uint8Array(await blob.slice(0, 4).arrayBuffer());
    return head[0] === 0x50 && head[1] === 0x4b && head[2] === 3 && head[3] === 4;
}

function startPage() {
    const $ = (id) => document.getElementById(id);
    const status = $('status');
    const playPause = $('playpause');
    const muteButton = $('mute');
    const volume = $('volume');
    const settings = loadSettings();

    let module = null;
    let api = null;
    let library = null;
    let pending = null; /* files offered, not yet added: {files, byPath, source} */
    let current = null; /* the game playing (its meta) */
    let loadedId = null; /* the game whose files are in GAME_FOLDER */
    let paused = false;
    let autoPaused = false;
    let busy = false; /* adding, loading or removing a game */
    let fpsTimer = null;

    const devtools = createDevtools(
        $('devtools'),
        () => api,
        () => paused
    );
    const remapper = new KeyRemapper();
    const gamepads = new Gamepads((slot, state) => {
        if (api?.running() === 1) {
            api.setJoystick(slot, state);
        }
    });
    gamepads.enabled = settings.gamepad;
    gamepads.onChange = (pads) => {
        $('pad-note').hidden = pads.length === 0;
        $('pad-note').title = pads.join(', ');
    };

    function setStatus(text, progress) {
        status.textContent = text;
        $('loadbar').hidden = progress === undefined;
        if (progress !== undefined) {
            $('loadfill').style.width = `${Math.round(progress * 100)}%`;
        }
    }

    /* Run `task` unless another such task runs: adding, playing and removing games each
       change what GAME_FOLDER holds, so one at a time, the page's buttons off meanwhile */
    async function exclusively(task) {
        if (busy) {
            return null;
        }
        busy = true;
        document.body.classList.add('busy');
        try {
            return await task();
        } finally {
            busy = false;
            document.body.classList.remove('busy');
        }
    }

    /* ---- The player module, its download shown as it comes ---- */

    const downloads = { wasm: [0, 0], data: [0, 0] };
    function showDownload() {
        const received = downloads.wasm[0] + downloads.data[0];
        const total = downloads.wasm[1] + downloads.data[1];
        $('boot').textContent =
            total > 0
                ? `Loading the player… ${formatSize(received)} of ${formatSize(total)}`
                : 'Loading the player…';
    }

    function persist() {
        module?.FS.syncfs(false, (error) => {
            if (error) {
                devtools.print(`Could not keep the saves: ${error}`, 'warning');
            }
        });
    }

    const ready = createNmoPlayer({
        canvas: $('canvas'),
        print: (text) => devtools.print(text, text.startsWith('[console] ') ? 'console' : 'info'),
        printErr: (text) => devtools.print(text, /^Error\b/.test(text) ? 'error' : 'warning'),
        /* Emscripten reports the data package's download this way; the .wasm comes through
           instantiateWasm below */
        setStatus: (text) => {
            const match = /^Downloading data\.\.\. \((\d+)\/(\d+)\)/.exec(text);
            if (match) {
                downloads.data = [Number(match[1]), Number(match[2])];
                showDownload();
            }
        },
        instantiateWasm(imports, receiveInstance) {
            const url = new URL('nmo-player.wasm', import.meta.url);
            fetchWithProgress(url, (received, total) => {
                downloads.wasm = [received, total];
                showDownload();
            })
                .then((blob) => blob.arrayBuffer())
                .then((bytes) => WebAssembly.instantiate(bytes, imports))
                .then((result) => receiveInstance(result.instance, result.module))
                .catch((error) => {
                    $('boot').textContent = `The player could not load: ${error.message}`;
                });
            return {};
        },
        persistSaves: persist,
        onPlayerExit: (code) => {
            endRun(
                code === 0
                    ? `${current?.name ?? 'The game'} has ended.`
                    : 'The game stopped with an error (see the log).'
            );
        },
    }).then(async (created) => {
        module = created;
        api = bindApi(module);
        module.FS.mkdir(SAVES_FOLDER);
        module.FS.mount(module.IDBFS, {}, SAVES_FOLDER);
        await new Promise((resolve) => module.FS.syncfs(true, () => resolve()));
        module.FS.mkdir(GAME_FOLDER);
        $('boot').textContent = '';
        return module;
    });
    ready.catch((error) => {
        $('boot').textContent = `The player could not start: ${error}`;
    });

    function clearGameFiles() {
        removeTree(module, GAME_FOLDER);
        module.FS.mkdir(GAME_FOLDER);
        loadedId = null;
    }

    /* ---- The library ---- */

    async function refreshLibrary() {
        if (!library) {
            return;
        }
        const games = await library.list();
        $('games').replaceChildren(
            ...games.map((game) => {
                const profile = profileById(game.profile);
                const detail = [
                    profile.name,
                    game.entry,
                    formatSize(game.size),
                    `${game.count} files`,
                ];
                const playButton = smallButton('Play', () => play(game.id));
                playButton.className = 'play-button small';
                return element('li', { className: 'game' }, [
                    element('div', { className: 'game-info' }, [
                        element('strong', { textContent: game.name }),
                        element('span', { className: 'dim', textContent: detail.join(' · ') }),
                    ]),
                    element('div', { className: 'game-actions' }, [
                        playButton,
                        smallButton('Settings', () => openSettings(game.id)),
                        smallButton('Saves', () => openSaves(game.id)),
                        smallButton('Remove', () => removeGame(game)),
                    ]),
                ]);
            })
        );
        $('no-games').hidden = games.length > 0;
        if (library.persistent) {
            const estimate = await navigator.storage?.estimate?.().catch(() => null);
            $('storage-note').textContent = estimate?.usage
                ? `Kept in this browser · ${formatSize(estimate.usage)} used`
                : 'Kept in this browser';
        } else {
            $('storage-note').textContent =
                "This browser can't keep games: they last until the tab closes.";
        }
    }

    function removeGame(game) {
        if (
            busy ||
            !confirm(`Remove "${game.name}" from this browser? Its saves are deleted too.`)
        ) {
            return;
        }
        exclusively(async () => {
            if (current?.id === game.id) {
                stopRun();
            }
            await library.remove(game.id);
            await ready;
            removeTree(module, saveFolder(game.id));
            persist();
            if (loadedId === game.id) {
                clearGameFiles();
            }
            await refreshLibrary();
        });
    }

    /* ---- Adding a game ---- */

    const planForm = $('plan');
    const planEntry = $('plan-entry');
    const planProfile = $('plan-profile');

    const currentPlan = () =>
        planGame(
            pending.files.map(([path]) => path),
            {
                entry: planEntry.value,
                profile: planProfile.value,
            }
        );

    function describePlan() {
        const plan = currentPlan();
        const size = plan.files.reduce(
            (sum, [path]) => sum + (pending.byPath.get(path)?.size ?? 0),
            0
        );
        const from = plan.root || 'the top folder';
        $('plan-summary').textContent =
            `${plan.files.length} files${size ? `, ${formatSize(size)}` : ''}, from ${from}.`;
    }

    /* Offer `files` ([path, file-like]) as a game: the form to add it */
    function offer(files, source) {
        const plan = planGame(files.map(([path]) => path));
        if (!plan) {
            setStatus('There is no .cmo, .vmo or .nmo there to start.');
            return;
        }
        pending = { files, byPath: new Map(files), source };
        $('plan-name').value = plan.name;
        planEntry.replaceChildren(
            ...plan.entries.map((entry) => element('option', { value: entry, textContent: entry }))
        );
        planEntry.value = plan.entry;
        planProfile.value = plan.profile.id;
        describePlan();
        planForm.hidden = false;
        const known = plan.profile.recognizes(files.map(([path]) => path));
        setStatus(known ? `This looks like ${plan.profile.name}.` : '');
        $('plan-name').focus();
    }

    planEntry.addEventListener('change', describePlan);
    planProfile.addEventListener('change', describePlan);
    $('plan-cancel').addEventListener('click', () => {
        pending = null;
        planForm.hidden = true;
        setStatus('');
    });
    planForm.addEventListener('submit', (event) => {
        event.preventDefault();
        exclusively(addPending);
    });

    /* Copy the offered game into the library (and, on the way, into GAME_FOLDER), then play it */
    async function addPending() {
        if (pending === null) {
            return;
        }
        const plan = currentPlan();
        const name = $('plan-name').value.trim() || plan.name;
        await ready;
        stopRun();
        clearGameFiles();
        let copied = 0;
        const files = plan.files.map(([source, inner]) => {
            const file = pending.byPath.get(source);
            return [
                inner,
                {
                    size: file.size,
                    arrayBuffer: async () => {
                        const data = await file.arrayBuffer();
                        writeGameFile(module, `${GAME_FOLDER}/${inner}`, new Uint8Array(data));
                        copied++;
                        return data;
                    },
                },
            ];
        });
        planForm.hidden = true;
        setStatus('Copying…', 0);
        let game;
        try {
            navigator.storage?.persist?.();
            const onProgress = (done, total) =>
                setStatus(`Copying ${done} of ${total} files…`, done / total);
            game = await library.add(
                {
                    name,
                    entry: plan.inner,
                    profile: plan.profile.id,
                    source: pending.source ?? null,
                    settings: {},
                },
                files,
                onProgress
            );
        } catch (error) {
            clearGameFiles();
            setStatus(`Could not add the game: ${error.message}`);
            planForm.hidden = false;
            return;
        }
        loadedId = copied === files.length ? game.id : null;
        pending = null;
        await refreshLibrary();
        await loadAndStart(game.id);
    }

    async function offerZip(blob, source) {
        let zip;
        try {
            zip = await readZip(blob);
        } catch (error) {
            setStatus(`That does not look like a zip file (${error.message}).`);
            return;
        }
        if (zip.skipped.length > 0) {
            const some = zip.skipped.slice(0, 5).join(', ');
            devtools.print(
                `Left out ${zip.skipped.length} files whose paths leave the zip's folder: ${some}`,
                'warning'
            );
        }
        offer(zip.files, source);
    }

    async function offerFile(file) {
        if (await isZip(file.name, file)) {
            await offerZip(file, null);
        } else {
            offer([[file.name, file]], null);
        }
    }

    /* A game from a URL: a .zip of it, or a single composition (not again when one from
       the same URL is in the library) */
    async function loadUrl(url) {
        const absolute = new URL(url, location.href).href;
        const known = (await library.list()).find((game) => game.source === absolute);
        if (known) {
            setStatus(`"${known.name}" is in your games already: press Play.`);
            return;
        }
        setStatus('Downloading…', 0);
        let blob;
        try {
            blob = await fetchWithProgress(absolute, (received, total) => {
                const of = total ? ` of ${formatSize(total)}` : '';
                setStatus(
                    `Downloading… ${formatSize(received)}${of}`,
                    total ? received / total : 0
                );
            });
        } catch (error) {
            setStatus(`Could not download ${absolute}: ${error.message}`);
            return;
        }
        const name = decodeURIComponent(new URL(absolute).pathname.split('/').pop() || 'game');
        if (await isZip(name, blob)) {
            await offerZip(blob, absolute);
        } else {
            offer([[name, blob]], absolute);
        }
    }

    $('folder').addEventListener('change', (event) => {
        const files = [...event.target.files].map((file) => [
            file.webkitRelativePath || file.name,
            file,
        ]);
        event.target.value = '';
        offer(files, null);
    });
    $('file').addEventListener('change', (event) => {
        const file = event.target.files[0];
        event.target.value = '';
        if (file) {
            offerFile(file);
        }
    });
    $('url-form').addEventListener('submit', (event) => {
        event.preventDefault();
        const url = $('url').value.trim();
        if (url) {
            loadUrl(url);
        }
    });
    const drop = $('drop');
    drop.addEventListener('dragover', (event) => {
        event.preventDefault();
        drop.classList.add('over');
    });
    drop.addEventListener('dragleave', () => drop.classList.remove('over'));
    drop.addEventListener('drop', async (event) => {
        event.preventDefault();
        drop.classList.remove('over');
        const items = [...event.dataTransfer.items].filter((item) => item.kind === 'file');
        const entries = items.map((item) => item.webkitGetAsEntry?.()).filter(Boolean);
        if (items.length === 1 && !entries[0]?.isDirectory) {
            const file = items[0].getAsFile();
            if (file) {
                await offerFile(file);
                return;
            }
        }
        offer(await droppedFiles(entries), null);
    });

    /* ---- Playing ---- */

    /* The window scale: the largest whole one at which the game's render context still
       fits the stage, the player's own choice otherwise */
    function windowScale(size) {
        if (settings.scale !== 'auto') {
            return settings.scale;
        }
        const [width, height] = (size || '640x480').split('x').map(Number);
        const room = Math.min($('player').clientWidth / width, (0.7 * window.innerHeight) / height);
        return String(Math.max(1, Math.floor(room)));
    }

    function runArgs(game) {
        const own = gameSettings(game);
        const more = ['--scale', windowScale(own.size)];
        if (own.size) {
            more.push('--size', own.size);
        }
        if (settings.renderer !== 'auto') {
            more.push('--renderer', settings.renderer);
        }
        if (!settings.physics) {
            more.push('--physics', 'none');
        }
        if (!settings.frameLimit) {
            more.push('--no-frame-limit');
        }
        return playerArguments({
            profile: profileById(game.profile),
            entry: game.entry,
            values: own.values,
            registry: own.registry,
            write: saveFolder(game.id),
            more,
        }).join('\n');
    }

    function setControls(playing) {
        for (const id of ['playpause', 'restart', 'stop', 'screenshot', 'fullscreen']) {
            $(id).disabled = !playing;
        }
    }

    function setPaused(value) {
        paused = value;
        setHidden(playPause.querySelector('.icon-pause'), paused);
        setHidden(playPause.querySelector('.icon-play'), !paused);
        playPause.setAttribute('aria-label', paused ? 'Play' : 'Pause');
        playPause.title = paused ? 'Play' : 'Pause';
    }

    /* A new canvas for each run: one that held a WebGL 2 context can't give the software
       renderer another kind, nor the next run a clean one */
    function freshCanvas() {
        const canvas = $('canvas').cloneNode(false);
        $('canvas').replaceWith(canvas);
        module.canvas = canvas;
        return canvas;
    }

    /* The stage takes the shape of the game's render context, as the canvas has it */
    function fitStage(canvas) {
        if (canvas.width > 0 && canvas.height > 0) {
            $('stage').style.setProperty('--aspect', `${canvas.width} / ${canvas.height}`);
        }
    }

    /* The game's keys and gamepad buttons, from now on */
    function applyInput(game) {
        remapper.set(gameSettings(game).keys);
        gamepads.release();
        gamepads.mapping = gamepadMapping(game);
    }

    function applyVolume() {
        api?.setVolume(settings.volume);
        api?.setMute(settings.muted ? 1 : 0);
        setHidden(muteButton.querySelector('.icon-unmuted'), settings.muted);
        setHidden(muteButton.querySelector('.icon-muted'), !settings.muted);
        muteButton.setAttribute('aria-label', settings.muted ? 'Unmute' : 'Mute');
        muteButton.title = settings.muted ? 'Unmute' : 'Mute';
    }

    /* The frame rate and timings over the game, when the settings ask for them */
    function drawOverlay() {
        const overlay = $('overlay');
        overlay.hidden = !settings.overlay || api?.running() !== 1;
        if (!overlay.hidden) {
            overlay.textContent =
                `${$('fps').textContent} · behaviors ${api.stepMs().toFixed(1)} ms` +
                ` · drawing ${api.drawMs().toFixed(1)} ms`;
        }
    }

    /* Start `current` from the files in GAME_FOLDER; false (and the page back to its
       games) when it could not */
    function startRun() {
        const profile = profileById(current.profile);
        api.stop();
        devtools.reset();
        document.body.classList.add('playing');
        const canvas = freshCanvas();
        if (api.start(runArgs(current)) !== 0) {
            endRun('The game could not start (see the log).');
            showDevtools(true);
            devtools.show('log');
            return false;
        }
        applyVolume();
        api.setFrameLimit(settings.frameLimit ? 1 : 0);
        applyInput(current);
        buildTouchControls($('touch-controls'), profile);
        $('hint').textContent = profile.hint;
        document.title = `${current.name} · nmo player`;
        fitStage(canvas);
        setPaused(false);
        setControls(true);
        setStatus('');
        let frames = api.frames();
        clearInterval(fpsTimer);
        fpsTimer = setInterval(() => {
            const now = api.frames();
            $('fps').textContent = paused ? 'paused' : `${now - frames} fps`;
            frames = now;
            drawOverlay();
        }, 1000);
        canvas.focus();
        return true;
    }

    function endRun(message) {
        document.body.classList.remove('playing');
        clearInterval(fpsTimer);
        fpsTimer = null;
        $('fps').textContent = '';
        $('hint').textContent = '';
        $('overlay').hidden = true;
        remapper.releaseAll();
        gamepads.release();
        gamepads.mapping = null;
        setPaused(false);
        autoPaused = false;
        setControls(false);
        if (document.pointerLockElement) {
            document.exitPointerLock();
        }
        current = null;
        document.title = 'nmo player';
        devtools.reset();
        if (message) {
            setStatus(message);
        }
        refreshLibrary();
    }

    function stopRun() {
        if (api?.running() === 1) {
            api.stop();
            endRun('');
        }
    }

    /* Load game `id`'s files into GAME_FOLDER (unless there already) and start it */
    async function loadAndStart(id) {
        await ready;
        const game = await library.get(id);
        if (!game) {
            return;
        }
        stopRun();
        if (loadedId !== id) {
            clearGameFiles();
            const files = await library.files(id);
            let done = 0;
            for (const [inner, source] of files) {
                const data = new Uint8Array(await source.arrayBuffer());
                writeGameFile(module, `${GAME_FOLDER}/${inner}`, data);
                done++;
                if (done % 25 === 0 || done === files.length) {
                    setStatus(
                        `Loading ${game.name}… ${done} of ${files.length} files`,
                        done / files.length
                    );
                }
            }
            loadedId = id;
        }
        if (adoptLooseSaves(module, saveFolder(id))) {
            persist();
        }
        current = await library.update(id, { played: Date.now() });
        startRun();
    }

    function play(id) {
        exclusively(() => loadAndStart(id)).catch((error) => {
            if (module) {
                clearGameFiles();
            }
            setStatus(`Could not play the game: ${error.message}`);
        });
    }

    $('stop').addEventListener('click', stopRun);
    $('restart').addEventListener('click', () => {
        if (current && !busy) {
            startRun();
        }
    });

    playPause.addEventListener('click', () => {
        if (api?.running() !== 1) {
            return;
        }
        setPaused(!paused);
        if (paused) {
            api.pause();
        } else {
            api.resume();
        }
    });

    /* A hidden tab throttles requestAnimationFrame to a few fps (or less),
       the main loop's own clock: rather than stumble along at that rate (or
       jump by the huge gap it leaves once visible again, which can make a
       physics step unstable), freeze the engine while hidden and resume
       where it left off. Tracked apart from `paused` so a user's own pause
       is not silently lifted just because the tab came back into view. */
    document.addEventListener('visibilitychange', () => {
        if (api?.running() !== 1) {
            return;
        }
        if (document.hidden) {
            if (!paused) {
                api.pause();
                autoPaused = true;
            }
        } else if (autoPaused) {
            api.resume();
            autoPaused = false;
        }
    });

    muteButton.addEventListener('click', () => {
        settings.muted = !settings.muted;
        if (!settings.muted && settings.volume === 0) {
            settings.volume = 1;
        }
        volume.value = String(settings.muted ? 0 : settings.volume);
        applyVolume();
        saveSettings(settings);
    });
    volume.addEventListener('input', () => {
        settings.volume = Number(volume.value);
        settings.muted = settings.volume === 0;
        applyVolume();
    });
    volume.addEventListener('change', () => saveSettings(settings));
    volume.value = String(settings.muted ? 0 : settings.volume);
    applyVolume();

    $('screenshot').addEventListener('click', () => {
        const path = '/screenshot.png';
        if (api?.screenshot(path) !== 0) {
            devtools.print('Could not take the screenshot', 'warning');
            return;
        }
        const data = module.FS.readFile(path);
        module.FS.unlink(path);
        const name = `${fileNameOf(current?.name ?? 'nmo')} ${api.frames()}.png`;
        download(name, new Blob([data], { type: 'image/png' }));
    });

    $('stage').addEventListener('click', (event) => {
        const onGame = event.target.id === 'canvas';
        if (
            onGame &&
            settings.pointerLock &&
            api?.running() === 1 &&
            !document.pointerLockElement
        ) {
            Promise.resolve(event.target.requestPointerLock?.()).catch(() => {});
        }
    });

    const devtoolsButton = $('devtools-button');
    function showDevtools(shown) {
        $('devtools').hidden = !shown;
        devtoolsButton.setAttribute('aria-pressed', String(shown));
        devtools.refresh();
    }
    devtools.onOpen = () => devtoolsButton.setAttribute('aria-pressed', 'true');
    devtoolsButton.addEventListener('click', () => {
        settings.devtools = $('devtools').hidden;
        showDevtools(settings.devtools);
        saveSettings(settings);
    });
    showDevtools(settings.devtools);

    $('fullscreen').addEventListener('click', () => {
        const el = $('player');
        if (el.classList.contains('fake-fullscreen')) {
            el.classList.remove('fake-fullscreen');
            document.body.classList.remove('fullscreen-lock');
            return;
        }
        if (document.fullscreenElement) {
            document.exitFullscreen();
            return;
        }
        /* iPhone Safari grants the real Fullscreen API to <video> only: it
           may throw, reject, or just silently do nothing, so a plain CSS
           "cover the viewport" stand-in catches all three. */
        const fallback = () => {
            el.classList.add('fake-fullscreen');
            document.body.classList.add('fullscreen-lock');
        };
        if (!el.requestFullscreen) {
            fallback();
            return;
        }
        try {
            Promise.resolve(el.requestFullscreen()).catch(fallback);
        } catch {
            fallback();
            return;
        }
        setTimeout(() => {
            if (!document.fullscreenElement) {
                fallback();
            }
        }, 400);
    });
    document.addEventListener('fullscreenchange', () => {
        if (!document.fullscreenElement) {
            document.body.classList.remove('fullscreen-lock');
        }
    });

    /* ---- Settings and saves ---- */

    const settingsDialog = createSettingsDialog({
        dialog: $('settings'),
        settings,
        getLibrary: () => library,
        screenSizes: () => api.screenSizes(),
        onSave(game, restart) {
            saveSettings(settings);
            gamepads.enabled = settings.gamepad;
            api?.setFrameLimit(settings.frameLimit ? 1 : 0);
            drawOverlay();
            if (game && current?.id === game.id) {
                current = game;
                applyInput(game);
            }
            refreshLibrary();
            if (restart && current && !busy) {
                startRun();
            }
        },
    });

    async function openSettings(id) {
        await ready;
        settingsDialog.open(id ? await library.get(id) : null, current !== null);
    }

    $('settings-button').addEventListener('click', () => openSettings(current?.id));

    const savesDialog = createSavesDialog({
        dialog: $('saves'),
        getModule: () => module,
        persist,
        print: (text, kind) => devtools.print(text, kind),
        playing: () => current?.id ?? null,
    });

    async function openSaves(id) {
        await ready;
        savesDialog.open(await library.get(id));
    }

    /* ---- Start: the game profiles (profiles.json), then the library they describe ---- */

    if (/iPad|iPhone|iPod/.test(navigator.userAgent)) {
        $('ios-note').hidden = false;
    }

    loadProfiles()
        .catch((error) => {
            devtools.print(
                `Could not read the game profiles (profiles.json): ${error.message}`,
                'warning'
            );
        })
        .then(async () => {
            planProfile.replaceChildren(
                ...PROFILES.map((profile) =>
                    element('option', { value: profile.id, textContent: profile.name })
                )
            );
            library = await openLibrary();
            await refreshLibrary();
            const src = new URLSearchParams(location.search).get('src');
            if (src) {
                $('url').value = new URL(src, location.href).href;
                await loadUrl(src);
            }
        });
}

if (typeof document !== 'undefined') {
    startPage();
}
