// Game profiles: what the player knows about particular games, as data in
// profiles.json next to this module. A profile says how to recognize a game
// among the files it is given ("match": base names that must all be there,
// any case), which composition it starts from ("entry", a base name), which
// of its files the game reads ("files": "include" the first path parts, or
// "exclude" extensions), its own options ("settings", each a registry value
// with a few choices), its "touch" buttons, the keys a gamepad presses by
// default ("gamepad") and a "hint" on how to play. A profile without
// "match" never recognizes a game; the one with id "generic" is what any
// other game runs with. Key remapping and registry values a player adds
// themselves are the game's own settings, not the profile's.

/* What runs a game when profiles.json could not be read */
const FALLBACK = {
    id: 'generic',
    name: 'Virtools game',
    touch: {dpad: true, buttons: ['Enter', 'Escape', 'Space']},
    gamepad: {stick: 'arrows', buttons: {}},
    settings: [],
    hint: '',
};

function baseName(path) {
    return path.split('/').pop().toLowerCase();
}

/*
 * A profile from its JSON: the data as it is, with functions for what
 * games.mjs asks of it: detect(paths), entry(candidates) and keep(inner).
 */
function makeProfile(data) {
    const profile = {...FALLBACK, ...data};
    const required = (data.match?.files ?? []).map((name) => name.toLowerCase());
    profile.detect = required.length === 0 ? null : (paths) => {
        const names = new Set(paths.map(baseName));
        return required.every((name) => names.has(name));
    };
    const entry = data.entry?.toLowerCase();
    profile.entry = entry ? (candidates) => candidates.find((path) => baseName(path) === entry) ?? null : null;
    const include = data.files?.include?.map((part) => part.toLowerCase());
    const exclude = data.files?.exclude?.map((extension) => extension.toLowerCase()) ?? [];
    profile.keep = include
        ? (inner) => include.includes(inner.split('/')[0].toLowerCase())
        : (inner) => !exclude.some((extension) => inner.toLowerCase().endsWith(extension));
    return profile;
}

export let PROFILES = [makeProfile(FALLBACK)];
export let GENERIC = PROFILES[0];

/* Use the profiles in `data` (profiles.json's contents) */
export function setProfiles(data) {
    PROFILES = (data?.profiles ?? []).map(makeProfile);
    GENERIC = PROFILES.find((profile) => profile.id === 'generic') ?? makeProfile(FALLBACK);
    if (!PROFILES.includes(GENERIC)) {
        PROFILES.push(GENERIC);
    }
}

/* Read profiles.json, from the page's server or (in Node.js) its folder */
export async function loadProfiles() {
    const url = new URL('./profiles.json', import.meta.url);
    const text = url.protocol === 'file:'
        ? await (await import('node:fs/promises')).readFile(url, 'utf8')
        : await (await fetch(url)).text();
    setProfiles(JSON.parse(text));
}

export function profileById(id) {
    return PROFILES.find((profile) => profile.id === id) ?? GENERIC;
}

/* The profile for a game made of `paths`: the first that knows it, else the generic one */
export function detectProfile(paths) {
    return PROFILES.find((profile) => profile.detect?.(paths)) ?? GENERIC;
}

/* The registry values a game starts with, as the player's --registry takes
   them: its profile's options as chosen (`values`, by setting id), then
   `extra` [key, name, value] entries */
export function registryFor(profile, values = {}, extra = []) {
    const entries = profile.settings
        .filter((setting) => setting.registry)
        .map((setting) => `${setting.registry}=${values[setting.id] ?? setting.default}`);
    for (const [key, name, value] of extra) {
        if (key && name) {
            entries.push(`${key}\\${name}=${value ?? ''}`);
        }
    }
    return entries.join(';');
}
