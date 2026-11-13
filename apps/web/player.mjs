// nmo's web player page: a library of Virtools games kept in this browser,
// added from a folder, a .zip, a single composition or a URL; each runs in
// the engine (nmo-player.wasm) on the canvas, with its own settings, keys
// and saves, and the developer tools underneath.
//
// URL parameters: ?src=URL (a .zip, .cmo or .vmo to add, or play if it was
// added before), &entry=PATH (the composition inside a .zip), &profile=ID,
// &name=NAME, &autoplay=1 (add and start without asking).

import createNmoPlayer from './nmo-player.mjs';
import {planGame, writeGameFile, removeTree, listTree} from './games.mjs';
import {PROFILES, loadProfiles, profileById, registryFor} from './profiles.mjs';
import {openLibrary} from './library.mjs';
import {zipFiles, writeZip} from './zip.mjs';
import {KeyRemapper, Gamepads, buildTouchControls, keyName} from './input.mjs';
import {createDevtools} from './devtools.mjs';

const SETTINGS_KEY = 'nmo-player-settings';
const DEFAULT_SETTINGS = {
    renderer: 'auto',
    scale: 'auto',
    touch: 'auto',
    physics: true,
    frameLimit: true,
    gamepad: true,
    pointerLock: false,
    volume: 1,
    muted: false,
    devtools: false,
};

function loadSettings() {
    try {
        const saved = JSON.parse(localStorage.getItem(SETTINGS_KEY) ?? '{}');
        /* The page before the settings dialog kept only these two */
        if (localStorage.getItem('nmo-player-muted') === '1' && saved.muted === undefined) {
            saved.muted = true;
        }
        return {...DEFAULT_SETTINGS, ...saved};
    } catch {
        return {...DEFAULT_SETTINGS};
    }
}

function saveSettings(settings) {
    try {
        localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings));
    } catch {
        /* remembered only where storage works */
    }
}

/* The player's C functions (player.c's player_*) */
function bindApi(module) {
    const n = 'number', s = 'string';
    const wrap = (name, result, args = []) => module.cwrap(`player_${name}`, result, args);
    return {
        start: wrap('start', n, [s]),
        stop: wrap('stop', null),
        running: wrap('running', n),
        pause: wrap('pause', null),
        resume: wrap('resume', null),
        setMute: wrap('set_mute', null, [n]),
        setVolume: wrap('set_volume', null, [n]),
        setFrameLimit: wrap('set_frame_limit', null, [n]),
        screenshot: wrap('screenshot', n, [s]),
        setJoystick: wrap('set_joystick', null, [n, n, n, n, n, n, n, n, n, n]),
        frames: wrap('frames', n),
        stepMs: wrap('step_ms', n),
        drawMs: wrap('draw_ms', n),
        time: wrap('time', n),
        memory: wrap('memory', n),
        fileCount: wrap('file_count', n),
        fileName: wrap('file_name', s, [n]),
        objectCount: wrap('object_count', n, [n]),
        object: wrap('object', n, [n, n]),
        objectName: wrap('object_name', s, [n]),
        objectClass: wrap('object_class', n, [n]),
        className: wrap('class_name', s, [n]),
        objectState: wrap('object_state', n, [n]),
        scriptCount: wrap('script_count', n),
        scriptId: wrap('script_id', n, [n]),
        label: wrap('label', s, [n]),
        behaviorActive: wrap('behavior_active', n, [n]),
        behaviorExecutions: wrap('behavior_executions', n, [n]),
        messageName: wrap('message_name', s, [n]),
        trace: wrap('trace', null, [n]),
        tracing: wrap('tracing', n),
        traceCount: wrap('trace_count', n),
        traceKind: wrap('trace_kind', n, [n]),
        traceFrame: wrap('trace_frame', n, [n]),
        traceBehavior: wrap('trace_behavior', n, [n]),
        traceObject: wrap('trace_object', n, [n]),
        traceResult: wrap('trace_result', n, [n]),
        traceDepth: wrap('trace_depth', n, [n]),
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
                const batch = await new Promise((resolve, reject) => reader.readEntries(resolve, reject));
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

/* An SVG icon's `hidden` attribute, set through `toggleAttribute`: some
   engines don't reflect the `hidden` IDL property on SVG elements. */
function setHidden(element, hidden) {
    element.toggleAttribute('hidden', hidden);
}

function formatSize(bytes) {
    if (bytes >= 1073741824) return `${(bytes / 1073741824).toFixed(1)} GB`;
    if (bytes >= 1048576) return `${(bytes / 1048576).toFixed(1)} MB`;
    if (bytes >= 1024) return `${Math.round(bytes / 1024)} KB`;
    return `${bytes} B`;
}

function download(name, blob) {
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = name;
    document.body.append(link);
    link.click();
    link.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
}

/* A file name made safe for a download */
function fileNameOf(text) {
    return text.replace(/[\\/:*?"<>|]+/g, ' ').trim() || 'game';
}

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
        const {done, value} = await reader.read();
        if (done) {
            break;
        }
        chunks.push(value);
        received += value.length;
        onProgress(received, total);
    }
    return new Blob(chunks, {type: response.headers.get('content-type') ?? ''});
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
    const canvas = $('canvas');
    const status = $('status');
    const loadbar = $('loadbar');
    const loadfill = $('loadfill');
    const playPause = $('playpause');
    const muteButton = $('mute');
    const volume = $('volume');
    const settingsDialog = $('settings');
    const savesDialog = $('saves');
    const settings = loadSettings();

    let module = null;
    let api = null;
    let library = null;
    let games = [];
    let pending = null;    /* files offered, not yet added: {files, byPath, source} */
    let current = null;    /* the game playing (its meta) */
    let loadedId = null;   /* the game whose files are in /game */
    let paused = false;
    let autoPaused = false;
    let fpsTimer = null;
    let busy = false;

    const devtools = createDevtools($('devtools'), () => api);
    const remapper = new KeyRemapper();
    const gamepads = new Gamepads((...args) => api?.running() === 1 && api.setJoystick(...args));
    gamepads.enabled = settings.gamepad;
    gamepads.onChange = (pads) => {
        $('pad-note').hidden = pads.length === 0;
        $('pad-note').title = pads.join(', ');
    };

    const setStatus = (text, progress) => {
        status.textContent = text;
        loadbar.hidden = progress === undefined;
        if (progress !== undefined) {
            loadfill.style.width = `${Math.round(progress * 100)}%`;
        }
    };

    /* ---- The player module ---- */

    const persist = () => {
        module?.FS.syncfs(false, (error) => {
            if (error) {
                devtools.print('Could not keep the saves: ' + error, true);
            }
        });
    };

    const ready = createNmoPlayer({
        canvas,
        print: (text) => devtools.print(text, false),
        printErr: (text) => devtools.print(text, true),
        setStatus: (text) => {
            const match = /\((\d+)\/(\d+)\)/.exec(text);
            $('boot').textContent = match
                ? `Loading the player… ${Math.round((100 * Number(match[1])) / Number(match[2]))}%`
                : text || '';
        },
        persistSaves: persist,
        onPlayerExit: (code) => {
            endRun(code === 0 ? `${current?.name ?? 'The game'} has ended.` : 'The game stopped with an error (see the log).');
        },
    }).then(async (created) => {
        module = created;
        api = bindApi(module);
        module.FS.mkdir('/save');
        module.FS.mount(module.IDBFS, {}, '/save');
        await new Promise((resolve) => module.FS.syncfs(true, () => resolve()));
        module.FS.mkdir('/game');
        $('boot').textContent = '';
        return module;
    });
    ready.catch((error) => {
        $('boot').textContent = 'The player could not start: ' + error;
    });

    /* ---- The library ---- */

    const refreshLibrary = async () => {
        if (!library) {
            return;
        }
        games = await library.list();
        const list = $('games');
        list.replaceChildren(...games.map((game) => {
            const profile = profileById(game.profile);
            const item = document.createElement('li');
            item.className = 'game';
            const info = document.createElement('div');
            info.className = 'game-info';
            const name = document.createElement('strong');
            name.textContent = game.name;
            const detail = document.createElement('span');
            detail.className = 'dim';
            detail.textContent = [profile.name, game.entry, formatSize(game.size), `${game.count} files`].join(' · ');
            info.append(name, detail);
            const actions = document.createElement('div');
            actions.className = 'game-actions';
            const button = (label, className, action) => {
                const element = document.createElement('button');
                element.type = 'button';
                element.className = className;
                element.textContent = label;
                element.addEventListener('click', action);
                actions.append(element);
                return element;
            };
            button('Play', 'play-button small', () => play(game.id));
            button('Settings', 'button small', async () => openSettings(await library.get(game.id)));
            button('Saves', 'button small', () => openSaves(game));
            button('Remove', 'button small', () => removeGame(game));
            item.append(info, actions);
            return item;
        }));
        $('no-games').hidden = games.length > 0;
        if (library.persistent) {
            const estimate = await navigator.storage?.estimate?.().catch(() => null);
            $('storage-note').textContent = estimate?.usage
                ? `Kept in this browser · ${formatSize(estimate.usage)} used`
                : 'Kept in this browser';
        } else {
            $('storage-note').textContent = "This browser can't keep games: they last until the tab closes.";
        }
    };

    const removeGame = async (game) => {
        if (!confirm(`Remove "${game.name}" from this browser? Its saves are deleted too.`)) {
            return;
        }
        if (current?.id === game.id) {
            stopRun();
        }
        await library.remove(game.id);
        await ready;
        removeTree(module, `/save/${game.id}`);
        persist();
        if (loadedId === game.id) {
            removeTree(module, '/game');
            module.FS.mkdir('/game');
            loadedId = null;
        }
        await refreshLibrary();
    };

    /* ---- Adding a game ---- */

    const planForm = $('plan');
    const planEntry = $('plan-entry');
    const planProfile = $('plan-profile');

    const currentPlan = () => planGame(pending.files.map(([path]) => path), {
        entry: planEntry.value,
        profile: planProfile.value,
    });

    const describePlan = () => {
        const plan = currentPlan();
        const size = plan.files.reduce((sum, [path]) => sum + (pending.byPath.get(path)?.size ?? 0), 0);
        $('plan-summary').textContent = `${plan.files.length} files${size ? `, ${formatSize(size)}` : ''}, ` +
            `from ${plan.root || 'the top folder'}.`;
    };

    /* Offer `files` ([path, file-like]) as a game: the form to add it, or straight in with options.autoplay */
    const offer = async (files, source, options = {}) => {
        const plan = planGame(files.map(([path]) => path), options);
        if (!plan) {
            setStatus('There is no .cmo, .vmo or .nmo there to start.');
            return null;
        }
        pending = {files, byPath: new Map(files), source};
        $('plan-name').value = options.name ?? plan.name;
        planEntry.replaceChildren(...plan.entries.map((entry) => new Option(entry, entry)));
        planEntry.value = plan.entry;
        planProfile.value = plan.profile.id;
        describePlan();
        if (options.autoplay) {
            return addPending();
        }
        planForm.hidden = false;
        setStatus(plan.profile.detect ? `This looks like ${plan.profile.name}.` : '');
        $('plan-name').focus();
        return null;
    };

    planEntry.addEventListener('change', describePlan);
    planProfile.addEventListener('change', describePlan);
    $('plan-cancel').addEventListener('click', () => {
        pending = null;
        planForm.hidden = true;
        setStatus('');
    });
    planForm.addEventListener('submit', (event) => {
        event.preventDefault();
        addPending();
    });

    /* Copy the offered game into the library (and, on the way, into /game), then play it */
    const addPending = async () => {
        if (busy || pending === null) {
            return null;
        }
        busy = true;
        const plan = currentPlan();
        const name = $('plan-name').value.trim() || plan.name;
        await ready;
        stopRun();
        removeTree(module, '/game');
        module.FS.mkdir('/game');
        loadedId = null;
        let copied = 0;
        const files = plan.files.map(([source, inner]) => {
            const file = pending.byPath.get(source);
            return [inner, {
                size: file.size,
                arrayBuffer: async () => {
                    const data = await file.arrayBuffer();
                    writeGameFile(module, '/game/' + inner, new Uint8Array(data));
                    copied++;
                    return data;
                },
            }];
        });
        planForm.hidden = true;
        setStatus('Copying…', 0);
        let game;
        try {
            navigator.storage?.persist?.();
            game = await library.add({
                name,
                entry: plan.inner,
                profile: plan.profile.id,
                source: pending.source ?? null,
                settings: {values: {}, keys: [], pad: {}, registry: []},
            }, files, (done, total) => setStatus(`Copying ${done} of ${total} files…`, done / total));
        } catch (error) {
            busy = false;
            setStatus(`Could not add the game: ${error.message}`);
            planForm.hidden = false;
            return null;
        }
        if (copied === files.length) {
            loadedId = game.id;
        }
        pending = null;
        busy = false;
        await refreshLibrary();
        await play(game.id);
        return game;
    };

    const offerZip = async (blob, source, options) => {
        let files;
        try {
            files = zipFiles(await blob.arrayBuffer());
        } catch (error) {
            setStatus(`That does not look like a zip file (${error.message}).`);
            return null;
        }
        return offer(files, source, options);
    };

    const offerFile = async (file, options) => (await isZip(file.name, file))
        ? offerZip(file, null, options)
        : offer([[file.name, file]], null, options);

    const loadUrl = async (url, options = {}) => {
        const absolute = new URL(url, location.href).href;
        const known = games.find((game) => game.source === absolute);
        if (known) {
            if (options.autoplay) {
                await play(known.id);
            } else {
                setStatus(`"${known.name}" is in your games already.`);
            }
            return known;
        }
        setStatus('Downloading…', 0);
        let blob;
        try {
            blob = await fetchWithProgress(absolute, (received, total) => setStatus(
                `Downloading… ${formatSize(received)}${total ? ` of ${formatSize(total)}` : ''}`,
                total ? received / total : 0));
        } catch (error) {
            setStatus(`Could not download ${absolute}: ${error.message}`);
            return null;
        }
        const name = decodeURIComponent(new URL(absolute).pathname.split('/').pop() || 'game');
        return (await isZip(name, blob))
            ? offerZip(blob, absolute, options)
            : offer([[name, blob]], absolute, options);
    };

    $('folder').addEventListener('change', (event) => {
        offer([...event.target.files].map((file) => [file.webkitRelativePath || file.name, file]), null);
        event.target.value = '';
    });
    $('file').addEventListener('change', (event) => {
        const file = event.target.files[0];
        if (file) {
            offerFile(file);
        }
        event.target.value = '';
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

    /* A game's own settings, as the dialog keeps them: profile option
       values, key pairs [player's key, game key], gamepad buttons
       {index: key code ('' for none)} over the profile's, and registry
       entries [key, name, value]; read from the earlier shape too
       (keymap {game key: player's key}, registry as KEY\NAME=VALUE lines) */
    const gameSettings = (game) => {
        const own = game.settings ?? {};
        const keys = own.keys ?? Object.entries(own.keymap ?? {}).map(([to, from]) => [from, to]);
        let registry = own.registry ?? [];
        if (typeof registry === 'string') {
            registry = registry.split('\n').map((line) => line.trim()).filter(Boolean).map((line) => {
                const equals = line.indexOf('=');
                const head = equals >= 0 ? line.slice(0, equals) : line;
                const slash = head.lastIndexOf('\\');
                return [head.slice(0, Math.max(slash, 0)), head.slice(slash + 1), equals >= 0 ? line.slice(equals + 1) : ''];
            });
        }
        return {values: own.values ?? {}, keys, pad: own.pad ?? {}, registry};
    };

    /* The game's keys and gamepad buttons, from now on */
    const applyInput = (game) => {
        const profile = profileById(game.profile);
        const own = gameSettings(game);
        remapper.set(own.keys);
        gamepads.release();
        gamepads.mapping = {
            stick: profile.gamepad?.stick,
            buttons: {...(profile.gamepad?.buttons ?? {}), ...own.pad},
        };
    };

    const runArgs = (game) => {
        const profile = profileById(game.profile);
        const own = gameSettings(game);
        const lines = ['--write', `/save/${game.id}`, '--path', '/game'];
        const registry = registryFor(profile, own.values, own.registry);
        if (registry) {
            lines.push('--registry', registry);
        }
        if (settings.renderer !== 'auto') {
            lines.push('--renderer', settings.renderer);
        }
        const scale = settings.scale !== 'auto' ? settings.scale
            : window.innerWidth >= 1340 && window.innerHeight >= 1080 ? '2' : '1';
        lines.push('--scale', scale);
        if (!settings.physics) {
            lines.push('--physics', 'none');
        }
        if (!settings.frameLimit) {
            lines.push('--no-frame-limit');
        }
        lines.push('/game/' + game.entry);
        return lines.join('\n');
    };

    /* Saves the page before the library kept straight in /save (Ballance's):
       the first Ballance game played takes them */
    const adoptOldSaves = (game) => {
        const folder = `/save/${game.id}`;
        if (game.profile !== 'ballance' || module.FS.analyzePath(folder).exists) {
            return;
        }
        module.FS.mkdir(folder);
        for (const name of module.FS.readdir('/save')) {
            const path = `/save/${name}`;
            if (name !== '.' && name !== '..' && !module.FS.isDir(module.FS.stat(path).mode)) {
                module.FS.rename(path, `${folder}/${name}`);
            }
        }
        persist();
    };

    const setControls = (playing) => {
        for (const id of ['playpause', 'restart', 'stop', 'screenshot', 'fullscreen']) {
            $(id).disabled = !playing;
        }
    };

    const setPaused = (value) => {
        paused = value;
        setHidden(playPause.querySelector('.icon-pause'), paused);
        setHidden(playPause.querySelector('.icon-play'), !paused);
        playPause.setAttribute('aria-label', paused ? 'Play' : 'Pause');
        playPause.title = paused ? 'Play' : 'Pause';
    };

    /* The stage takes the shape of the game's render context, as the canvas has it */
    const fitStage = () => {
        if (canvas.width > 0 && canvas.height > 0) {
            $('stage').style.setProperty('--aspect', `${canvas.width} / ${canvas.height}`);
        }
    };

    const startRun = () => {
        const profile = profileById(current.profile);
        document.body.classList.add('playing');
        devtools.reset();
        const code = api.start(runArgs(current));
        if (code !== 0) {
            document.body.classList.remove('playing');
            setStatus('The game could not start (see the log).');
            $('devtools').hidden = false;
            devtools.show('log');
            return false;
        }
        api.setVolume(settings.volume);
        api.setMute(settings.muted ? 1 : 0);
        api.setFrameLimit(settings.frameLimit ? 1 : 0);
        applyInput(current);
        buildTouchControls($('touch-controls'), profile);
        $('hint').textContent = profile.hint;
        document.title = `${current.name} · nmo player`;
        fitStage();
        setPaused(false);
        setControls(true);
        setStatus('');
        let frames = api.frames();
        clearInterval(fpsTimer);
        fpsTimer = setInterval(() => {
            const now = api.frames();
            $('fps').textContent = paused ? 'paused' : `${now - frames} fps`;
            frames = now;
        }, 1000);
        canvas.focus();
        return true;
    };

    function endRun(message) {
        document.body.classList.remove('playing');
        clearInterval(fpsTimer);
        fpsTimer = null;
        $('fps').textContent = '';
        $('hint').textContent = '';
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

    async function play(id) {
        if (busy && pending === null) {
            return;
        }
        await ready;
        const game = await library.get(id);
        if (!game) {
            return;
        }
        busy = true;
        stopRun();
        if (loadedId !== id) {
            removeTree(module, '/game');
            module.FS.mkdir('/game');
            loadedId = null;
            const files = await library.files(id);
            let done = 0;
            for (const [inner, source] of files) {
                writeGameFile(module, '/game/' + inner, new Uint8Array(await source.arrayBuffer()));
                if (++done % 25 === 0 || done === files.length) {
                    setStatus(`Loading ${game.name}… ${done} of ${files.length} files`, done / files.length);
                }
            }
            loadedId = id;
        }
        adoptOldSaves(game);
        module.FS.mkdirTree(`/save/${id}`);
        current = await library.update(id, {played: Date.now()});
        busy = false;
        startRun();
    }

    $('stop').addEventListener('click', stopRun);
    $('restart').addEventListener('click', () => {
        if (current) {
            api.stop();
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

    const showMute = () => {
        setHidden(muteButton.querySelector('.icon-unmuted'), settings.muted);
        setHidden(muteButton.querySelector('.icon-muted'), !settings.muted);
        muteButton.setAttribute('aria-label', settings.muted ? 'Unmute' : 'Mute');
        muteButton.title = settings.muted ? 'Unmute' : 'Mute';
        volume.value = String(settings.muted ? 0 : settings.volume);
    };
    muteButton.addEventListener('click', () => {
        settings.muted = !settings.muted;
        if (!settings.muted && settings.volume === 0) {
            settings.volume = 1;
        }
        api?.setVolume(settings.volume);
        api?.setMute(settings.muted ? 1 : 0);
        showMute();
        saveSettings(settings);
    });
    volume.addEventListener('input', () => {
        settings.volume = Number(volume.value);
        settings.muted = settings.volume === 0;
        api?.setVolume(settings.volume);
        api?.setMute(settings.muted ? 1 : 0);
        setHidden(muteButton.querySelector('.icon-unmuted'), settings.muted);
        setHidden(muteButton.querySelector('.icon-muted'), !settings.muted);
    });
    volume.addEventListener('change', () => saveSettings(settings));
    showMute();

    $('screenshot').addEventListener('click', () => {
        const path = '/screenshot.png';
        if (api?.screenshot(path) !== 0) {
            devtools.print('Could not take the screenshot', true);
            return;
        }
        const data = module.FS.readFile(path);
        module.FS.unlink(path);
        download(`${fileNameOf(current?.name ?? 'nmo')} ${api.frames()}.png`, new Blob([data], {type: 'image/png'}));
    });

    canvas.addEventListener('click', () => {
        if (settings.pointerLock && api?.running() === 1 && !document.pointerLockElement) {
            Promise.resolve(canvas.requestPointerLock?.()).catch(() => {});
        }
    });

    const devtoolsButton = $('devtools-button');
    const showDevtools = (shown) => {
        $('devtools').hidden = !shown;
        devtoolsButton.setAttribute('aria-pressed', String(shown));
        devtools.refresh();
    };
    devtoolsButton.addEventListener('click', () => {
        settings.devtools = $('devtools').hidden;
        showDevtools(settings.devtools);
        saveSettings(settings);
    });
    showDevtools(settings.devtools);

    const applyTouchMode = () => {
        document.body.classList.toggle('touch-always', settings.touch === 'always');
        document.body.classList.toggle('touch-never', settings.touch === 'never');
    };
    applyTouchMode();

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

    /* ---- Settings ---- */

    let editing = null;      /* the game whose settings the dialog shows */
    let draft = null;        /* its settings as the dialog has them */
    let capturing = null;    /* what waits for a key: {list: 'keys', row, side} or {list: 'pad', button} */

    const PAD_BUTTONS = [
        [0, 'A / Cross'], [1, 'B / Circle'], [2, 'X / Square'], [3, 'Y / Triangle'],
        [4, 'Left shoulder'], [5, 'Right shoulder'], [6, 'Left trigger'], [7, 'Right trigger'],
        [8, 'Back / Select'], [9, 'Start'], [10, 'Left stick'], [11, 'Right stick'],
    ];

    const element = (tag, props = {}, children = []) => {
        const node = Object.assign(document.createElement(tag), props);
        node.append(...children);
        return node;
    };
    const smallButton = (text, action, extra = '') => {
        const button = element('button', {type: 'button', className: `button small ${extra}`, textContent: text});
        button.addEventListener('click', action);
        return button;
    };
    const capture = (target) => {
        capturing = target;
        drawKeymap();
        drawPadmap();
    };
    const capturingIs = (list, a, b) => capturing?.list === list &&
        (list === 'pad' ? capturing.button === a : capturing.row === a && capturing.side === b);

    function drawKeymap() {
        const table = $('keymap');
        if (draft.keys.length === 0) {
            table.replaceChildren(element('tr', {}, [element('td', {className: 'dim', textContent: 'The game gets the keys you press, as they are.'})]));
            return;
        }
        table.replaceChildren(...draft.keys.map(([from, to], row) => {
            const key = (code, side) => smallButton(
                capturingIs('keys', row, side) ? 'Press a key…' : code ? keyName(code) : '?',
                () => capture({list: 'keys', row, side}), 'key');
            return element('tr', {}, [
                element('td', {}, [key(from, 'from')]),
                element('td', {className: 'dim', textContent: '→'}),
                element('td', {}, [key(to, 'to')]),
                element('td', {}, [smallButton('Remove', () => {
                    draft.keys.splice(row, 1);
                    capture(null);
                })]),
            ]);
        }));
    }

    function drawPadmap() {
        const profile = profileById(editing.profile);
        const defaults = profile.gamepad?.buttons ?? {};
        $('padmap').replaceChildren(...PAD_BUTTONS.map(([button, label]) => {
            const own = Object.hasOwn(draft.pad, button);
            const code = own ? draft.pad[button] : defaults[button];
            const actions = [smallButton(
                capturingIs('pad', button) ? 'Press a key…' : code ? keyName(code) : 'Nothing',
                () => capture({list: 'pad', button}), 'key')];
            if (code) {
                actions.push(smallButton('Clear', () => {
                    draft.pad[button] = '';
                    capture(null);
                }));
            }
            if (own) {
                actions.push(smallButton('Default', () => {
                    delete draft.pad[button];
                    capture(null);
                }));
            }
            return element('tr', {}, [element('td', {textContent: label}), element('td', {}, actions)]);
        }));
    }

    function drawRegistry() {
        const table = $('registry');
        if (draft.registry.length === 0) {
            table.replaceChildren(element('tr', {}, [element('td', {className: 'dim', textContent: 'None added.'})]));
            return;
        }
        const fields = [['Key', 'Software\\Company\\Game'], ['Name', 'Setting'], ['Value', '1']];
        table.replaceChildren(...draft.registry.map((entry, row) => element('tr', {}, [
            ...fields.map(([label, placeholder], column) => {
                const input = element('input', {value: entry[column] ?? '', placeholder, spellcheck: false});
                input.setAttribute('aria-label', label);
                input.addEventListener('input', () => {
                    entry[column] = input.value;
                });
                return element('td', {}, [input]);
            }),
            element('td', {}, [smallButton('Remove', () => {
                draft.registry.splice(row, 1);
                drawRegistry();
            })]),
        ])));
    }

    $('keymap-add').addEventListener('click', () => {
        draft.keys.push(['', '']);
        capture({list: 'keys', row: draft.keys.length - 1, side: 'from'});
    });
    $('registry-add').addEventListener('click', () => {
        draft.registry.push(['', '', '']);
        drawRegistry();
        $('registry').querySelector('tr:last-child input')?.focus();
    });

    /* The key the player presses while a key button waits for one; Esc
       gives up (and drops a new key row left half chosen) */
    window.addEventListener('keydown', (event) => {
        if (capturing === null || !settingsDialog.open) {
            return;
        }
        event.preventDefault();
        event.stopImmediatePropagation();
        const target = capturing;
        capturing = null;
        if (target.list === 'pad') {
            if (event.code !== 'Escape') {
                draft.pad[target.button] = event.code;
            }
        } else {
            const pair = draft.keys[target.row];
            if (event.code === 'Escape') {
                if (!pair[0] || !pair[1]) {
                    draft.keys.splice(target.row, 1);
                }
            } else {
                pair[target.side === 'from' ? 0 : 1] = event.code;
                if (target.side === 'from' && !pair[1]) {
                    capturing = {list: 'keys', row: target.row, side: 'to'};
                }
            }
        }
        drawKeymap();
        drawPadmap();
    }, true);

    function openSettings(game) {
        editing = game ?? null;
        capturing = null;
        $('game-settings').hidden = editing === null;
        if (editing) {
            const profile = profileById(editing.profile);
            const own = gameSettings(editing);
            draft = {keys: own.keys.map((pair) => [...pair]), pad: {...own.pad},
                     registry: own.registry.map((entry) => [...entry])};
            $('game-settings-title').textContent =
                editing.name === profile.name ? editing.name : `${editing.name} (${profile.name})`;
            $('profile-options').hidden = profile.settings.length === 0;
            $('profile-options-title').textContent = `${profile.name} options`;
            $('profile-settings').replaceChildren(...profile.settings.map((setting) => {
                const select = element('select');
                select.dataset.setting = setting.id;
                for (const [value, text] of setting.options) {
                    select.append(new Option(text, value));
                }
                select.value = own.values[setting.id] ?? setting.default;
                return element('label', {}, [setting.label, ' ', select]);
            }));
            drawKeymap();
            drawPadmap();
            drawRegistry();
        }
        $('opt-renderer').value = settings.renderer;
        $('opt-scale').value = settings.scale;
        $('opt-touch').value = settings.touch;
        $('opt-physics').checked = settings.physics;
        $('opt-frame-limit').checked = settings.frameLimit;
        $('opt-gamepad').checked = settings.gamepad;
        $('opt-pointer-lock').checked = settings.pointerLock;
        $('settings-restart').hidden = current === null;
        settingsDialog.showModal();
    }

    $('settings-button').addEventListener('click', () => openSettings(current));

    /* Done or Save and restart (the form's submit, before the dialog closes;
       Esc closes it without one and keeps everything as it was) */
    settingsDialog.querySelector('form').addEventListener('submit', async (event) => {
        const choice = event.submitter?.value;
        capturing = null;
        if (choice !== 'save' && choice !== 'restart') {
            return;
        }
        Object.assign(settings, {
            renderer: $('opt-renderer').value,
            scale: $('opt-scale').value,
            touch: $('opt-touch').value,
            physics: $('opt-physics').checked,
            frameLimit: $('opt-frame-limit').checked,
            gamepad: $('opt-gamepad').checked,
            pointerLock: $('opt-pointer-lock').checked,
        });
        saveSettings(settings);
        applyTouchMode();
        gamepads.enabled = settings.gamepad;
        api?.setFrameLimit(settings.frameLimit ? 1 : 0);
        if (editing) {
            const values = {};
            for (const select of $('profile-settings').querySelectorAll('select')) {
                values[select.dataset.setting] = select.value;
            }
            const updated = await library.update(editing.id, {
                settings: {
                    values,
                    keys: draft.keys.filter(([from, to]) => from && to),
                    pad: draft.pad,
                    registry: draft.registry
                        .map((entry) => entry.map((part) => part.trim()))
                        .filter(([key, name]) => key && name),
                },
            });
            if (current?.id === updated.id) {
                current = updated;
                applyInput(updated);
            }
            refreshLibrary();
        }
        if (choice === 'restart' && current) {
            api.stop();
            startRun();
        }
    });

    /* ---- Saves ---- */

    let savesGame = null;

    const drawSaves = () => {
        const folder = `/save/${savesGame.id}`;
        const files = listTree(module, folder);
        const table = $('saves-table');
        if (files.length === 0) {
            const row = document.createElement('tr');
            const cell = document.createElement('td');
            cell.className = 'dim';
            cell.textContent = 'Nothing saved yet.';
            row.append(cell);
            table.replaceChildren(row);
        } else {
            table.replaceChildren(...files.map(([path, size, time]) => {
                const row = document.createElement('tr');
                for (const text of [path, formatSize(size), new Date(time).toLocaleString()]) {
                    const cell = document.createElement('td');
                    cell.textContent = text;
                    row.append(cell);
                }
                return row;
            }));
        }
        $('saves-export').disabled = files.length === 0;
        $('saves-clear').disabled = files.length === 0;
    };

    async function openSaves(game) {
        await ready;
        savesGame = game;
        $('saves-title').textContent = `Saves of ${game.name}`;
        drawSaves();
        savesDialog.showModal();
    }

    $('saves-export').addEventListener('click', () => {
        const folder = `/save/${savesGame.id}`;
        const files = listTree(module, folder).map(([path]) => [path, module.FS.readFile(`${folder}/${path}`)]);
        download(`${fileNameOf(savesGame.name)} saves.zip`, writeZip(files));
    });
    $('saves-import').addEventListener('change', async (event) => {
        const file = event.target.files[0];
        event.target.value = '';
        if (!file) {
            return;
        }
        try {
            const folder = `/save/${savesGame.id}`;
            for (const [path, entry] of zipFiles(await file.arrayBuffer())) {
                writeGameFile(module, `${folder}/${path}`, new Uint8Array(await entry.arrayBuffer()));
            }
            persist();
            drawSaves();
            if (current?.id === savesGame.id) {
                devtools.print('Saves imported: restart the game to load them.', false);
            }
        } catch (error) {
            alert(`Could not import ${file.name}: ${error.message}`);
        }
    });
    $('saves-clear').addEventListener('click', () => {
        if (!confirm(`Delete every save of "${savesGame.name}"? This cannot be undone.`)) {
            return;
        }
        const folder = `/save/${savesGame.id}`;
        removeTree(module, folder);
        module.FS.mkdir(folder);
        persist();
        drawSaves();
    });

    /* ---- Start ---- */

    if (/iPad|iPhone|iPod/.test(navigator.userAgent)) {
        $('ios-note').hidden = false;
    }

    /* The game profiles (profiles.json), then the library they describe */
    const opened = loadProfiles().catch((error) => {
        devtools.print(`Could not read the game profiles (profiles.json): ${error.message}`, true);
    }).then(() => {
        planProfile.replaceChildren(...PROFILES.map((profile) => new Option(profile.name, profile.id)));
        return openLibrary();
    }).then(async (opened) => {
        library = opened;
        await refreshLibrary();
        const params = new URLSearchParams(location.search);
        const src = params.get('src');
        if (src) {
            $('url').value = src;
            await loadUrl(src, {
                entry: params.get('entry') ?? undefined,
                profile: params.get('profile') ?? undefined,
                name: params.get('name') ?? undefined,
                autoplay: params.get('autoplay') === '1',
            });
        }
        return library;
    });

    /* For embedding pages and tests */
    globalThis.nmoPlayer = {
        ready,
        library: opened,
        /* Offer files fetched from `base` + each of `paths`, as a chosen folder would */
        loadFromUrls: async (base, paths, options) => {
            await opened;
            const files = [];
            for (const path of paths) {
                const response = await fetch(base + path.split('/').map(encodeURIComponent).join('/'));
                files.push([path, await response.blob()]);
            }
            return offer(files, null, options);
        },
        loadUrl: async (url, options) => {
            await opened;
            return loadUrl(url, options);
        },
        games: async () => {
            await opened;
            return library.list();
        },
        play: async (id) => {
            await opened;
            return play(id);
        },
        stop: () => stopRun(),
        api: () => api,
        module: () => module,
    };
}

if (typeof document !== 'undefined') {
    startPage();
}
