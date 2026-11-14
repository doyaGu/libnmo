// Input the page adds to the keyboard and mouse SDL already reads: keys
// remapped to others, a gamepad (as the engine's joystick and as keys), and
// touch screen buttons. Keys reach the game as real keydown/keyup events
// SDL reads like any other: the view re-syncs the arrow keys from SDL's
// tracked keyboard state every frame, so a side channel straight into the
// engine would be overridden.

/* keyCode for the codes the page sends (SDL maps either) */
const KEY_CODES = {
    ArrowUp: 38,
    ArrowDown: 40,
    ArrowLeft: 37,
    ArrowRight: 39,
    Enter: 13,
    Escape: 27,
    Space: 32,
    Tab: 9,
    Backspace: 8,
    ShiftLeft: 16,
    ShiftRight: 16,
    ControlLeft: 17,
    ControlRight: 17,
    AltLeft: 18,
    AltRight: 18,
};

const KEY_NAMES = {
    ArrowUp: '↑',
    ArrowDown: '↓',
    ArrowLeft: '←',
    ArrowRight: '→',
    Space: 'Space',
    Escape: 'Esc',
    Enter: 'Enter',
    ShiftLeft: 'Shift',
    ShiftRight: 'Right Shift',
    ControlLeft: 'Ctrl',
    ControlRight: 'Right Ctrl',
    AltLeft: 'Alt',
    AltRight: 'Right Alt',
};

function keyCodeOf(code) {
    if (code in KEY_CODES) {
        return KEY_CODES[code];
    }
    let match = /^Key([A-Z])$/.exec(code);
    if (match) {
        return match[1].charCodeAt(0);
    }
    match = /^Digit([0-9])$/.exec(code);
    if (match) {
        return 48 + Number(match[1]);
    }
    match = /^F([0-9]{1,2})$/.exec(code);
    if (match) {
        return 111 + Number(match[1]);
    }
    return 0;
}

function keyOf(code) {
    if (code === 'Space') {
        return ' ';
    }
    if (/^Key[A-Z]$/.test(code)) {
        return code.slice(3).toLowerCase();
    }
    if (/^Digit[0-9]$/.test(code)) {
        return code.slice(5);
    }
    return code.replace(/(Left|Right)$/, '') || code;
}

/* A short name for a key code, for buttons and the remapping table */
export function keyName(code) {
    return KEY_NAMES[code] ?? code.replace(/^Key|^Digit/, '');
}

/* Events this module sent, which its own remapping must let through */
const sent = new WeakSet();

export function sendKey(code, down) {
    const keyCode = keyCodeOf(code);
    const event = new KeyboardEvent(down ? 'keydown' : 'keyup', {
        code,
        key: keyOf(code),
        keyCode,
        which: keyCode,
        bubbles: true,
        cancelable: true,
    });
    sent.add(event);
    document.dispatchEvent(event);
}

function isEditable(target) {
    return (
        target instanceof HTMLElement &&
        (target.isContentEditable || ['INPUT', 'SELECT', 'TEXTAREA'].includes(target.tagName))
    );
}

/*
 * Keys the player pressed, sent as others: set() takes [player's key, game
 * key] pairs. Typing in the page's own fields never reaches the game at all:
 * SDL listens on the window, so those keys stop at the document, after
 * the field itself has had them.
 */
export class KeyRemapper {
    constructor() {
        this.physical = new Map(); /* player's key -> game key */
        this.held = new Set();
        const keepInPage = (event) => {
            if (isEditable(event.target) || event.target?.closest?.('dialog')) {
                event.stopPropagation();
            }
        };
        for (const type of ['keydown', 'keyup', 'keypress']) {
            document.addEventListener(type, keepInPage);
        }
        const handle = (event) => {
            if (sent.has(event) || isEditable(event.target) || event.target?.closest?.('dialog')) {
                return;
            }
            const target = this.physical.get(event.code);
            if (target === undefined) {
                return;
            }
            event.stopImmediatePropagation();
            event.preventDefault();
            const down = event.type === 'keydown';
            if (down && event.repeat) {
                return;
            }
            if (down) {
                this.held.add(target);
            } else {
                this.held.delete(target);
            }
            sendKey(target, down);
        };
        window.addEventListener('keydown', handle, true);
        window.addEventListener('keyup', handle, true);
    }

    set(pairs = []) {
        this.releaseAll();
        this.physical = new Map();
        for (const [player, game] of pairs) {
            if (player && game && player !== game) {
                this.physical.set(player, game);
            }
        }
    }

    releaseAll() {
        for (const code of this.held) {
            sendKey(code, false);
        }
        this.held.clear();
    }
}

/* The POV hat from the standard mapping's D-pad (buttons 12-15), in hundredths of a degree */
function pointOfView(buttons) {
    const pressed = (index) => (buttons[index]?.pressed ? 1 : 0);
    const x = pressed(15) - pressed(14);
    const y = pressed(13) - pressed(12);
    if (x === 0 && y === 0) {
        return -1;
    }
    const degrees = (Math.atan2(x, -y) * 180) / Math.PI;
    return Math.round(((degrees + 360) % 360) * 100);
}

/* A joystick no gamepad holds */
const DETACHED = { attached: false, position: [0, 0, 0], rotation: [0, 0, 0], pov: -1, buttons: 0 };

/* A gamepad as the engine's joystick: the left stick its position, the
   right stick its rotation, the D-pad its POV hat, bit i for button i */
function joystickOf(pad) {
    const axis = (index) => pad.axes[index] ?? 0;
    let buttons = 0;
    pad.buttons.forEach((button, index) => {
        if (button.pressed && index < 32) {
            buttons |= 1 << index;
        }
    });
    return {
        attached: true,
        position: [axis(0), axis(1), 0],
        rotation: [axis(2), axis(3), 0],
        pov: pointOfView(pad.buttons),
        buttons: buttons >>> 0,
    };
}

/*
 * Gamepads, read every animation frame: each of the first four is the
 * engine's joystick of the same slot (setJoystick(slot, state), state as
 * joystickOf makes it), and the first also presses keys as `mapping`
 * says: {stick: 'arrows' (the stick and D-pad as the arrow keys) or none,
 * buttons: {index: key code}}, the standard mapping's button indices.
 */
export class Gamepads {
    constructor(setJoystick) {
        this.setJoystick = setJoystick;
        this.enabled = true;
        this.mapping = null;
        this.keysDown = new Set();
        this.attached = new Set();
        this.onChange = null;
        const changed = () => this.onChange?.(this.connected());
        window.addEventListener('gamepadconnected', changed);
        window.addEventListener('gamepaddisconnected', changed);
        const frame = () => {
            this.poll();
            requestAnimationFrame(frame);
        };
        requestAnimationFrame(frame);
    }

    connected() {
        return [...(navigator.getGamepads?.() ?? [])].filter(Boolean).map((pad) => pad.id);
    }

    poll() {
        const pads = this.enabled ? [...(navigator.getGamepads?.() ?? [])] : [];
        const seen = new Set();
        const keys = new Set();
        for (const pad of pads) {
            if (!pad || pad.index > 3) {
                continue;
            }
            seen.add(pad.index);
            this.setJoystick(pad.index, joystickOf(pad));
            if (this.mapping && pad === pads.find(Boolean)) {
                this.padKeys(pad, keys);
            }
        }
        for (const index of this.attached) {
            if (!seen.has(index)) {
                this.setJoystick(index, DETACHED);
            }
        }
        this.attached = seen;
        for (const code of keys) {
            if (!this.keysDown.has(code)) {
                sendKey(code, true);
            }
        }
        for (const code of this.keysDown) {
            if (!keys.has(code)) {
                sendKey(code, false);
            }
        }
        this.keysDown = keys;
    }

    padKeys(pad, keys) {
        const config = this.mapping;
        const dead = 0.5;
        if (config.stick === 'arrows') {
            const x = pad.axes[0] ?? 0;
            const y = pad.axes[1] ?? 0;
            const arrows = [
                ['ArrowUp', y < -dead, 12],
                ['ArrowDown', y > dead, 13],
                ['ArrowLeft', x < -dead, 14],
                ['ArrowRight', x > dead, 15],
            ];
            for (const [code, tilted, button] of arrows) {
                if (tilted || pad.buttons[button]?.pressed) {
                    keys.add(code);
                }
            }
        }
        for (const [index, code] of Object.entries(config.buttons ?? {})) {
            if (code && pad.buttons[index]?.pressed) {
                keys.add(code);
            }
        }
    }

    release() {
        for (const code of this.keysDown) {
            sendKey(code, false);
        }
        this.keysDown.clear();
    }
}

const ARROW_SHAPES = {
    ArrowUp: '10,5 16,14 4,14',
    ArrowLeft: '5,10 14,4 14,16',
    ArrowRight: '15,10 6,4 6,16',
    ArrowDown: '10,15 4,6 16,6',
};

/* Each button tracks its own held pointers (by id), so two fingers on two
   buttons (a diagonal) hold both keys, and a second finger joining or
   leaving an already-held button sends no extra keydown/keyup. */
function bindTouchButton(button, code) {
    const held = new Set();
    let down = false;
    const sync = () => {
        const wantDown = held.size > 0;
        if (wantDown !== down) {
            down = wantDown;
            sendKey(code, down);
        }
    };
    button.addEventListener('pointerdown', (event) => {
        event.preventDefault();
        button.setPointerCapture(event.pointerId);
        held.add(event.pointerId);
        sync();
    });
    const release = (event) => {
        held.delete(event.pointerId);
        sync();
    };
    button.addEventListener('pointerup', release);
    button.addEventListener('pointercancel', release);
    button.addEventListener('lostpointercapture', release);
}

/* The profile's touch screen buttons, in `root`: a D-pad and round buttons */
export function buildTouchControls(root, profile) {
    root.replaceChildren();
    const touch = profile.touch ?? {};
    if (touch.dpad) {
        const dpad = document.createElement('div');
        dpad.id = 'dpad';
        for (const [code, points] of Object.entries(ARROW_SHAPES)) {
            const button = document.createElement('button');
            button.className = `dpad-btn dpad-${code.slice(5).toLowerCase()}`;
            button.setAttribute('aria-label', code.slice(5));
            button.innerHTML =
                '<svg viewBox="0 0 20 20" aria-hidden="true">' +
                `<polygon points="${points}"/></svg>`;
            bindTouchButton(button, code);
            dpad.append(button);
        }
        root.append(dpad);
    }
    if (touch.buttons?.length) {
        const actions = document.createElement('div');
        actions.id = 'touch-actions';
        for (const code of touch.buttons) {
            const button = document.createElement('button');
            button.className = 'action-btn';
            button.textContent = keyName(code);
            bindTouchButton(button, code);
            actions.append(button);
        }
        root.append(actions);
    }
}
