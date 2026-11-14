// Zip files without a library. A game's .zip is read straight from its Blob
// (only the directory at its end, then each file the game keeps, so a
// large zip is never in memory whole), ZIP64 included; the browser's own
// DecompressionStream does deflate, the one compression method a zip
// actually needs. The saves' .zip is written stored, uncompressed.

const EOCD_SIGNATURE = 0x06054b50;
const ZIP64_LOCATOR_SIGNATURE = 0x07064b50;
const ZIP64_EOCD_SIGNATURE = 0x06064b50;
const CENTRAL_DIRECTORY_SIGNATURE = 0x02014b50;
const LOCAL_FILE_SIGNATURE = 0x04034b50;
const UTF8_NAMES = 0x0800;
const ZIP64_EXTRA = 0x0001;
const UNICODE_PATH_EXTRA = 0x7075;
const EOCD_SIZE = 22;
const ZIP64_LOCATOR_SIZE = 20;
const LONGEST_COMMENT = 65535;

async function bytesOf(blob, start, end) {
    return new DataView(await blob.slice(start, end).arrayBuffer());
}

function uint64(view, offset) {
    return Number(view.getBigUint64(offset, true));
}

/* The code pages a name without the UTF-8 flag may be in: the one the
   zipping machine likely had (the browser's language says which), then the
   other common ones; Windows-1252 last, which decodes anything */
const LEGACY_ENCODINGS = {
    zh: ['gbk', 'big5'],
    'zh-tw': ['big5', 'gbk'],
    'zh-hk': ['big5', 'gbk'],
    ja: ['shift_jis'],
    ko: ['euc-kr'],
};

function nameEncodings() {
    const language = (globalThis.navigator?.language ?? '').toLowerCase();
    const own = LEGACY_ENCODINGS[language] ?? LEGACY_ENCODINGS[language.split('-')[0]] ?? [];
    return [...new Set(['utf-8', ...own, 'gbk', 'shift_jis', 'big5', 'euc-kr'])];
}

function decodeName(bytes, utf8) {
    if (utf8) {
        return new TextDecoder('utf-8').decode(bytes);
    }
    for (const encoding of nameEncodings()) {
        try {
            return new TextDecoder(encoding, { fatal: true }).decode(bytes);
        } catch {
            /* not this one (or this runtime lacks it): the next */
        }
    }
    return new TextDecoder('windows-1252').decode(bytes);
}

/* A name made a plain relative path, or null when it would leave the folder
   it is unpacked in (an absolute path, a drive, or a ".." part) */
export function safePath(name) {
    const parts = name
        .replace(/\\/g, '/')
        .split('/')
        .filter((part) => part !== '' && part !== '.');
    if (/^([a-z]:|\/)/i.test(name) || parts.some((part) => part === '..' || part.includes(':'))) {
        return null;
    }
    return parts.join('/');
}

/* Where the central directory is and how many entries it holds, ZIP64 or not */
async function findDirectory(blob) {
    const tailStart = Math.max(0, blob.size - EOCD_SIZE - LONGEST_COMMENT - ZIP64_LOCATOR_SIZE);
    const tail = await bytesOf(blob, tailStart, blob.size);
    let eocd = -1;
    for (let i = tail.byteLength - EOCD_SIZE; i >= 0; i--) {
        if (tail.getUint32(i, true) === EOCD_SIGNATURE) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        throw new Error('no end-of-central-directory record (not a zip file?)');
    }
    let count = tail.getUint16(eocd + 10, true);
    let size = tail.getUint32(eocd + 12, true);
    let offset = tail.getUint32(eocd + 16, true);
    const locator = eocd - ZIP64_LOCATOR_SIZE;
    if (locator >= 0 && tail.getUint32(locator, true) === ZIP64_LOCATOR_SIGNATURE) {
        const at = uint64(tail, locator + 8);
        const record = await bytesOf(blob, at, at + 56);
        if (record.getUint32(0, true) !== ZIP64_EOCD_SIGNATURE) {
            throw new Error('the ZIP64 end-of-central-directory record is not where it should be');
        }
        count = uint64(record, 32);
        size = uint64(record, 40);
        offset = uint64(record, 48);
    }
    return { count, size, offset };
}

/* Sizes and offset a ZIP64 extra field holds for those the entry marks 0xFFFFFFFF */
function readZip64Extra(view, start, length, entry) {
    let at = start;
    for (const field of ['size', 'compressedSize', 'localHeaderOffset']) {
        if (entry[field] === 0xffffffff && at + 8 <= start + length) {
            entry[field] = uint64(view, at);
            at += 8;
        }
    }
}

/* Every entry's name and where its data is, from the directory at the end
   of the zip (not the local headers scattered through it, which a
   streamed/appended zip can disagree with) */
export async function readZipEntries(blob) {
    const directory = await findDirectory(blob);
    const view = await bytesOf(blob, directory.offset, directory.offset + directory.size);
    const bytes = new Uint8Array(view.buffer);
    const entries = [];
    let offset = 0;
    for (let i = 0; i < directory.count; i++) {
        if (view.getUint32(offset, true) !== CENTRAL_DIRECTORY_SIGNATURE) {
            throw new Error('a central directory entry is not where it should be');
        }
        const flags = view.getUint16(offset + 8, true);
        const nameLength = view.getUint16(offset + 28, true);
        const extraLength = view.getUint16(offset + 30, true);
        const commentLength = view.getUint16(offset + 32, true);
        const entry = {
            method: view.getUint16(offset + 10, true),
            compressedSize: view.getUint32(offset + 20, true),
            size: view.getUint32(offset + 24, true),
            localHeaderOffset: view.getUint32(offset + 42, true),
            name: decodeName(
                bytes.subarray(offset + 46, offset + 46 + nameLength),
                flags & UTF8_NAMES
            ),
        };
        for (
            let at = offset + 46 + nameLength;
            at + 4 <= offset + 46 + nameLength + extraLength;

        ) {
            const id = view.getUint16(at, true);
            const length = view.getUint16(at + 2, true);
            if (id === ZIP64_EXTRA) {
                readZip64Extra(view, at + 4, length, entry);
            } else if (id === UNICODE_PATH_EXTRA && length > 5) {
                /* Info-ZIP's Unicode path: a version byte, the CRC of the plain name, then UTF-8 */
                entry.name = new TextDecoder('utf-8').decode(
                    bytes.subarray(at + 9, at + 4 + length)
                );
            }
            at += 4 + length;
        }
        entries.push(entry);
        offset += 46 + nameLength + extraLength + commentLength;
    }
    return entries;
}

/* One entry's bytes, decompressed; the local header's own name/extra lengths
   (not the central directory's, which can differ) say where they start */
export async function readZipEntryData(blob, entry) {
    const header = await bytesOf(blob, entry.localHeaderOffset, entry.localHeaderOffset + 30);
    if (header.getUint32(0, true) !== LOCAL_FILE_SIGNATURE) {
        throw new Error(
            `"${entry.name}" has no local file header where its directory entry points`
        );
    }
    const start =
        entry.localHeaderOffset + 30 + header.getUint16(26, true) + header.getUint16(28, true);
    const data = blob.slice(start, start + entry.compressedSize);
    if (entry.method === 0) {
        return new Uint8Array(await data.arrayBuffer());
    }
    if (entry.method === 8) {
        if (typeof DecompressionStream === 'undefined') {
            throw new Error("this browser can't inflate a zip's compressed files");
        }
        const stream = data.stream().pipeThrough(new DecompressionStream('deflate-raw'));
        return new Uint8Array(await new Response(stream).arrayBuffer());
    }
    throw new Error(
        `"${entry.name}" uses zip compression method ${entry.method}, not store or deflate`
    );
}

/*
 * A zip's files: `files` as [path, file-like] pairs (the file-like's
 * arrayBuffer() reads and decompresses on demand), folders left out, and
 * `skipped`, the names left out because they would land outside the
 * folder the zip is unpacked in.
 */
export async function readZip(blob) {
    const files = [];
    const skipped = [];
    for (const entry of await readZipEntries(blob)) {
        if (entry.name.endsWith('/') || entry.name.endsWith('\\')) {
            continue;
        }
        const path = safePath(entry.name);
        if (path === null || path === '') {
            skipped.push(entry.name);
            continue;
        }
        files.push([
            path,
            { size: entry.size, arrayBuffer: async () => readZipEntryData(blob, entry) },
        ]);
    }
    return { files, skipped };
}

let crcTable = null;

function crc32(bytes) {
    if (crcTable === null) {
        crcTable = new Uint32Array(256);
        for (let n = 0; n < 256; n++) {
            let c = n;
            for (let k = 0; k < 8; k++) {
                c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
            }
            crcTable[n] = c >>> 0;
        }
    }
    let crc = 0xffffffff;
    for (let i = 0; i < bytes.length; i++) {
        crc = crcTable[(crc ^ bytes[i]) & 0xff] ^ (crc >>> 8);
    }
    return (crc ^ 0xffffffff) >>> 0;
}

/* A date as a zip's MS-DOS time and date words (local time, two-second steps, from 1980) */
function dosDateTime(date) {
    const year = Math.max(1980, date.getFullYear());
    return {
        time: (date.getHours() << 11) | (date.getMinutes() << 5) | (date.getSeconds() >> 1),
        date: ((year - 1980) << 9) | ((date.getMonth() + 1) << 5) | date.getDate(),
    };
}

/* A .zip of `files` ([path, Uint8Array, Date or ms when written]), stored uncompressed */
export function writeZip(files) {
    const encoder = new TextEncoder();
    const parts = [];
    const directory = [];
    let offset = 0;
    for (const [path, data, written] of files) {
        const name = encoder.encode(path);
        const crc = crc32(data);
        const stamp = dosDateTime(new Date(written ?? Date.now()));
        const local = new DataView(new ArrayBuffer(30));
        local.setUint32(0, LOCAL_FILE_SIGNATURE, true);
        local.setUint16(4, 20, true);
        local.setUint16(6, UTF8_NAMES, true);
        local.setUint16(10, stamp.time, true);
        local.setUint16(12, stamp.date, true);
        local.setUint32(14, crc, true);
        local.setUint32(18, data.length, true);
        local.setUint32(22, data.length, true);
        local.setUint16(26, name.length, true);
        parts.push(local, name, data);

        const central = new DataView(new ArrayBuffer(46));
        central.setUint32(0, CENTRAL_DIRECTORY_SIGNATURE, true);
        central.setUint16(4, 20, true);
        central.setUint16(6, 20, true);
        central.setUint16(8, UTF8_NAMES, true);
        central.setUint16(12, stamp.time, true);
        central.setUint16(14, stamp.date, true);
        central.setUint32(16, crc, true);
        central.setUint32(20, data.length, true);
        central.setUint32(24, data.length, true);
        central.setUint16(28, name.length, true);
        central.setUint32(42, offset, true);
        directory.push(central, name);
        offset += 30 + name.length + data.length;
    }
    if (offset > 0xffffffff || files.length > 0xffff) {
        throw new Error('too much for a zip without ZIP64');
    }
    const directorySize = directory.reduce((sum, part) => sum + part.byteLength, 0);
    const end = new DataView(new ArrayBuffer(EOCD_SIZE));
    end.setUint32(0, EOCD_SIGNATURE, true);
    end.setUint16(8, files.length, true);
    end.setUint16(10, files.length, true);
    end.setUint32(12, directorySize, true);
    end.setUint32(16, offset, true);
    return new Blob([...parts, ...directory, end], { type: 'application/zip' });
}
