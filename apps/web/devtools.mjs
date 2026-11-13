// The page's developer tools, under the player: how fast the frames go, the
// objects of every file the engine runs, its scripts, a recording of what
// the behavior engine did over a few frames, and the log.

const TRACE_KINDS = [
    'frame begin', 'frame end', 'activate', 'execute', 'link', 'link delayed', 'link arrived',
    'operation', 'unknown block', 'infinite loop', 'message sent', 'message received', 'effect',
    'file loaded',
];

/* The most rows a table shows (the rest are counted, not drawn) */
const ROW_LIMIT = 2000;

function element(tag, props = {}, children = []) {
    const node = document.createElement(tag);
    for (const [key, value] of Object.entries(props)) {
        if (key === 'class') {
            node.className = value;
        } else if (key in node) {
            node[key] = value;
        } else {
            node.setAttribute(key, value);
        }
    }
    node.append(...children);
    return node;
}

function fillTable(table, headers, rows, total) {
    const head = element('tr', {}, headers.map((text) => element('th', {textContent: text})));
    const body = rows.slice(0, ROW_LIMIT).map((cells) => element('tr', {}, cells.map((cell) =>
        cell instanceof Node ? element('td', {}, [cell]) : element('td', {textContent: String(cell)}))));
    table.replaceChildren(element('thead', {}, [head]), element('tbody', {}, body));
    const count = (total ?? rows.length).toLocaleString();
    return rows.length > ROW_LIMIT ? `${count} (the first ${ROW_LIMIT.toLocaleString()} shown)` : count;
}

function download(name, blob) {
    const url = URL.createObjectURL(blob);
    const link = element('a', {href: url, download: name});
    document.body.append(link);
    link.click();
    link.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
}

export function createDevtools(root, getApi) {
    const tabs = ['stats', 'objects', 'scripts', 'trace', 'log'];
    const tabBar = element('div', {class: 'tabs', role: 'tablist'});
    const panels = {};
    let current = 'log';
    const show = (name) => {
        current = name;
        for (const button of tabBar.children) {
            button.setAttribute('aria-selected', String(button.dataset.tab === name));
        }
        for (const [key, panel] of Object.entries(panels)) {
            panel.hidden = key !== name;
        }
        refresh();
    };
    for (const name of tabs) {
        const button = element('button', {class: 'tab', role: 'tab', textContent: name[0].toUpperCase() + name.slice(1)});
        button.dataset.tab = name;
        button.addEventListener('click', () => show(name));
        tabBar.append(button);
        panels[name] = element('div', {class: 'tab-panel', role: 'tabpanel'});
    }
    root.replaceChildren(tabBar, ...Object.values(panels));
    const running = () => getApi()?.running() === 1;

    /* ---- Stats: rates, timings, memory, and a graph of the last frames ---- */
    const statsList = element('dl', {class: 'stats'});
    const graph = element('canvas', {class: 'graph', width: 600, height: 90});
    panels.stats.append(statsList, graph);
    const history = [];
    let lastFrames = 0;
    let lastTime = performance.now();
    let fps = 0;
    const sample = () => {
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
    };
    const drawStats = () => {
        const api = getApi();
        if (!running()) {
            statsList.replaceChildren(element('p', {class: 'empty', textContent: 'Start a game to see this.'}));
            graph.hidden = true;
            return;
        }
        graph.hidden = false;
        const average = (index) => history.length === 0 ? 0 :
            history.reduce((sum, entry) => sum + entry[index], 0) / history.length;
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
        statsList.replaceChildren(...rows.flatMap(([term, value]) =>
            [element('dt', {textContent: term}), element('dd', {textContent: String(value)})]));
        const context = graph.getContext('2d');
        const width = graph.width, height = graph.height;
        context.clearRect(0, 0, width, height);
        const scale = height / 33.4; /* two 60 Hz frames tall */
        context.strokeStyle = 'rgba(128,128,128,0.5)';
        context.beginPath();
        context.moveTo(0, height - 16.7 * scale);
        context.lineTo(width, height - 16.7 * scale);
        context.stroke();
        const barWidth = width / 300;
        history.forEach(([step, draw], i) => {
            const x = i * barWidth;
            context.fillStyle = '#4fa3d3';
            context.fillRect(x, height - step * scale, barWidth, step * scale);
            context.fillStyle = '#4fd38a';
            context.fillRect(x, height - (step + draw) * scale, barWidth, draw * scale);
        });
    };

    /* ---- Objects: every file's objects, with their class and state ---- */
    const fileSelect = element('select', {'aria-label': 'File'});
    const objectFilter = element('input', {type: 'search', placeholder: 'Filter by name, class or ID'});
    const objectCount = element('span', {class: 'count'});
    const objectTable = element('table');
    const objectRefresh = element('button', {class: 'small', textContent: 'Refresh'});
    panels.objects.append(
        element('div', {class: 'toolbar'}, [fileSelect, objectFilter, objectRefresh, objectCount]),
        element('div', {class: 'table-wrap'}, [objectTable]));
    const drawObjects = () => {
        const api = getApi();
        if (!running()) {
            objectTable.replaceChildren();
            objectCount.textContent = '';
            fileSelect.replaceChildren();
            return;
        }
        const files = api.fileCount();
        if (fileSelect.options.length !== files) {
            const selected = fileSelect.value;
            fileSelect.replaceChildren(...Array.from({length: files}, (_, i) =>
                element('option', {value: String(i), textContent: api.fileName(i).split('/').pop() || `File ${i}`})));
            fileSelect.value = selected || '0';
        }
        const file = Number(fileSelect.value || 0);
        const needle = objectFilter.value.trim().toLowerCase();
        const classNames = new Map();
        const rows = [];
        const count = api.objectCount(file);
        for (let i = 0; i < count; i++) {
            const id = api.object(file, i) >>> 0;
            const classId = api.objectClass(id);
            if (!classNames.has(classId)) {
                classNames.set(classId, api.className(classId) || `class ${classId}`);
            }
            const name = api.objectName(id);
            const className = classNames.get(classId);
            if (needle && !name.toLowerCase().includes(needle) && !className.toLowerCase().includes(needle) &&
                String(id) !== needle) {
                continue;
            }
            const state = api.objectState(id);
            rows.push([id, name, className, state & 1 ? '●' : '', state & 2 ? '●' : '']);
        }
        const shown = fillTable(objectTable, ['ID', 'Name', 'Class', 'Active', 'Visible'], rows);
        objectCount.textContent = needle ? `${shown} of ${count.toLocaleString()} objects match` : `${shown} objects`;
    };
    fileSelect.addEventListener('change', drawObjects);
    objectFilter.addEventListener('input', drawObjects);
    objectRefresh.addEventListener('click', drawObjects);

    /* ---- Scripts: whether each runs, and how often its behaviors have ---- */
    const scriptFilter = element('input', {type: 'search', placeholder: 'Filter scripts'});
    const activeOnly = element('input', {type: 'checkbox'});
    const scriptCount = element('span', {class: 'count'});
    const scriptTable = element('table');
    const scriptRefresh = element('button', {class: 'small', textContent: 'Refresh'});
    panels.scripts.append(
        element('div', {class: 'toolbar'}, [scriptFilter, element('label', {}, [activeOnly, ' Active only']),
                                            scriptRefresh, scriptCount]),
        element('div', {class: 'table-wrap'}, [scriptTable]));
    const drawScripts = () => {
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
            const id = api.scriptId(i) >>> 0;
            const active = api.behaviorActive(id) === 1;
            if (activeOnly.checked && !active) {
                continue;
            }
            const label = api.label(id);
            if (needle && !label.toLowerCase().includes(needle)) {
                continue;
            }
            rows.push([label, active ? '●' : '', api.behaviorExecutions(id)]);
        }
        const shown = fillTable(scriptTable, ['Script', 'Active', 'Executions'], rows);
        scriptCount.textContent = rows.length === count ? `${shown} scripts` : `${shown} of ${count.toLocaleString()} scripts`;
    };
    scriptFilter.addEventListener('input', drawScripts);
    activeOnly.addEventListener('change', drawScripts);
    scriptRefresh.addEventListener('click', drawScripts);

    /* ---- Trace: what the behavior engine did, frame by frame ---- */
    const traceFrames = element('input', {type: 'number', min: 1, max: 120, value: 1, 'aria-label': 'Frames to record'});
    const traceButton = element('button', {class: 'small', textContent: 'Record'});
    const traceKinds = element('select', {'aria-label': 'Events'}, [
        element('option', {value: 'all', textContent: 'Every event'}),
        element('option', {value: 'execute', textContent: 'Executions'}),
        element('option', {value: 'message', textContent: 'Messages'}),
        element('option', {value: 'activate', textContent: 'Activations'}),
        element('option', {value: 'problem', textContent: 'Unknown blocks, loops'}),
    ]);
    const traceSave = element('button', {class: 'small', textContent: 'Save as text', disabled: true});
    const traceStatus = element('span', {class: 'count'});
    const traceTable = element('table');
    panels.trace.append(
        element('div', {class: 'toolbar'}, [element('label', {}, ['Frames ', traceFrames]), traceButton,
                                            traceKinds, traceSave, traceStatus]),
        element('div', {class: 'table-wrap'}, [traceTable]));
    let traceEvents = [];
    const kindMatches = (kind) => {
        switch (traceKinds.value) {
        case 'execute': return kind === 3;
        case 'message': return kind === 10 || kind === 11;
        case 'activate': return kind === 2;
        case 'problem': return kind === 8 || kind === 9;
        default: return true;
        }
    };
    const readTrace = () => {
        const api = getApi();
        const labels = new Map();
        const label = (id) => {
            if (id === 0) {
                return '';
            }
            if (!labels.has(id)) {
                labels.set(id, api.label(id));
            }
            return labels.get(id);
        };
        const count = api.traceCount();
        traceEvents = [];
        for (let i = 0; i < count; i++) {
            const kind = api.traceKind(i);
            const result = api.traceResult(i);
            let detail = result === 0 ? '' : String(result);
            if (kind === 10 || kind === 11) {
                detail = api.messageName(result) || detail;
            }
            traceEvents.push({
                frame: api.traceFrame(i),
                depth: api.traceDepth(i),
                kind,
                behavior: label(api.traceBehavior(i) >>> 0),
                object: label(api.traceObject(i) >>> 0),
                detail,
            });
        }
        traceSave.disabled = traceEvents.length === 0;
        drawTrace();
    };
    const drawTrace = () => {
        const rows = traceEvents.filter((event) => kindMatches(event.kind)).map((event) => [
            event.frame,
            TRACE_KINDS[event.kind] ?? event.kind,
            element('span', {textContent: event.behavior, style: `padding-left:${event.depth * 12}px`}),
            event.object,
            event.detail,
        ]);
        const shown = fillTable(traceTable, ['Frame', 'Event', 'Behavior', 'Object', 'Result'], rows);
        traceStatus.textContent = traceEvents.length === 0 ? ''
            : rows.length === traceEvents.length ? `${shown} events`
            : `${shown} of ${traceEvents.length.toLocaleString()} events`;
    };
    traceKinds.addEventListener('change', drawTrace);
    traceButton.addEventListener('click', () => {
        const api = getApi();
        if (!running()) {
            traceStatus.textContent = 'Start a game first.';
            return;
        }
        api.trace(Math.max(1, Math.min(120, Number(traceFrames.value) || 1)));
        traceButton.disabled = true;
        traceStatus.textContent = 'Recording…';
        const wait = () => {
            if (!running()) {
                traceButton.disabled = false;
                traceStatus.textContent = 'The game ended before the recording did.';
            } else if (api.tracing()) {
                setTimeout(wait, 50);
            } else {
                traceButton.disabled = false;
                readTrace();
            }
        };
        wait();
    });
    traceSave.addEventListener('click', () => {
        const lines = traceEvents.map((event) =>
            `${event.frame}\t${'  '.repeat(event.depth)}${TRACE_KINDS[event.kind] ?? event.kind}\t${event.behavior}\t${event.object}\t${event.detail}`);
        download('trace.txt', new Blob([lines.join('\n') + '\n'], {type: 'text/plain'}));
    });

    /* ---- Log: the player's output, its errors, and the game's console ---- */
    const logFilter = element('select', {'aria-label': 'Show'}, [
        element('option', {value: 'all', textContent: 'Everything'}),
        element('option', {value: 'error', textContent: 'Errors'}),
        element('option', {value: 'console', textContent: "The game's console"}),
    ]);
    const logClear = element('button', {class: 'small', textContent: 'Clear'});
    const logCopy = element('button', {class: 'small', textContent: 'Copy'});
    const log = element('div', {class: 'log'});
    panels.log.append(element('div', {class: 'toolbar'}, [logFilter, logClear, logCopy]), log);
    const applyLogFilter = () => {
        for (const line of log.children) {
            line.hidden = logFilter.value !== 'all' && !line.classList.contains(logFilter.value);
        }
    };
    logFilter.addEventListener('change', applyLogFilter);
    logClear.addEventListener('click', () => log.replaceChildren());
    logCopy.addEventListener('click', () => {
        const text = [...log.children].filter((line) => !line.hidden).map((line) => line.textContent).join('\n');
        navigator.clipboard?.writeText(text);
    });

    function refresh() {
        if (root.hidden) {
            return;
        }
        if (current === 'stats') drawStats();
        if (current === 'objects') drawObjects();
        if (current === 'scripts') drawScripts();
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

    return {
        print(text, error) {
            const kind = error ? 'error' : text.startsWith('[console] ') ? 'console' : 'info';
            const line = element('div', {class: kind, textContent: text});
            line.hidden = logFilter.value !== 'all' && logFilter.value !== kind;
            const stick = log.scrollTop + log.clientHeight >= log.scrollHeight - 4;
            log.append(line);
            while (log.childElementCount > 5000) {
                log.firstElementChild.remove();
            }
            if (stick) {
                log.scrollTop = log.scrollHeight;
            }
            if (error) {
                root.hidden = false;
                show('log');
            }
        },
        show,
        refresh,
        /* The game started or ended: forget what the tables showed */
        reset() {
            traceEvents = [];
            traceSave.disabled = true;
            traceTable.replaceChildren();
            traceStatus.textContent = '';
            fileSelect.replaceChildren();
            history.length = 0;
            refresh();
        },
    };
}
