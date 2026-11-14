// Which files make a game: the compositions it could start from, the folder
// it lives in, and the files of that folder it reads. Plain functions over
// relative paths ('/' separators, as a folder picker, a drop, a zip or
// Node.js's file listing gives them), shared by the page and run_node.mjs.

import { detectProfile, profileById, registryFor } from './profiles.mjs';

/* What a player can start: a composition (.cmo), a player-ready one (.vmo), or an object
   file (.nmo) */
export const ENTRY_EXTENSIONS = ['.cmo', '.vmo', '.nmo'];

/* What file systems and archivers leave behind, never a game's */
function isJunk(path) {
    return path
        .split('/')
        .some(
            (part) =>
                part === '__MACOSX' ||
                part === '.DS_Store' ||
                part === 'Thumbs.db' ||
                part === 'desktop.ini'
        );
}

function depth(path) {
    return path.split('/').length;
}

/* The compositions among `paths`, shallowest first, then .cmo before .vmo before .nmo, then by
   name */
export function findEntries(paths) {
    const rank = (path) =>
        ENTRY_EXTENSIONS.findIndex((extension) => path.toLowerCase().endsWith(extension));
    return paths
        .filter((path) => !isJunk(path) && rank(path) >= 0)
        .sort((a, b) => depth(a) - depth(b) || rank(a) - rank(b) || a.localeCompare(b));
}

/* The folder every path is in ('' when they share none) */
function commonFolder(paths) {
    if (paths.length === 0) {
        return '';
    }
    let parts = paths[0].split('/').slice(0, -1);
    for (const path of paths.slice(1)) {
        const other = path.split('/');
        let same = 0;
        while (same < parts.length && same < other.length - 1 && parts[same] === other[same]) {
            same++;
        }
        parts = parts.slice(0, same);
    }
    return parts.join('/');
}

/*
 * A game made of `paths`: its profile (`options.profile`, an id, or the one
 * that recognizes the files), the compositions it could start from, the one
 * it starts from (`options.entry`, the profile's choice, or the first), the
 * folder taken as the game's (the composition's for a known game, else the
 * folder all the files share) and its files as [source path, path inside
 * that folder]. Null when there is nothing to start.
 */
export function planGame(paths, options = {}) {
    const usable = paths.filter((path) => !isJunk(path));
    const profile = options.profile ? profileById(options.profile) : detectProfile(usable);
    const entries = findEntries(usable);
    if (entries.length === 0) {
        return null;
    }
    let entry = options.entry && entries.includes(options.entry) ? options.entry : null;
    entry ??= profile.findEntry(entries) ?? entries[0];
    /* A game its profile knows lives in its composition's folder */
    const known = profile.recognizes(usable);
    const root = known ? entry.split('/').slice(0, -1).join('/') : commonFolder(usable);
    const prefix = root === '' ? '' : root + '/';
    const files = [];
    for (const path of usable) {
        if (path.startsWith(prefix)) {
            const inner = path.slice(prefix.length);
            if (inner === entry.slice(prefix.length) || profile.keeps(inner)) {
                files.push([path, inner]);
            }
        }
    }
    const entryName = entry
        .split('/')
        .pop()
        .replace(/\.[^.]+$/, '');
    return {
        profile,
        entries,
        entry,
        inner: entry.slice(prefix.length),
        root,
        files,
        name: root.split('/').pop() || (known ? profile.name : entryName),
    };
}

/* Where the page keeps the game files of the run, and a game's saves */
export const GAME_FOLDER = '/game';
export const SAVES_FOLDER = '/save';

export function saveFolder(id) {
    return `${SAVES_FOLDER}/${id}`;
}

/*
 * The player's arguments for a game of `profile` starting from `entry`
 * (its path inside GAME_FOLDER): its registry values (`values`, the
 * profile's options, then `registry`, [key, name, value] entries), where
 * it saves (`write`), and `more` of the player's options before the file.
 */
export function playerArguments({
    profile,
    entry,
    values = {},
    registry = [],
    write = null,
    more = [],
}) {
    const args = ['--path', GAME_FOLDER];
    if (write) {
        args.push('--write', write);
    }
    const text = registryFor(profile, values, registry);
    if (text) {
        args.push('--registry', text);
    }
    return [...args, ...more, `${GAME_FOLDER}/${entry}`];
}

/*
 * Saves the page kept straight in SAVES_FOLDER before it kept each game's
 * apart: whichever game is played first that has files by those names (a
 * save is a game file written over, read first) takes them into `folder`.
 */
export function adoptLooseSaves(module, folder) {
    if (module.FS.analyzePath(folder).exists) {
        return false;
    }
    const gameNames = new Set(module.FS.readdir(GAME_FOLDER).map((name) => name.toLowerCase()));
    const loose = module.FS.readdir(SAVES_FOLDER).filter(
        (name) => name !== '.' && name !== '..' && gameNames.has(name.toLowerCase())
    );
    module.FS.mkdir(folder);
    for (const name of loose) {
        module.FS.rename(`${SAVES_FOLDER}/${name}`, `${folder}/${name}`);
    }
    return loose.length > 0;
}

/* Write `data` (bytes) at `path` in the module's file system, making its folders */
export function writeGameFile(module, path, data) {
    const slash = path.lastIndexOf('/');
    if (slash > 0) {
        module.FS.mkdirTree(path.slice(0, slash));
    }
    module.FS.writeFile(path, data);
}

/* Remove `path` and everything under it from the module's file system */
export function removeTree(module, path) {
    let stat;
    try {
        stat = module.FS.stat(path);
    } catch {
        return;
    }
    if (module.FS.isDir(stat.mode)) {
        for (const name of module.FS.readdir(path)) {
            if (name !== '.' && name !== '..') {
                removeTree(module, `${path}/${name}`);
            }
        }
        module.FS.rmdir(path);
    } else {
        module.FS.unlink(path);
    }
}

/* Every file under `path` in the module's file system, as [path inside it, size, time] */
export function listTree(module, path, prefix = '') {
    const found = [];
    let names;
    try {
        names = module.FS.readdir(path);
    } catch {
        return found;
    }
    for (const name of names) {
        if (name === '.' || name === '..') {
            continue;
        }
        const full = `${path}/${name}`;
        const stat = module.FS.stat(full);
        if (module.FS.isDir(stat.mode)) {
            found.push(...listTree(module, full, prefix + name + '/'));
        } else {
            found.push([prefix + name, stat.size, stat.mtime]);
        }
    }
    return found;
}
