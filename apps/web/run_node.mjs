// Run nmo's web player in Node.js as the page runs it: the game folder's
// files copied into the player's memory file system (which tells case apart,
// as a browser's does), then main with the given options and /game/base.cmo.
//
//   node run_node.mjs GAME_FOLDER [--out DIR] -- [player options]
//
// Files the player writes under /out (--shot /out/menu.png@760) are copied
// to --out (default: the current folder). The exit code is the player's.

import fs from 'node:fs';
import path from 'node:path';
import createNmoPlayer from './nmo-player.mjs';
import { selectGameFiles, writeGameFile } from './player.mjs';

const args = process.argv.slice(2);
const dashes = args.indexOf('--');
const own = dashes >= 0 ? args.slice(0, dashes) : args;
const playerArgs = dashes >= 0 ? args.slice(dashes + 1) : [];
let gameFolder = null;
let outFolder = '.';
for (let i = 0; i < own.length; i++) {
    if (own[i] === '--out' && i + 1 < own.length) {
        outFolder = own[++i];
    } else if (gameFolder === null) {
        gameFolder = own[i];
    }
}
if (gameFolder === null) {
    console.error('Usage: node run_node.mjs GAME_FOLDER [--out DIR] -- [player options]');
    process.exit(2);
}

/* Every file under `folder`, as paths relative to it with '/' separators */
function listFiles(folder, prefix = '') {
    const found = [];
    for (const entry of fs.readdirSync(path.join(folder, prefix), { withFileTypes: true })) {
        const relative = prefix === '' ? entry.name : prefix + '/' + entry.name;
        if (entry.isDirectory()) {
            found.push(...listFiles(folder, relative));
        } else if (entry.isFile()) {
            found.push(relative);
        }
    }
    return found;
}

const player = await createNmoPlayer({
    print: (text) => console.log(text),
    printErr: (text) => console.error(text),
});
const selected = selectGameFiles(listFiles(gameFolder));
if (selected.length === 0) {
    console.error(`No base.cmo in ${gameFolder}`);
    process.exit(2);
}
for (const [source, inner] of selected) {
    writeGameFile(player, '/game/' + inner, fs.readFileSync(path.join(gameFolder, source)));
}
player.FS.mkdir('/out');
const code = player.callMain([...playerArgs, '/game/base.cmo']);
fs.mkdirSync(outFolder, { recursive: true });
for (const name of player.FS.readdir('/out')) {
    if (name !== '.' && name !== '..') {
        fs.writeFileSync(path.join(outFolder, name), player.FS.readFile('/out/' + name));
    }
}
process.exit(code);
