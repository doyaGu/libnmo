// The games the player has been given, kept between visits in the
// browser's own private file system (the origin private file system, OPFS):
// each game a folder of its files and a meta.json saying what it is. A
// browser without OPFS (or without writable OPFS files) gets a library that
// lasts as long as the tab, holding the chosen files themselves.
//
// A game's meta: {id, name, entry (path inside the game), profile (id),
// size, count, added, played, source (the URL it came from, if any),
// settings: {values, keymap, registry}}.

const GAMES = 'games';
const FILES = 'files';
const META = 'meta.json';

function newId() {
    return globalThis.crypto?.randomUUID?.() ?? `${Date.now().toString(36)}-${Math.random().toString(36).slice(2)}`;
}

/* The folder handle at `parts` under `root`, made when `create` */
async function folderAt(root, parts, create) {
    let folder = root;
    for (const part of parts) {
        folder = await folder.getDirectoryHandle(part, {create});
    }
    return folder;
}

/* Every file under `folder`, as [path inside it, FileSystemFileHandle] */
async function walk(folder, prefix = '') {
    const found = [];
    for await (const [name, handle] of folder.entries()) {
        if (handle.kind === 'directory') {
            found.push(...await walk(handle, prefix + name + '/'));
        } else {
            found.push([prefix + name, handle]);
        }
    }
    return found;
}

class FolderLibrary {
    constructor(root) {
        this.root = root;
        this.persistent = true;
    }

    async list() {
        const games = [];
        const folder = await this.root.getDirectoryHandle(GAMES, {create: true});
        for await (const [id, handle] of folder.entries()) {
            if (handle.kind !== 'directory') {
                continue;
            }
            try {
                const file = await (await handle.getFileHandle(META)).getFile();
                games.push(JSON.parse(await file.text()));
            } catch {
                /* a game whose copy never finished: no meta, so not listed (removed on the next add) */
            }
        }
        return games.sort((a, b) => (b.played ?? b.added) - (a.played ?? a.added));
    }

    async get(id) {
        return (await this.list()).find((game) => game.id === id) ?? null;
    }

    /* Copy `files` ([path inside the game, file-like with arrayBuffer()]) in; onProgress(done, total) */
    async add(meta, files, onProgress) {
        await this.sweep();
        const id = newId();
        const games = await this.root.getDirectoryHandle(GAMES, {create: true});
        const folder = await games.getDirectoryHandle(id, {create: true});
        const filesFolder = await folder.getDirectoryHandle(FILES, {create: true});
        let size = 0;
        let done = 0;
        try {
            for (const [inner, source] of files) {
                const parts = inner.split('/');
                const parent = await folderAt(filesFolder, parts.slice(0, -1), true);
                const handle = await parent.getFileHandle(parts[parts.length - 1], {create: true});
                const data = await source.arrayBuffer();
                const writable = await handle.createWritable();
                await writable.write(data);
                await writable.close();
                size += data.byteLength;
                onProgress?.(++done, files.length);
            }
        } catch (error) {
            await games.removeEntry(id, {recursive: true}).catch(() => {});
            throw error;
        }
        const game = {...meta, id, size, count: files.length, added: Date.now(), played: null};
        await this.writeMeta(folder, game);
        return game;
    }

    async update(id, patch) {
        const folder = await folderAt(this.root, [GAMES, id], false);
        const game = {...await this.get(id), ...patch, id};
        await this.writeMeta(folder, game);
        return game;
    }

    async writeMeta(folder, game) {
        const writable = await (await folder.getFileHandle(META, {create: true})).createWritable();
        await writable.write(JSON.stringify(game));
        await writable.close();
    }

    /* The game's files, as [path inside the game, file-like with arrayBuffer()] */
    async files(id) {
        const folder = await folderAt(this.root, [GAMES, id, FILES], false);
        return (await walk(folder)).map(([inner, handle]) => [inner, {
            arrayBuffer: async () => (await handle.getFile()).arrayBuffer(),
        }]);
    }

    async remove(id) {
        const games = await this.root.getDirectoryHandle(GAMES, {create: true});
        await games.removeEntry(id, {recursive: true});
    }

    /* Games left without a meta by an add that never finished (a closed tab) */
    async sweep() {
        const games = await this.root.getDirectoryHandle(GAMES, {create: true});
        const stale = [];
        for await (const [id, handle] of games.entries()) {
            if (handle.kind === 'directory') {
                try {
                    await handle.getFileHandle(META);
                } catch {
                    stale.push(id);
                }
            }
        }
        for (const id of stale) {
            await games.removeEntry(id, {recursive: true}).catch(() => {});
        }
    }
}

class MemoryLibrary {
    constructor() {
        this.games = new Map();
        this.persistent = false;
    }

    async list() {
        return [...this.games.values()].map(({meta}) => meta)
            .sort((a, b) => (b.played ?? b.added) - (a.played ?? a.added));
    }

    async get(id) {
        return this.games.get(id)?.meta ?? null;
    }

    async add(meta, files, onProgress) {
        const id = newId();
        let size = 0;
        for (const [, source] of files) {
            size += source.size ?? 0;
        }
        const game = {...meta, id, size, count: files.length, added: Date.now(), played: null};
        this.games.set(id, {meta: game, files});
        onProgress?.(files.length, files.length);
        return game;
    }

    async update(id, patch) {
        const entry = this.games.get(id);
        entry.meta = {...entry.meta, ...patch, id};
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
        const handle = await root.getFileHandle('.probe', {create: true});
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
        if (root && await canWrite(root)) {
            return new FolderLibrary(root);
        }
    } catch {
        /* no OPFS here (or not in this kind of window): the tab's memory instead */
    }
    return new MemoryLibrary();
}
