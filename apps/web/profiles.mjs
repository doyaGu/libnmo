// What the player knows about particular games: a profile says how to find
// a game's composition among the files it is given, which of them the game
// reads, which registry settings it offers, which keys it plays with (for
// remapping, a gamepad and the touch screen buttons) and a line on how to
// play. Any Virtools game runs with the generic profile; the others only
// make a known game nicer to start.

/* Keys a game commonly plays with, by KeyboardEvent.code */
const ARROWS = ['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight'];

/* The generic gamepad: stick and D-pad steer with the arrows; buttons in the
   standard mapping's order (0 A/cross, 1 B/circle, 2 X/square, 3 Y/triangle,
   4/5 shoulders, 8 back/select, 9 start) */
const GAMEPAD_KEYS = {0: 'Enter', 1: 'Escape', 2: 'Space', 3: 'ShiftLeft', 9: 'Escape'};

export const GENERIC = {
    id: 'generic',
    name: 'Virtools game',
    keys: [...ARROWS, 'Enter', 'Escape', 'Space', 'ShiftLeft', 'ControlLeft'],
    touch: {dpad: true, buttons: ['Enter', 'Escape', 'Space', 'ShiftLeft']},
    gamepad: {stick: 'arrows', buttons: GAMEPAD_KEYS},
    settings: [],
    hint: 'The game reads the keyboard and the mouse; a gamepad works as a joystick and as the arrow keys.',
    /* No guess at the composition: the shortest path among them */
    entry: null,
    /* Every file but the Windows player's own programs */
    keep: (inner) => !/\.(exe|dll|pdb)$/i.test(inner),
};

/* The parts of a Ballance folder the game reads, by their first path part (any case) */
const BALLANCE_PARTS = ['base.cmo', 'database.tdb', '3d entities', 'textures', 'sounds', 'text'];

export const BALLANCE = {
    id: 'ballance',
    name: 'Ballance',
    keys: [...ARROWS, 'ShiftLeft', 'Space', 'Enter', 'Escape'],
    touch: {dpad: true, buttons: ['Enter', 'Escape', 'Space', 'ShiftLeft']},
    gamepad: {stick: 'arrows', buttons: {0: 'Enter', 1: 'Escape', 2: 'Space', 3: 'ShiftLeft', 4: 'ShiftLeft', 5: 'ShiftLeft', 9: 'Escape'}},
    settings: [{
        id: 'language',
        label: 'Language',
        registry: 'Software\\Ballance\\Settings\\Language',
        options: [['0', 'Deutsch'], ['1', 'English'], ['2', 'Español'], ['3', 'Italiano'], ['4', 'Français']],
        default: '1',
    }],
    hint: 'The arrow keys roll the ball, Shift with an arrow turns the camera, Space looks from above, Esc opens the menu.',
    /* The folder holding base.cmo and Database.tdb is the game's */
    detect: (paths) => {
        const names = new Set(paths.map((path) => path.split('/').pop().toLowerCase()));
        return names.has('base.cmo') && names.has('database.tdb');
    },
    entry: (candidates) => candidates.find((path) => path.split('/').pop().toLowerCase() === 'base.cmo') ?? null,
    keep: (inner) => BALLANCE_PARTS.includes(inner.split('/')[0].toLowerCase()),
};

export const PROFILES = [BALLANCE, GENERIC];

export function profileById(id) {
    return PROFILES.find((profile) => profile.id === id) ?? GENERIC;
}

/* The profile for a game made of `paths`: the first that knows it, else the generic one */
export function detectProfile(paths) {
    return PROFILES.find((profile) => profile.detect?.(paths)) ?? GENERIC;
}

/* The registry values a game starts with: its profile's settings as chosen
   (`values`, by setting id), then the extra KEY\NAME=VALUE lines */
export function registryFor(profile, values = {}, extra = '') {
    const entries = profile.settings
        .filter((setting) => setting.registry)
        .map((setting) => `${setting.registry}=${values[setting.id] ?? setting.default}`);
    for (const line of extra.split('\n')) {
        const entry = line.trim();
        if (entry !== '' && !entry.startsWith('#')) {
            entries.push(entry);
        }
    }
    return entries.join(';');
}
