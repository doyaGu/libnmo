// The Settings dialog: the player's own settings, and those of one game
// (its profile's options, the screen size it starts at, its keys, its
// gamepad buttons and the registry values it reads).
//
// A game's settings, as its library meta keeps them: {values (the
// profile's options, by setting id), size ('WxH', or '' for the size it
// starts at by itself), keys ([player's key, game key] pairs), pad
// ({button index: key code, '' for none}, over the profile's), registry
// ([key, name, value] entries)}.

import { element, smallButton } from './dom.mjs';
import { keyName } from './input.mjs';
import { profileById } from './profiles.mjs';

/* The standard mapping's buttons, but the D-pad (the arrows, or the joystick's POV hat) */
const PAD_BUTTONS = [
    [0, 'A / Cross'],
    [1, 'B / Circle'],
    [2, 'X / Square'],
    [3, 'Y / Triangle'],
    [4, 'Left shoulder'],
    [5, 'Right shoulder'],
    [6, 'Left trigger'],
    [7, 'Right trigger'],
    [8, 'Back / Select'],
    [9, 'Start'],
    [10, 'Left stick'],
    [11, 'Right stick'],
];

/* The player's own settings, and what they are until changed */
export const DEFAULT_SETTINGS = {
    renderer: 'auto',
    scale: 'auto',
    physics: true,
    frameLimit: true,
    gamepad: true,
    pointerLock: false,
    overlay: false,
    volume: 1,
    muted: false,
    devtools: false,
};

/* A game's settings with every part there */
export function gameSettings(game) {
    const own = game.settings ?? {};
    return {
        values: own.values ?? {},
        size: own.size ?? '',
        keys: own.keys ?? [],
        pad: own.pad ?? {},
        registry: own.registry ?? [],
    };
}

/* The keys a gamepad presses for a game: its profile's, then the game's own */
export function gamepadMapping(game) {
    const profile = profileById(game.profile);
    return {
        stick: profile.gamepad.stick,
        buttons: { ...profile.gamepad.buttons, ...gameSettings(game).pad },
    };
}

/*
 * `settings` is the player's (changed in place when saved); `getLibrary()`
 * the games' library; `screenSizes()` the 'WxH' sizes a game can start at;
 * `onSave(game, restart)` is told what was saved: the game's new meta
 * (null when the dialog showed none) and whether to restart the game
 * playing.
 */
export function createSettingsDialog({ dialog, settings, getLibrary, screenSizes, onSave }) {
    const $ = (id) => document.getElementById(id);
    let editing = null; /* the game whose settings the dialog shows */
    let draft = null; /* its settings as the dialog has them */
    let capturing =
        null; /* what waits for a key: {list: 'keys', row, side} or {list: 'pad', button} */

    const isCapturing = (target) =>
        capturing !== null &&
        Object.entries(target).every(([key, value]) => capturing[key] === value);

    function capture(target) {
        capturing = target;
        drawKeys();
        drawPad();
    }

    function keyButton(target, code, none) {
        const text = isCapturing(target) ? 'Press a key…' : code ? keyName(code) : none;
        return smallButton(text, () => capture(target), 'key');
    }

    function drawKeys() {
        const table = $('keymap');
        if (draft.keys.length === 0) {
            const cell = element('td', {
                className: 'dim',
                textContent: 'The game gets the keys you press, as they are.',
            });
            table.replaceChildren(element('tr', {}, [cell]));
            return;
        }
        table.replaceChildren(
            ...draft.keys.map(([from, to], row) =>
                element('tr', {}, [
                    element('td', {}, [keyButton({ list: 'keys', row, side: 'from' }, from, '?')]),
                    element('td', { className: 'dim', textContent: '→' }),
                    element('td', {}, [keyButton({ list: 'keys', row, side: 'to' }, to, '?')]),
                    element('td', {}, [
                        smallButton('Remove', () => {
                            draft.keys.splice(row, 1);
                            capture(null);
                        }),
                    ]),
                ])
            )
        );
    }

    function drawPad() {
        const defaults = profileById(editing.profile).gamepad.buttons;
        $('padmap').replaceChildren(
            ...PAD_BUTTONS.map(([button, label]) => {
                const own = Object.hasOwn(draft.pad, button);
                const code = own ? draft.pad[button] : defaults[button];
                const actions = [keyButton({ list: 'pad', button }, code, 'Nothing')];
                if (code) {
                    actions.push(
                        smallButton('Clear', () => {
                            draft.pad[button] = '';
                            capture(null);
                        })
                    );
                }
                if (own) {
                    actions.push(
                        smallButton('Default', () => {
                            delete draft.pad[button];
                            capture(null);
                        })
                    );
                }
                return element('tr', {}, [
                    element('td', { textContent: label }),
                    element('td', {}, actions),
                ]);
            })
        );
    }

    function drawRegistry() {
        const table = $('registry');
        if (draft.registry.length === 0) {
            table.replaceChildren(
                element('tr', {}, [element('td', { className: 'dim', textContent: 'None added.' })])
            );
            return;
        }
        const fields = [
            ['Key', 'Software\\Company\\Game'],
            ['Name', 'Setting'],
            ['Value', '1'],
        ];
        table.replaceChildren(
            ...draft.registry.map((entry, row) =>
                element('tr', {}, [
                    ...fields.map(([label, placeholder], column) => {
                        const input = element('input', {
                            value: entry[column] ?? '',
                            placeholder,
                            spellcheck: false,
                            'aria-label': label,
                        });
                        input.addEventListener('input', () => {
                            entry[column] = input.value;
                        });
                        return element('td', {}, [input]);
                    }),
                    element('td', {}, [
                        smallButton('Remove', () => {
                            draft.registry.splice(row, 1);
                            drawRegistry();
                        }),
                    ]),
                ])
            )
        );
    }

    function drawGame() {
        const profile = profileById(editing.profile);
        const own = gameSettings(editing);
        draft = {
            keys: own.keys.map((pair) => [...pair]),
            pad: { ...own.pad },
            registry: own.registry.map((entry) => [...entry]),
        };
        $('game-settings-title').textContent =
            editing.name === profile.name ? editing.name : `${editing.name} (${profile.name})`;
        $('profile-options').hidden = profile.settings.length === 0;
        $('profile-options-title').textContent = `${profile.name} options`;
        $('profile-settings').replaceChildren(
            ...profile.settings.map((setting) => {
                const select = element(
                    'select',
                    {},
                    setting.options.map(([value, text]) =>
                        element('option', { value, textContent: text })
                    )
                );
                select.dataset.setting = setting.id;
                select.value = own.values[setting.id] ?? setting.default;
                return element('label', {}, [setting.label, ' ', select]);
            })
        );
        const size = $('game-size');
        size.replaceChildren(
            element('option', { value: '', textContent: 'As the game starts' }),
            ...screenSizes().map((value) =>
                element('option', { value, textContent: value.replace('x', ' × ') })
            )
        );
        size.value = own.size;
        drawKeys();
        drawPad();
        drawRegistry();
    }

    $('keymap-add').addEventListener('click', () => {
        draft.keys.push(['', '']);
        capture({ list: 'keys', row: draft.keys.length - 1, side: 'from' });
    });
    $('registry-add').addEventListener('click', () => {
        draft.registry.push(['', '', '']);
        drawRegistry();
        $('registry').querySelector('tr:last-child input')?.focus();
    });

    /* The key the player presses while a key button waits for one; Esc gives
       up (and drops a new key row left half chosen) */
    window.addEventListener(
        'keydown',
        (event) => {
            if (capturing === null || !dialog.open) {
                return;
            }
            event.preventDefault();
            event.stopImmediatePropagation();
            const target = capturing;
            capturing = null;
            const escape = event.code === 'Escape';
            if (target.list === 'pad') {
                if (!escape) {
                    draft.pad[target.button] = event.code;
                }
            } else {
                const pair = draft.keys[target.row];
                if (escape && (!pair[0] || !pair[1])) {
                    draft.keys.splice(target.row, 1);
                } else if (!escape) {
                    pair[target.side === 'from' ? 0 : 1] = event.code;
                    if (target.side === 'from' && !pair[1]) {
                        capturing = { list: 'keys', row: target.row, side: 'to' };
                    }
                }
            }
            drawKeys();
            drawPad();
        },
        true
    );

    /* Done or Save and restart (the form's submit, before the dialog closes;
       Esc closes it without one and keeps everything as it was) */
    dialog.querySelector('form').addEventListener('submit', async (event) => {
        const choice = event.submitter?.value;
        capturing = null;
        if (choice !== 'save' && choice !== 'restart') {
            return;
        }
        Object.assign(settings, {
            renderer: $('opt-renderer').value,
            scale: $('opt-scale').value,
            physics: $('opt-physics').checked,
            frameLimit: $('opt-frame-limit').checked,
            gamepad: $('opt-gamepad').checked,
            pointerLock: $('opt-pointer-lock').checked,
            overlay: $('opt-overlay').checked,
        });
        let saved = null;
        if (editing) {
            const values = {};
            for (const select of $('profile-settings').querySelectorAll('select')) {
                values[select.dataset.setting] = select.value;
            }
            const registry = draft.registry
                .map((entry) => entry.map((part) => part.trim()))
                .filter(([key, name]) => key && name);
            saved = await getLibrary().update(editing.id, {
                settings: {
                    values,
                    size: $('game-size').value,
                    keys: draft.keys.filter(([from, to]) => from && to),
                    pad: draft.pad,
                    registry,
                },
            });
        }
        onSave(saved, choice === 'restart');
    });

    return {
        /* Show the player's settings, and `game`'s (null for none); `running` says whether
           a game plays (which Save and restart restarts) */
        open(game, running) {
            editing = game ?? null;
            capturing = null;
            $('game-settings').hidden = editing === null;
            if (editing) {
                drawGame();
            }
            $('opt-renderer').value = settings.renderer;
            $('opt-scale').value = settings.scale;
            $('opt-physics').checked = settings.physics;
            $('opt-frame-limit').checked = settings.frameLimit;
            $('opt-gamepad').checked = settings.gamepad;
            $('opt-pointer-lock').checked = settings.pointerLock;
            $('opt-overlay').checked = settings.overlay;
            $('settings-restart').hidden = !running;
            dialog.showModal();
        },
    };
}
