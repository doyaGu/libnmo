// The Saves dialog: what a game wrote (kept in the browser's IndexedDB
// through the player's IDBFS folder), exported as a .zip, imported from
// one, or deleted.

import { download, element, fileNameOf, formatSize } from './dom.mjs';
import { listTree, removeTree, saveFolder, writeGameFile } from './games.mjs';
import { readZip, writeZip } from './zip.mjs';

/*
 * `module` is the player's, once ready; `persist` keeps the saves folder's
 * changes; `print(text, kind)` reports to the log; `playing()` is the id
 * of the game running, if any.
 */
export function createSavesDialog({ dialog, getModule, persist, print, playing }) {
    const $ = (id) => document.getElementById(id);
    let game = null;

    function draw() {
        const module = getModule();
        const files = listTree(module, saveFolder(game.id));
        const table = $('saves-table');
        if (files.length === 0) {
            const cell = element('td', { className: 'dim', textContent: 'Nothing saved yet.' });
            table.replaceChildren(element('tr', {}, [cell]));
        } else {
            table.replaceChildren(
                ...files.map(([path, size, time]) =>
                    element('tr', {}, [
                        element('td', { textContent: path }),
                        element('td', { textContent: formatSize(size) }),
                        element('td', { textContent: new Date(time).toLocaleString() }),
                    ])
                )
            );
        }
        $('saves-export').disabled = files.length === 0;
        $('saves-clear').disabled = files.length === 0;
        $('saves-note').textContent = '';
    }

    function note(text) {
        $('saves-note').textContent = text;
    }

    $('saves-export').addEventListener('click', () => {
        const module = getModule();
        const folder = saveFolder(game.id);
        const files = listTree(module, folder).map(([path, , time]) => [
            path,
            module.FS.readFile(`${folder}/${path}`),
            time,
        ]);
        download(`${fileNameOf(game.name)} saves.zip`, writeZip(files));
    });

    $('saves-import').addEventListener('change', async (event) => {
        const file = event.target.files[0];
        event.target.value = '';
        if (!file) {
            return;
        }
        const module = getModule();
        const folder = saveFolder(game.id);
        try {
            const { files, skipped } = await readZip(file);
            for (const [path, entry] of files) {
                writeGameFile(
                    module,
                    `${folder}/${path}`,
                    new Uint8Array(await entry.arrayBuffer())
                );
            }
            persist();
            draw();
            const parts = [`Imported ${files.length} files.`];
            if (skipped.length > 0) {
                parts.push(`Left out ${skipped.length} that would land outside this game's saves.`);
            }
            if (playing() === game.id) {
                parts.push('Restart the game to load them.');
            }
            note(parts.join(' '));
        } catch (error) {
            note(`Could not import ${file.name}: ${error.message}`);
            print(`Could not import ${file.name}: ${error.message}`, 'warning');
        }
    });

    $('saves-clear').addEventListener('click', () => {
        if (!confirm(`Delete every save of "${game.name}"? This cannot be undone.`)) {
            return;
        }
        const module = getModule();
        const folder = saveFolder(game.id);
        removeTree(module, folder);
        module.FS.mkdir(folder);
        persist();
        draw();
    });

    return {
        open(which) {
            game = which;
            $('saves-title').textContent = `Saves of ${game.name}`;
            draw();
            dialog.showModal();
        },
    };
}
