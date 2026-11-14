// The page's developer tools, under the player: how fast the frames go, the
// objects of every file the engine runs (and one of them in detail), its
// scripts, a recording of what the behavior engine did over a few frames,
// and the log. `api` is the player's (player.mjs's bindApi).

import { download, element, smallButton } from './dom.mjs';

/* The most rows a table shows (the rest are counted, not drawn) */
const ROW_LIMIT = 2000;

/* A trace's event kinds, by what the player's player_trace_kind_name calls them */
const TRACE_FILTERS = {
    all: () => true,
    execute: (name) => name === 'execute',
    message: (name) => name.startsWith('message'),
    activate: (name) => name === 'activate',
    problem: (name) => name === 'unknown block' || name === 'infinite loop',
};

/* Log lines by kind: the player's errors, other output on its error stream, the game's
   console, and everything else */
const LOG_KINDS = ['error', 'warning', 'console', 'info'];

function fillTable(table, headers, rows, onPick) {
    const head = element(
        'tr',
        {},
        headers.map((text) => element('th', { textContent: text }))
    );
    const body = rows.slice(0, ROW_LIMIT).map((row) => {
        const cells = row.cells.map((cell) =>
            cell instanceof Node
                ? element('td', {}, [cell])
                : element('td', { textContent: String(cell) })
        );
        const tr = element('tr', {}, cells);
        if (onPick) {
            tr.className = 'pickable';
            tr.addEventListener('click', () => onPick(row));
        }
        return tr;
    });
    table.replaceChildren(element('thead', {}, [head]), element('tbody', {}, body));
    const count = rows.length.toLocaleString();
    return rows.length > ROW_LIMIT
        ? `${count} (the first ${ROW_LIMIT.toLocaleString()} shown)`
        : count;
}

function formatVector(matrix, row) {
    return [0, 1, 2].map((i) => matrix[row * 4 + i].toFixed(3)).join(', ');
}

/* A parameter's bytes, as what they most likely hold */
function describeValue(bytes) {
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const lines = [];
    if (bytes.length === 4) {
        lines.push(['As an integer', String(view.getInt32(0, true))]);
        lines.push(['As a float', String(view.getFloat32(0, true))]);
    } else if (bytes.length % 4 === 0 && bytes.length > 0 && bytes.length <= 64) {
        const floats = [];
        for (let at = 0; at < bytes.length; at += 4) {
            floats.push(view.getFloat32(at, true).toFixed(3));
        }
        lines.push(['As floats', floats.join(', ')]);
    }
    const end = bytes.indexOf(0);
    const text = new TextDecoder('latin1').decode(bytes.subarray(0, end < 0 ? bytes.length : end));
    if (text.length > 0 && /^[\x20-\x7e\xa0-\xff]*$/.test(text)) {
        lines.push(['As text', text]);
    }
    const hex = [...bytes.subarray(0, 64)].map((byte) => byte.toString(16).padStart(2, '0'));
    lines.push(['Bytes', hex.join(' ') + (bytes.length > 64 ? ' …' : '')]);
    return lines;
}

export function createDevtools(root, getApi, isPaused) {
    const tabs = ['stats', 'objects', 'scripts', 'trace', 'log'];
    const tabBar = element('div', { className: 'tabs', role: 'tablist' });
    const panels = {};
    let current = 'log';
    const running = () => getApi()?.running() === 1;
    const empty = () =>
        element('p', { className: 'empty', textContent: 'Start a game to see this.' });

    function show(name) {
        current = name;
        for (const button of tabBar.children) {
            button.setAttribute('aria-selected', String(button.dataset.tab === name));
        }
        for (const [key, panel] of Object.entries(panels)) {
            panel.hidden = key !== name;
        }
        refresh();
    }

    for (const name of tabs) {
        const label = name[0].toUpperCase() + name.slice(1);
        const button = element('button', { className: 'tab', role: 'tab', textContent: label });
        button.dataset.tab = name;
        button.addEventListener('click', () => show(name));
        tabBar.append(button);
        panels[name] = element('div', { className: 'tab-panel', role: 'tabpanel' });
    }
    root.replaceChildren(tabBar, ...Object.values(panels));

    /* ---- Stats: rates, timings, memory, and a graph of the last frames ---- */

    const statsList = element('dl', { className: 'stats' });
    const graph = element('canvas', { className: 'graph', width: 600, height: 90 });
    panels.stats.append(statsList, graph);
    const history = [];
    let lastFrames = 0;
    let lastTime = performance.now();
    let fps = 0;

    function sample() {
        const api = getApi();
        if (!api || !running()) {
            history.length = 0;
            return;
        }
        history.push([api.stepMs(), api.drawMs()]);
        if (history.length > 300) {
            history.shift();
        }
        const now = performance.now();
        if (now - lastTime >= 1000) {
            const frames = api.frames();
            fps = Math.round(((frames - lastFrames) * 1000) / (now - lastTime));
            lastFrames = frames;
            lastTime = now;
        }
    }

    const average = (index) =>
        history.length === 0
            ? 0
            : history.reduce((sum, entry) => sum + entry[index], 0) / history.length;

    function drawGraph() {
        const context = graph.getContext('2d');
        const width = graph.width;
        const height = graph.height;
        const scale = height / 33.4; /* two 60 Hz frames tall */
        const barWidth = width / 300;
        context.clearRect(0, 0, width, height);
        context.strokeStyle = 'rgba(128,128,128,0.5)';
        context.beginPath();
        context.moveTo(0, height - 16.7 * scale);
        context.lineTo(width, height - 16.7 * scale);
        context.stroke();
        history.forEach(([step, draw], i) => {
            const x = i * barWidth;
            context.fillStyle = '#4fa3d3';
            context.fillRect(x, height - step * scale, barWidth, step * scale);
            context.fillStyle = '#4fd38a';
            context.fillRect(x, height - (step + draw) * scale, barWidth, draw * scale);
        });
    }

    function drawStats() {
        const api = getApi();
        graph.hidden = !running();
        if (!running()) {
            statsList.replaceChildren(empty());
            return;
        }
        const rows = [
            ['Frame rate', `${fps} fps`],
            ['Frames', api.frames()],
            ['Engine time', `${(api.time() / 1000).toFixed(1)} s`],
            ['Behaviors (avg)', `${average(0).toFixed(2)} ms`],
            ['Drawing (avg)', `${average(1).toFixed(2)} ms`],
            ['Memory', `${(api.memory() / 1048576).toFixed(0)} MB`],
            ['Files', api.fileCount()],
            ['Scripts', api.scriptCount()],
        ];
        statsList.replaceChildren(
            ...rows.flatMap(([term, value]) => [
                element('dt', { textContent: term }),
                element('dd', { textContent: String(value) }),
            ])
        );
        drawGraph();
    }

    /* ---- Objects: every file's objects, with their class and state; one in detail ---- */

    const fileSelect = element('select', { 'aria-label': 'File' });
    const objectFilter = element('input', {
        type: 'search',
        placeholder: 'Filter by name, class or ID',
    });
    const objectCount = element('span', { className: 'count' });
    const objectTable = element('table');
    const details = element('div', { className: 'details' });
    let picked = 0;

    function drawDetails() {
        const api = getApi();
        if (picked === 0 || !running()) {
            details.replaceChildren();
            return;
        }
        const label = (id) => (id ? api.label(id) : 'none');
        const classId = api.objectClass(picked);
        const rows = [
            ['Class', api.className(classId) || `class ${classId}`],
            ['Active', api.objectActive(picked) ? 'yes' : 'no'],
            ['Visible', api.objectVisible(picked) ? 'yes' : 'no'],
        ];
        const world = api.entityMatrix(picked, true);
        if (world) {
            rows.push(['World position', formatVector(world, 3)]);
            rows.push(['Local position', formatVector(api.entityMatrix(picked, false), 3)]);
            rows.push(['Parent', label(api.entityParent(picked))]);
        }
        const flags = api.behaviorFlags(picked);
        if (flags !== 0) {
            rows.push(['Behavior flags', `0x${(flags >>> 0).toString(16)}`]);
            rows.push(['Behavior active', api.behaviorActive(picked) ? 'yes' : 'no']);
            rows.push(['Executions', api.behaviorExecutions(picked)]);
        }
        const value = api.paramValue(picked);
        if (value) {
            rows.push(['Parameter type', value.type]);
            rows.push(...describeValue(value.bytes));
        }
        const list = rows.flatMap(([term, text]) => [
            element('dt', { textContent: term }),
            element('dd', { textContent: String(text) }),
        ]);
        details.replaceChildren(
            element('div', { className: 'details-head' }, [
                element('strong', { textContent: label(picked) }),
                smallButton('Refresh', drawDetails),
                smallButton('Close', () => {
                    picked = 0;
                    drawDetails();
                }),
            ]),
            element('dl', { className: 'stats' }, list)
        );
    }

    function syncFiles(api) {
        const files = api.fileCount();
        if (fileSelect.options.length === files) {
            return;
        }
        const selected = fileSelect.value;
        fileSelect.replaceChildren(
            ...Array.from({ length: files }, (_, i) =>
                element('option', {
                    value: String(i),
                    textContent: api.fileName(i).split('/').pop() || `File ${i}`,
                })
            )
        );
        fileSelect.value = selected || '0';
    }

    function drawObjects() {
        const api = getApi();
        if (!running()) {
            objectTable.replaceChildren();
            objectCount.textContent = '';
            fileSelect.replaceChildren();
            picked = 0;
            drawDetails();
            return;
        }
        syncFiles(api);
        const file = Number(fileSelect.value || 0);
        const needle = objectFilter.value.trim().toLowerCase();
        const classNames = new Map();
        const rows = [];
        const count = api.objectCount(file);
        for (let i = 0; i < count; i++) {
            const id = api.object(file, i);
            const classId = api.objectClass(id);
            if (!classNames.has(classId)) {
                classNames.set(classId, api.className(classId) || `class ${classId}`);
            }
            const name = api.objectName(id);
            const className = classNames.get(classId);
            const matches =
                !needle ||
                name.toLowerCase().includes(needle) ||
                className.toLowerCase().includes(needle) ||
                String(id) === needle;
            if (matches) {
                const active = api.objectActive(id) ? '●' : '';
                const visible = api.objectVisible(id) ? '●' : '';
                rows.push({ id, cells: [id, name, className, active, visible] });
            }
        }
        const headers = ['ID', 'Name', 'Class', 'Active', 'Visible'];
        const shown = fillTable(objectTable, headers, rows, (row) => {
            picked = row.id;
            drawDetails();
        });
        objectCount.textContent = needle
            ? `${shown} of ${count.toLocaleString()} objects match`
            : `${shown} objects (pick one for more)`;
        drawDetails();
    }

    panels.objects.append(
        element('div', { className: 'toolbar' }, [
            fileSelect,
            objectFilter,
            smallButton('Refresh', drawObjects),
            objectCount,
        ]),
        element('div', { className: 'table-wrap' }, [objectTable]),
        details
    );
    fileSelect.addEventListener('change', drawObjects);
    objectFilter.addEventListener('input', drawObjects);

    /* ---- Scripts: whether each runs, and how often its behaviors have ---- */

    const scriptFilter = element('input', { type: 'search', placeholder: 'Filter scripts' });
    const activeOnly = element('input', { type: 'checkbox' });
    const scriptCount = element('span', { className: 'count' });
    const scriptTable = element('table');

    function drawScripts() {
        const api = getApi();
        if (!running()) {
            scriptTable.replaceChildren();
            scriptCount.textContent = '';
            return;
        }
        const needle = scriptFilter.value.trim().toLowerCase();
        const rows = [];
        const count = api.scriptCount();
        for (let i = 0; i < count; i++) {
            const id = api.scriptId(i);
            const active = api.behaviorActive(id) === 1;
            const label = active || !activeOnly.checked ? api.label(id) : '';
            if (label && (!needle || label.toLowerCase().includes(needle))) {
                rows.push({ cells: [label, active ? '●' : '', api.behaviorExecutions(id)] });
            }
        }
        const shown = fillTable(scriptTable, ['Script', 'Active', 'Executions'], rows);
        scriptCount.textContent =
            rows.length === count
                ? `${shown} scripts`
                : `${shown} of ${count.toLocaleString()} scripts`;
    }

    panels.scripts.append(
        element('div', { className: 'toolbar' }, [
            scriptFilter,
            element('label', {}, [activeOnly, ' Active only']),
            smallButton('Refresh', drawScripts),
            scriptCount,
        ]),
        element('div', { className: 'table-wrap' }, [scriptTable])
    );
    scriptFilter.addEventListener('input', drawScripts);
    activeOnly.addEventListener('change', drawScripts);

    /* ---- Trace: what the behavior engine did, frame by frame ---- */

    const traceFrames = element('input', {
        type: 'number',
        min: 1,
        max: 120,
        value: 1,
        'aria-label': 'Frames to record',
    });
    const traceKinds = element('select', { 'aria-label': 'Events' }, [
        element('option', { value: 'all', textContent: 'Every event' }),
        element('option', { value: 'execute', textContent: 'Executions' }),
        element('option', { value: 'message', textContent: 'Messages' }),
        element('option', { value: 'activate', textContent: 'Activations' }),
        element('option', { value: 'problem', textContent: 'Unknown blocks, loops' }),
    ]);
    const traceStatus = element('span', { className: 'count' });
    const traceTable = element('table');
    let traceEvents = [];
    let traceDropped = 0;

    /* The recorded events, from the player's records (6 words each) */
    function readTrace() {
        const api = getApi();
        const records = api.traceRecords();
        const labels = new Map([[0, '']]);
        const kindNames = new Map();
        const label = (id) => {
            if (!labels.has(id)) {
                labels.set(id, api.label(id));
            }
            return labels.get(id);
        };
        traceEvents = [];
        for (let at = 0; at < records.length; at += 6) {
            const kind = records[at];
            if (!kindNames.has(kind)) {
                kindNames.set(kind, api.traceKindName(kind) || String(kind));
            }
            const name = kindNames.get(kind);
            const result = records[at + 4];
            let detail = result === 0 ? '' : String(result);
            if (name.startsWith('message')) {
                detail = api.messageName(result) || detail;
            }
            traceEvents.push({
                frame: records[at + 1] >>> 0,
                name,
                behavior: label(records[at + 2] >>> 0),
                object: label(records[at + 3] >>> 0),
                detail,
                depth: records[at + 5],
            });
        }
        traceDropped = api.traceDropped();
        traceSave.disabled = traceEvents.length === 0;
        drawTrace();
    }

    function drawTrace() {
        const keep = TRACE_FILTERS[traceKinds.value];
        const rows = traceEvents
            .filter((event) => keep(event.name))
            .map((event) => ({
                cells: [
                    event.frame,
                    event.name,
                    element('span', {
                        textContent: event.behavior,
                        style: `padding-left:${event.depth * 12}px`,
                    }),
                    event.object,
                    event.detail,
                ],
            }));
        const shown = fillTable(
            traceTable,
            ['Frame', 'Event', 'Behavior', 'Object', 'Result'],
            rows
        );
        const of =
            rows.length === traceEvents.length ? '' : ` of ${traceEvents.length.toLocaleString()}`;
        const dropped =
            traceDropped > 0
                ? `; ${traceDropped.toLocaleString()} more found no room and are not here`
                : '';
        traceStatus.textContent = traceEvents.length === 0 ? '' : `${shown}${of} events${dropped}`;
    }

    function record() {
        const api = getApi();
        if (!running()) {
            traceStatus.textContent = 'Start a game first.';
            return;
        }
        if (isPaused()) {
            traceStatus.textContent = 'The game is paused: play it to record its frames.';
            return;
        }
        api.trace(Math.max(1, Math.min(120, Number(traceFrames.value) || 1)));
        traceButton.disabled = true;
        const wait = () => {
            if (!running()) {
                traceButton.disabled = false;
                traceStatus.textContent = 'The game ended before the recording did.';
            } else if (api.tracing()) {
                traceStatus.textContent = isPaused()
                    ? 'Recording: it goes on when the game plays again.'
                    : 'Recording…';
                setTimeout(wait, 100);
            } else {
                traceButton.disabled = false;
                readTrace();
            }
        };
        wait();
    }

    function saveTrace() {
        const lines = traceEvents.map((event) =>
            [
                event.frame,
                '  '.repeat(event.depth) + event.name,
                event.behavior,
                event.object,
                event.detail,
            ].join('\t')
        );
        download('trace.txt', new Blob([lines.join('\n') + '\n'], { type: 'text/plain' }));
    }

    const traceButton = smallButton('Record', record);
    const traceSave = smallButton('Save as text', saveTrace);
    traceSave.disabled = true;
    panels.trace.append(
        element('div', { className: 'toolbar' }, [
            element('label', {}, ['Frames ', traceFrames]),
            traceButton,
            traceKinds,
            traceSave,
            traceStatus,
        ]),
        element('div', { className: 'table-wrap' }, [traceTable])
    );
    traceKinds.addEventListener('change', drawTrace);

    /* ---- Log: the player's output, its errors, and the game's console ---- */

    const logFilter = element('select', { 'aria-label': 'Show' }, [
        element('option', { value: 'all', textContent: 'Everything' }),
        element('option', { value: 'error', textContent: 'Errors' }),
        element('option', { value: 'warning', textContent: 'Warnings' }),
        element('option', { value: 'console', textContent: "The game's console" }),
    ]);
    const log = element('div', { className: 'log' });
    const copyLog = () => {
        const shown = [...log.children].filter((line) => !line.hidden);
        navigator.clipboard?.writeText(shown.map((line) => line.textContent).join('\n'));
    };
    panels.log.append(
        element('div', { className: 'toolbar' }, [
            logFilter,
            smallButton('Clear', () => log.replaceChildren()),
            smallButton('Copy', copyLog),
        ]),
        log
    );
    logFilter.addEventListener('change', () => {
        for (const line of log.children) {
            line.hidden = logFilter.value !== 'all' && !line.classList.contains(logFilter.value);
        }
    });

    function refresh() {
        if (root.hidden) {
            return;
        }
        if (current === 'stats') {
            drawStats();
        } else if (current === 'objects') {
            drawObjects();
        } else if (current === 'scripts') {
            drawScripts();
        }
    }

    /* Stats sample every animation frame and redraw twice a second; the
       tables only on demand (they can be thousands of rows) */
    const tick = () => {
        sample();
        requestAnimationFrame(tick);
    };
    requestAnimationFrame(tick);
    setInterval(() => {
        if (!root.hidden && current === 'stats') {
            drawStats();
        }
    }, 500);
    show('log');

    const tools = {
        /* Called when an error opens the tools (the page shows them as open) */
        onOpen: null,
        /* A line of the log; `kind` one of LOG_KINDS. An error opens the tools at the log. */
        print(text, kind = 'info') {
            const className = LOG_KINDS.includes(kind) ? kind : 'info';
            const line = element('div', { className, textContent: text });
            line.hidden = logFilter.value !== 'all' && logFilter.value !== className;
            const stick = log.scrollTop + log.clientHeight >= log.scrollHeight - 4;
            log.append(line);
            while (log.childElementCount > 5000) {
                log.firstElementChild.remove();
            }
            if (stick) {
                log.scrollTop = log.scrollHeight;
            }
            if (className === 'error') {
                root.hidden = false;
                tools.onOpen?.();
                show('log');
            }
        },
        show,
        refresh,
        /* The game started or ended: forget what the tables showed */
        reset() {
            traceEvents = [];
            traceDropped = 0;
            traceSave.disabled = true;
            traceTable.replaceChildren();
            traceStatus.textContent = '';
            fileSelect.replaceChildren();
            picked = 0;
            history.length = 0;
            refresh();
        },
    };
    return tools;
}
