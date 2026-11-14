// The games the player has been given, kept between visits: in the
// browser's own private file system (OPFS) where the page can write files
// there, else in IndexedDB, else (neither works, as in some private
// windows) for as long as the tab is open.
//
// A game's meta: {id, name, entry (path inside the game), profile (id),
// size, count, added, played, source (the URL it came from, if any),
// settings: {values, keys, pad, registry}} (see player.mjs's settings).

const GAMES = 'games';
const FILES = 'files';
const META = 'meta.json';
const ADD_LOCK = 'nmo-library-add-';

function newId() {
    if (globalThis.crypto?.randomUUID) {
        return globalThis.crypto.randomUUID();
    }
    return `${Date.now().toString(36)}-${Math.random().toString(36).slice(2)}`;
}

/* Most recently played (or added) first */
function byRecent(a, b) {
    return (b.played ?? b.added) - (a.played ?? a.added);
}

function newGame(meta, id, size, count) {
    return { ...meta, id, size, count, added: Date.now(), played: null };
}

/* Run `task` holding the lock that says game `id` is being added, so
   another tab's sweep leaves its unfinished files alone */
function whileAdding(id, task) {
    return globalThis.navigator?.locks?.request
        ? navigator.locks.request(ADD_LOCK + id, task)
        : task();
}

/* The games other tabs are adding now; null when the browser can't say */
async function gamesBeingAdded() {
    try {
        const state = await navigator.locks.query();
        return new Set(
            state.held
                .map((lock) => lock.name)
                .filter((name) => name.startsWith(ADD_LOCK))
                .map((name) => name.slice(ADD_LOCK.length))
        );
    } catch {
        return null;
    }
}

/* The folder handle at `parts` under `root`, made when `create` */
async function folderAt(root, parts, create) {
    let folder = root;
    for (const part of parts) {
        folder = await folder.getDirectoryHandle(part, { create });
    }
    return folder;
}

/* Every file under `folder`, as [path inside it, FileSystemFileHandle] */
async function walk(folder, prefix = '') {
    const found = [];
    for await (const [name, handle] of folder.entries()) {
        if (handle.kind === 'directory') {
            found.push(...(await walk(handle, prefix + name + '/')));
        } else {
            found.push([prefix + name, handle]);
        }
    }
    return found;
}

async function writeFile(folder, name, data) {
    const handle = await folder.getFileHandle(name, { create: true });
    const writable = await handle.createWritable();
    await writable.write(data);
    await writable.close();
}

class FolderLibrary {
    constructor(root) {
        this.root = root;
        this.persistent = true;
    }

    async games() {
        return this.root.getDirectoryHandle(GAMES, { create: true });
    }

    async list() {
        const games = [];
        for await (const [, handle] of (await this.games()).entries()) {
            if (handle.kind !== 'directory') {
                continue;
            }
            try {
                const file = await (await handle.getFileHandle(META)).getFile();
                games.push(JSON.parse(await file.text()));
            } catch {
                /* a game still being copied in (or whose copy never finished): not listed */
            }
        }
        return games.sort(byRecent);
    }

    async get(id) {
        return (await this.list()).find((game) => game.id === id) ?? null;
    }

    /* Copy `files` ([path inside the game, file-like with arrayBuffer()]) in;
       onProgress(done, total) */
    async add(meta, files, onProgress) {
        await this.sweep();
        const id = newId();
        return whileAdding(id, async () => {
            const games = await this.games();
            const folder = await games.getDirectoryHandle(id, { create: true });
            const filesFolder = await folder.getDirectoryHandle(FILES, { create: true });
            let size = 0;
            let done = 0;
            try {
                for (const [inner, source] of files) {
                    const parts = inner.split('/');
                    const parent = await folderAt(filesFolder, parts.slice(0, -1), true);
                    const data = await source.arrayBuffer();
                    await writeFile(parent, parts[parts.length - 1], data);
                    size += data.byteLength;
                    onProgress?.(++done, files.length);
                }
            } catch (error) {
                await games.removeEntry(id, { recursive: true }).catch(() => {});
                throw error;
            }
            const game = newGame(meta, id, size, files.length);
            await writeFile(folder, META, JSON.stringify(game));
            return game;
        });
    }

    async update(id, patch) {
        const game = { ...(await this.get(id)), ...patch, id };
        await writeFile(await folderAt(this.root, [GAMES, id], false), META, JSON.stringify(game));
        return game;
    }

    /* The game's files, as [path inside the game, file-like with arrayBuffer()] */
    async files(id) {
        const folder = await folderAt(this.root, [GAMES, id, FILES], false);
        return (await walk(folder)).map(([inner, handle]) => [
            inner,
            {
                arrayBuffer: async () => (await handle.getFile()).arrayBuffer(),
            },
        ]);
    }

    async remove(id) {
        await (await this.games()).removeEntry(id, { recursive: true });
    }

    /* Remove the games an add left without a meta (a tab closed while copying), but not those
       another tab is still adding; nothing when the browser can't tell which those are */
    async sweep() {
        const busy = await gamesBeingAdded();
        if (busy === null) {
            return;
        }
        const games = await this.games();
        const stale = [];
        for await (const [id, handle] of games.entries()) {
            if (handle.kind !== 'directory' || busy.has(id)) {
                continue;
            }
            try {
                await handle.getFileHandle(META);
            } catch {
                stale.push(id);
            }
        }
        for (const id of stale) {
            await games.removeEntry(id, { recursive: true }).catch(() => {});
        }
    }
}

/* An IndexedDB request (or transaction) as a promise */
function settled(request) {
    return new Promise((resolve, reject) => {
        if ('oncomplete' in request) {
            request.oncomplete = () => resolve();
        } else {
            request.onsuccess = () => resolve(request.result);
        }
        request.onerror = () => reject(request.error);
        request.onabort = () => reject(request.error);
    });
}

/* IndexedDB: a store of game metas by id, and one of file bytes by [id, path] */
class DatabaseLibrary {
    constructor(db) {
        this.db = db;
        this.persistent = true;
    }

    static async open() {
        const request = indexedDB.open('nmo-player', 1);
        request.onupgradeneeded = () => {
            request.result.createObjectStore(GAMES, { keyPath: 'id' });
            request.result.createObjectStore(FILES);
        };
        return new DatabaseLibrary(await settled(request));
    }

    store(name, mode = 'readonly') {
        return this.db.transaction(name, mode).objectStore(name);
    }

    filesOf(id) {
        return IDBKeyRange.bound([id, ''], [id, String.fromCharCode(0xffff)]);
    }

    async list() {
        return (await settled(this.store(GAMES).getAll())).sort(byRecent);
    }

    async get(id) {
        return (await settled(this.store(GAMES).get(id))) ?? null;
    }

    async add(meta, files, onProgress) {
        await this.sweep();
        const id = newId();
        return whileAdding(id, async () => {
            let size = 0;
            let done = 0;
            try {
                for (const [inner, source] of files) {
                    const data = await source.arrayBuffer();
                    const bytes = data instanceof ArrayBuffer ? data : data.slice().buffer;
                    await settled(this.store(FILES, 'readwrite').put(bytes, [id, inner]));
                    size += bytes.byteLength;
                    onProgress?.(++done, files.length);
                }
            } catch (error) {
                await this.removeFiles(id).catch(() => {});
                throw error;
            }
            const game = newGame(meta, id, size, files.length);
            await settled(this.store(GAMES, 'readwrite').put(game));
            return game;
        });
    }

    async update(id, patch) {
        const game = { ...(await this.get(id)), ...patch, id };
        await settled(this.store(GAMES, 'readwrite').put(game));
        return game;
    }

    async files(id) {
        const keys = await settled(this.store(FILES).getAllKeys(this.filesOf(id)));
        return keys.map((key) => [
            key[1],
            {
                arrayBuffer: async () => settled(this.store(FILES).get(key)),
            },
        ]);
    }

    async removeFiles(id) {
        await settled(this.store(FILES, 'readwrite').delete(this.filesOf(id)));
    }

    async remove(id) {
        await settled(this.store(GAMES, 'readwrite').delete(id));
        await this.removeFiles(id);
    }

    /* Files an add left without a meta, but not those of a game another tab is adding */
    async sweep() {
        const busy = await gamesBeingAdded();
        if (busy === null) {
            return;
        }
        const listed = new Set((await this.list()).map((game) => game.id));
        const keys = await settled(this.store(FILES).getAllKeys());
        const stale = new Set(
            keys.map((key) => key[0]).filter((id) => !listed.has(id) && !busy.has(id))
        );
        for (const id of stale) {
            await this.removeFiles(id);
        }
    }
}

class MemoryLibrary {
    constructor() {
        this.games = new Map();
        this.persistent = false;
    }

    async list() {
        return [...this.games.values()].map(({ meta }) => meta).sort(byRecent);
    }

    async get(id) {
        return this.games.get(id)?.meta ?? null;
    }

    async add(meta, files, onProgress) {
        const id = newId();
        const size = files.reduce((sum, [, source]) => sum + (source.size ?? 0), 0);
        const game = newGame(meta, id, size, files.length);
        this.games.set(id, { meta: game, files });
        onProgress?.(files.length, files.length);
        return game;
    }

    async update(id, patch) {
        const entry = this.games.get(id);
        entry.meta = { ...entry.meta, ...patch, id };
        return entry.meta;
    }

    async files(id) {
        return this.games.get(id)?.files ?? [];
    }

    async remove(id) {
        this.games.delete(id);
    }
}

/* Whether this browser can write files in OPFS from the page (not only from a worker) */
async function canWrite(root) {
    try {
        const handle = await root.getFileHandle('.probe', { create: true });
        if (typeof handle.createWritable !== 'function') {
            return false;
        }
        const writable = await handle.createWritable();
        await writable.close();
        await root.removeEntry('.probe');
        return true;
    } catch {
        return false;
    }
}

export async function openLibrary() {
    try {
        const root = await navigator.storage?.getDirectory?.();
        if (root && (await canWrite(root))) {
            return new FolderLibrary(root);
        }
    } catch {
        /* no OPFS here (or not in this kind of window) */
    }
    try {
        if (globalThis.indexedDB) {
            return await DatabaseLibrary.open();
        }
    } catch {
        /* no IndexedDB either (a private window can refuse it) */
    }
    return new MemoryLibrary();
}
