// Small helpers the page's modules share for building and showing things.

/*
 * A new element: `props` are its properties (className, textContent, a
 * handler's onclick, ...), except names it has no property for, which
 * become attributes (aria-label, data-*); `children` are appended.
 */
export function element(tag, props = {}, children = []) {
    const node = document.createElement(tag);
    for (const [key, value] of Object.entries(props)) {
        if (key in node) {
            node[key] = value;
        } else {
            node.setAttribute(key, value);
        }
    }
    node.append(...children);
    return node;
}

/* A small button of the page's style that does `action` */
export function smallButton(text, action, className = '') {
    const button = element('button', {
        type: 'button',
        className: `button small ${className}`.trim(),
    });
    button.textContent = text;
    button.addEventListener('click', action);
    return button;
}

/* An SVG icon's `hidden` attribute, set through `toggleAttribute`: some
   engines don't reflect the `hidden` IDL property on SVG elements. */
export function setHidden(node, hidden) {
    node.toggleAttribute('hidden', hidden);
}

export function formatSize(bytes) {
    if (bytes >= 1073741824) {
        return `${(bytes / 1073741824).toFixed(1)} GB`;
    }
    if (bytes >= 1048576) {
        return `${(bytes / 1048576).toFixed(1)} MB`;
    }
    if (bytes >= 1024) {
        return `${Math.round(bytes / 1024)} KB`;
    }
    return `${bytes} B`;
}

/* A file name made safe for a download */
export function fileNameOf(text) {
    return text.replace(/[\\/:*?"<>|]+/g, ' ').trim() || 'nmo';
}

/* Hand `blob` to the browser as a download named `name` */
export function download(name, blob) {
    const url = URL.createObjectURL(blob);
    const link = element('a', { href: url, download: name });
    document.body.append(link);
    link.click();
    link.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
}
