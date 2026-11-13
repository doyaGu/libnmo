// Run nmo's web player in Node.js as the page runs it: the game's files
// copied into the player's memory file system (which tells case apart, as a
// browser's does), then main with the given options and the composition.
//
//   node run_node.mjs GAME [--entry PATH] [--profile ID] [--out DIR] -- [player options]
//
// GAME is a folder or a single .cmo/.vmo. The composition is the one the
// page would pick (--entry names another, by its path inside GAME), and
// --profile picks a game profile (profiles.json) the page would otherwise
// guess, with its registry settings at their defaults. Files the player
// writes under /out (--shot /out/menu.png@760) are copied to --out (default:
// the current folder). The exit code is the player's.

import fs from 'node:fs';
import path from 'node:path';
import createNmoPlayer from './nmo-player.mjs';
import { planGame, writeGameFile } from './games.mjs';
import { loadProfiles, registryFor } from './profiles.mjs';

const args = process.argv.slice(2);
const dashes = args.indexOf('--');
const own = dashes >= 0 ? args.slice(0, dashes) : args;
const playerArgs = dashes >= 0 ? args.slice(dashes + 1) : [];
let game = null;
let outFolder = '.';
const options = {};
for (let i = 0; i < own.length; i++) {
    if (own[i] === '--out' && i + 1 < own.length) {
        outFolder = own[++i];
    } else if (own[i] === '--entry' && i + 1 < own.length) {
        options.entry = own[++i];
    } else if (own[i] === '--profile' && i + 1 < own.length) {
        options.profile = own[++i];
    } else if (game === null) {
        game = own[i];
    }
}
if (game === null) {
    console.error('Usage: node run_node.mjs GAME [--entry PATH] [--profile ID] [--out DIR] -- [player options]');
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

await loadProfiles();
const single = fs.statSync(game).isFile();
const folder = single ? path.dirname(game) : game;
const plan = planGame(single ? [path.basename(game)] : listFiles(folder), options);
if (plan === null) {
    console.error(`No .cmo, .vmo or .nmo in ${game}`);
    process.exit(2);
}
const player = await createNmoPlayer({
    print: (text) => console.log(text),
    printErr: (text) => console.error(text),
});
for (const [source, inner] of plan.files) {
    writeGameFile(player, '/game/' + inner, fs.readFileSync(path.join(folder, source)));
}
player.FS.mkdir('/out');
const registry = registryFor(plan.profile);
const registryArgs = registry && !playerArgs.includes('--registry') ? ['--registry', registry] : [];
const code = player.callMain([...registryArgs, '--path', '/game', ...playerArgs, '/game/' + plan.inner]);
fs.mkdirSync(outFolder, { recursive: true });
for (const name of player.FS.readdir('/out')) {
    if (name !== '.' && name !== '..') {
        fs.writeFileSync(path.join(outFolder, name), player.FS.readFile('/out/' + name));
    }
}
process.exit(code);
