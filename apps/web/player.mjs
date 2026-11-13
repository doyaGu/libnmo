// nmo's web player page: the player picks their game folder, its files are
// copied into the player's memory file system (nothing leaves the browser),
// and the engine runs base.cmo on the canvas. Node.js (run_node.mjs) imports
// the same file selection.

import createNmoPlayer from './nmo-player.mjs';

/* The parts of a Ballance folder the game reads, by their first path part (any case) */
const GAME_PARTS = ['base.cmo', 'database.tdb', '3d entities', 'textures', 'sounds', 'text'];

/*
 * The game's files among `paths` (relative paths with '/' separators, as a
 * folder picker or a drop gives them): those under the folder holding
 * base.cmo, as [source path, path inside that folder]. Empty when no
 * base.cmo is there.
 */
export function selectGameFiles(paths) {
    let root = null;
    for (const path of paths) {
        const parts = path.split('/');
        if (parts[parts.length - 1].toLowerCase() === 'base.cmo') {
            const folder = parts.slice(0, -1).join('/');
            if (root === null || folder.length < root.length) {
                root = folder;
            }
        }
    }
    if (root === null) {
        return [];
    }
    const prefix = root === '' ? '' : root + '/';
    const selected = [];
    for (const path of paths) {
        if (!path.startsWith(prefix)) {
            continue;
        }
        const inner = path.slice(prefix.length);
        if (GAME_PARTS.includes(inner.split('/')[0].toLowerCase())) {
            selected.push([path, inner]);
        }
    }
    return selected;
}

/* Write `data` (bytes) at `path` in the module's file system, making its folders */
export function writeGameFile(module, path, data) {
    const slash = path.lastIndexOf('/');
    if (slash > 0) {
        module.FS.mkdirTree(path.slice(0, slash));
    }
    module.FS.writeFile(path, data);
}

/* ---- The page ---- */

const LANGUAGES = [
    ['0', 'Deutsch'],
    ['1', 'English'],
    ['2', 'Español'],
    ['3', 'Italiano'],
    ['4', 'Français'],
];

/* The files of a dropped folder, with paths relative to the drop */
async function droppedFiles(items) {
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
    const entries = [];
    for (const item of items) {
        const entry = item.webkitGetAsEntry ? item.webkitGetAsEntry() : null;
        if (entry !== null) {
            entries.push(entry);
        }
    }
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

/* A touch screen's D-pad/Esc, as a real keydown/keyup the view's keyboard
   polling already reads (the engine re-syncs the arrow keys from that same
   tracked state every frame, so this has to go through it rather than some
   separate side channel). */
const KEY_INFO = {
    ArrowUp: {keyCode: 38, key: 'ArrowUp'},
    ArrowDown: {keyCode: 40, key: 'ArrowDown'},
    ArrowLeft: {keyCode: 37, key: 'ArrowLeft'},
    ArrowRight: {keyCode: 39, key: 'ArrowRight'},
    Enter: {keyCode: 13, key: 'Enter'},
    Escape: {keyCode: 27, key: 'Escape'},
    Space: {keyCode: 32, key: ' '},
    ShiftLeft: {keyCode: 16, key: 'Shift'},
};

function sendKey(code, down) {
    const info = KEY_INFO[code];
    document.dispatchEvent(new KeyboardEvent(down ? 'keydown' : 'keyup', {
        code,
        key: info.key,
        keyCode: info.keyCode,
        which: info.keyCode,
        bubbles: true,
        cancelable: true,
    }));
}

/* Each button tracks its own held pointers (by id), so two fingers on two
   different buttons (e.g. steering a diagonal) hold both keys down, and a
   second finger joining or leaving an already-held button does not send a
   spurious extra keydown/keyup. `sync` only acts on an actual down/up
   transition: releasing a captured pointer fires both `pointerup` and
   `lostpointercapture`, and a plain size check would send keyup twice. */
function setupTouchControls() {
    for (const button of document.querySelectorAll('#touch-controls [data-key]')) {
        const code = button.dataset.key;
        const held = new Set();
        let down = false;
        const sync = () => {
            const wantDown = held.size > 0;
            if (wantDown !== down) {
                down = wantDown;
                sendKey(code, down);
            }
        };
        const press = (event) => {
            event.preventDefault();
            button.setPointerCapture(event.pointerId);
            held.add(event.pointerId);
            sync();
        };
        const release = (event) => {
            held.delete(event.pointerId);
            sync();
        };
        button.addEventListener('pointerdown', press);
        button.addEventListener('pointerup', release);
        button.addEventListener('pointercancel', release);
        button.addEventListener('lostpointercapture', release);
    }
}

function startPage() {
    const $ = (id) => document.getElementById(id);
    const status = $('status');
    const loadbar = $('loadbar');
    const loadfill = $('loadfill');
    const startButton = $('start');
    const log = $('log');
    const language = $('language');
    const playPause = $('playpause');
    const muteButton = $('mute');
    const fullscreenButton = $('fullscreen');
    let gameReady = false;
    let paused = false;
    let muted = false;
    let fpsTimer = null;

    for (const [value, label] of LANGUAGES) {
        const option = document.createElement('option');
        option.value = value;
        option.textContent = label;
        language.append(option);
    }
    try {
        language.value = localStorage.getItem('nmo-player-language') ?? '1';
        muted = localStorage.getItem('nmo-player-muted') === '1';
    } catch {
        language.value = '1';
    }
    setHidden($('mute').querySelector('.icon-unmuted'), muted);
    setHidden($('mute').querySelector('.icon-muted'), !muted);
    setupTouchControls();

    /* iOS Safari's folder picker has a long history of silently falling
       back to individual files instead of a real folder; there is no
       reliable feature test for that, so this is a plain platform check. */
    if (/iPad|iPhone|iPod/.test(navigator.userAgent) && !window.MSStream) {
        $('ios-note').hidden = false;
    }

    const print = (text, error) => {
        const line = document.createElement('div');
        line.textContent = text;
        if (error) {
            line.className = 'error';
            $('log-box').open = true;
        }
        log.append(line);
        log.scrollTop = log.scrollHeight;
    };

    let module = null;
    const persist = () => {
        module?.FS.syncfs(false, (error) => {
            if (error) {
                print('Could not keep the saves: ' + error, true);
            }
        });
    };
    const ready = createNmoPlayer({
        canvas: $('canvas'),
        print: (text) => print(text, false),
        printErr: (text) => print(text, true),
        persistSaves: persist,
        onPlayerExit: (code) => {
            status.textContent = code === 0 ? 'The game has ended. Press Start to play again.' : 'The game stopped with an error (see the log).';
            document.body.classList.remove('playing');
            if (fpsTimer !== null) {
                clearInterval(fpsTimer);
                fpsTimer = null;
            }
            $('fps').textContent = '';
            paused = false;
            autoPaused = false;
            setHidden(playPause.querySelector('.icon-pause'), false);
            setHidden(playPause.querySelector('.icon-play'), true);
            playPause.disabled = true;
            muteButton.disabled = true;
            fullscreenButton.disabled = true;
        },
    }).then(async (created) => {
        module = created;
        module.FS.mkdir('/save');
        module.FS.mount(module.IDBFS, {}, '/save');
        await new Promise((resolve) => module.FS.syncfs(true, () => resolve()));
        module.FS.mkdir('/game');
        return module;
    });
    ready.catch((error) => {
        status.textContent = 'The player could not start: ' + error;
    });

    const load = async (files) => {
        const selected = selectGameFiles(files.map(([path]) => path));
        if (selected.length === 0) {
            status.textContent = 'That folder has no base.cmo. Choose the Ballance folder.';
            return;
        }
        const byPath = new Map(files);
        const player = await ready;
        startButton.disabled = true;
        loadbar.hidden = false;
        let done = 0;
        for (const [source, inner] of selected) {
            const data = new Uint8Array(await byPath.get(source).arrayBuffer());
            writeGameFile(player, '/game/' + inner, data);
            done++;
            if (done % 25 === 0 || done === selected.length) {
                loadfill.style.width = `${(100 * done) / selected.length}%`;
                status.textContent = `Copied ${done} of ${selected.length} files…`;
            }
        }
        gameReady = true;
        status.textContent = `${selected.length} files ready. Press Start.`;
        startButton.disabled = false;
    };

    $('folder').addEventListener('change', (event) => {
        const files = [...event.target.files].map((file) => [file.webkitRelativePath || file.name, file]);
        load(files);
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
        load(await droppedFiles([...event.dataTransfer.items]));
    });

    startButton.addEventListener('click', async () => {
        if (!gameReady) {
            return;
        }
        const player = await ready;
        try {
            localStorage.setItem('nmo-player-language', language.value);
        } catch {
            /* the choice is only remembered where storage works */
        }
        document.body.classList.add('playing');
        status.textContent = 'Playing. The arrow keys roll the ball; Esc opens the game\'s menu.';
        const canvas = $('canvas');
        canvas.focus();
        const scale = window.innerWidth >= 1340 && window.innerHeight >= 1080 ? '2' : '1';
        const code = player.callMain([
            '--write', '/save',
            '--registry', `Software\\Ballance\\Settings\\Language=${language.value}`,
            '--scale', scale,
            '/game/base.cmo',
        ]);
        if (code !== 0) {
            status.textContent = 'The game could not start (see the log).';
            document.body.classList.remove('playing');
            return;
        }
        player._player_set_mute(muted ? 1 : 0);
        paused = false;
        playPause.disabled = false;
        muteButton.disabled = false;
        fullscreenButton.disabled = false;
        let frames = player._player_frames();
        fpsTimer = setInterval(() => {
            const now = player._player_frames();
            $('fps').textContent = paused ? 'paused' : `${now - frames} fps`;
            frames = now;
        }, 1000);
    });

    playPause.addEventListener('click', async () => {
        const player = await ready;
        paused = !paused;
        if (paused) {
            player._player_pause();
        } else {
            player._player_resume();
        }
        setHidden(playPause.querySelector('.icon-pause'), paused);
        setHidden(playPause.querySelector('.icon-play'), !paused);
    });

    /* A hidden tab throttles requestAnimationFrame to a few fps (or less),
       the main loop's own clock: rather than stumble along at that rate (or
       jump by the huge gap it leaves once visible again, which can make a
       physics step unstable), freeze the engine while hidden and resume
       where it left off. Tracked apart from `paused` so a user's own pause
       is not silently lifted just because the tab came back into view. */
    let autoPaused = false;
    document.addEventListener('visibilitychange', async () => {
        if (!document.body.classList.contains('playing')) {
            return;
        }
        const player = await ready;
        if (document.hidden) {
            if (!paused) {
                player._player_pause();
                autoPaused = true;
            }
        } else if (autoPaused) {
            player._player_resume();
            autoPaused = false;
        }
    });

    muteButton.addEventListener('click', async () => {
        const player = await ready;
        muted = !muted;
        player._player_set_mute(muted ? 1 : 0);
        setHidden(muteButton.querySelector('.icon-unmuted'), muted);
        setHidden(muteButton.querySelector('.icon-muted'), !muted);
        try {
            localStorage.setItem('nmo-player-muted', muted ? '1' : '0');
        } catch {
            /* the choice is only remembered where storage works */
        }
    });

    fullscreenButton.addEventListener('click', () => {
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

    /* For tests: load files fetched from a list of URLs, as a chosen folder would */
    globalThis.nmoPlayer = {
        loadFromUrls: async (base, paths) => {
            const files = [];
            for (const path of paths) {
                const response = await fetch(base + path.split('/').map(encodeURIComponent).join('/'));
                files.push([path, await response.blob()]);
            }
            await load(files);
        },
        ready,
    };
}

if (typeof document !== 'undefined') {
    startPage();
}
