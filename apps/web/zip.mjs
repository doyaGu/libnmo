// Zip files without a library: reading a game's .zip (the browser's own
// DecompressionStream does deflate, the one compression method a zip
// actually needs) and writing the saves' .zip (stored, uncompressed).

const EOCD_SIGNATURE = 0x06054b50;
const CENTRAL_DIRECTORY_SIGNATURE = 0x02014b50;
const LOCAL_FILE_SIGNATURE = 0x04034b50;
const UTF8_NAMES = 0x0800;

/* Every entry's name and where to find its data, from the directory at the
   end of the zip (not the local headers scattered through it, which a
   streamed/appended zip can disagree with). */
export function readZipEntries(buffer) {
    const view = new DataView(buffer);
    const bytes = new Uint8Array(buffer);
    const shortest = 22; /* the EOCD record holds no comment */
    const longestComment = 65535;
    let eocd = -1;
    for (let i = bytes.length - shortest; i >= Math.max(0, bytes.length - shortest - longestComment); i--) {
        if (view.getUint32(i, true) === EOCD_SIGNATURE) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        throw new Error('no end-of-central-directory record (not a zip file?)');
    }
    const entryCount = view.getUint16(eocd + 10, true);
    let offset = view.getUint32(eocd + 16, true);
    const entries = [];
    for (let i = 0; i < entryCount; i++) {
        if (view.getUint32(offset, true) !== CENTRAL_DIRECTORY_SIGNATURE) {
            throw new Error('a central directory entry is not where it should be');
        }
        const flags = view.getUint16(offset + 8, true);
        const method = view.getUint16(offset + 10, true);
        const compressedSize = view.getUint32(offset + 20, true);
        const size = view.getUint32(offset + 24, true);
        const nameLength = view.getUint16(offset + 28, true);
        const extraLength = view.getUint16(offset + 30, true);
        const commentLength = view.getUint16(offset + 32, true);
        const localHeaderOffset = view.getUint32(offset + 42, true);
        const nameBytes = bytes.subarray(offset + 46, offset + 46 + nameLength);
        /* Without the UTF-8 flag the name is in the zipping machine's code
           page; Latin-1 at least keeps every byte as one character. */
        const name = new TextDecoder(flags & UTF8_NAMES ? 'utf-8' : 'latin1').decode(nameBytes);
        entries.push({name: name.replace(/\\/g, '/'), method, compressedSize, size, localHeaderOffset});
        offset += 46 + nameLength + extraLength + commentLength;
    }
    return entries;
}

/* One entry's bytes, decompressed; the local header's own name/extra
   lengths (not the central directory's, which can differ) say where they
   start. */
export async function readZipEntryData(buffer, entry) {
    const view = new DataView(buffer);
    const bytes = new Uint8Array(buffer);
    if (view.getUint32(entry.localHeaderOffset, true) !== LOCAL_FILE_SIGNATURE) {
        throw new Error(`"${entry.name}" has no local file header where its central directory entry points`);
    }
    const nameLength = view.getUint16(entry.localHeaderOffset + 26, true);
    const extraLength = view.getUint16(entry.localHeaderOffset + 28, true);
    const start = entry.localHeaderOffset + 30 + nameLength + extraLength;
    const compressed = bytes.subarray(start, start + entry.compressedSize);
    if (entry.method === 0) {
        return compressed;
    }
    if (entry.method === 8) {
        if (typeof DecompressionStream === 'undefined') {
            throw new Error("this browser can't inflate a zip's compressed files");
        }
        const stream = new Blob([compressed]).stream().pipeThrough(new DecompressionStream('deflate-raw'));
        return new Uint8Array(await new Response(stream).arrayBuffer());
    }
    throw new Error(`"${entry.name}" uses zip compression method ${entry.method}, not store or deflate`);
}

/* A zip's files as [path, file-like] pairs (the file-like's arrayBuffer()
   decompresses on demand), folders left out */
export function zipFiles(buffer) {
    return readZipEntries(buffer)
        .filter((entry) => !entry.name.endsWith('/'))
        .map((entry) => [entry.name, {
            size: entry.size,
            arrayBuffer: async () => readZipEntryData(buffer, entry),
        }]);
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

/* A .zip of `files` ([path, Uint8Array] pairs), stored uncompressed */
export function writeZip(files) {
    const encoder = new TextEncoder();
    const parts = [];
    const directory = [];
    let offset = 0;
    for (const [path, data] of files) {
        const name = encoder.encode(path);
        const crc = crc32(data);
        const local = new DataView(new ArrayBuffer(30));
        local.setUint32(0, LOCAL_FILE_SIGNATURE, true);
        local.setUint16(4, 20, true);
        local.setUint16(6, UTF8_NAMES, true);
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
        central.setUint32(16, crc, true);
        central.setUint32(20, data.length, true);
        central.setUint32(24, data.length, true);
        central.setUint16(28, name.length, true);
        central.setUint32(42, offset, true);
        directory.push(central, name);
        offset += 30 + name.length + data.length;
    }
    const directorySize = directory.reduce((sum, part) => sum + part.byteLength, 0);
    const end = new DataView(new ArrayBuffer(22));
    end.setUint32(0, EOCD_SIGNATURE, true);
    end.setUint16(8, files.length, true);
    end.setUint16(10, files.length, true);
    end.setUint32(12, directorySize, true);
    end.setUint32(16, offset, true);
    return new Blob([...parts, ...directory, end], {type: 'application/zip'});
}
