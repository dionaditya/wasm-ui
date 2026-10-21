// mount.js — the framework-free editor: `mountDocEditor(element, options)`.
//
// Built entirely with DOM APIs (no innerHTML), so it is safe with a strict CSP
// and with untrusted file names. The returned controller is the imperative
// API; the React component is a thin layer over the same DocEditor class.

import { DocEditor } from './editor.js';

/** Toolbar/status text; override per mount with `labels`. */
export const defaultLabels = {
  open: 'Open .docx',
  exportDocx: 'Export .docx',
  exportPdf: 'Export PDF',
  drop: 'Drop a .docx here',
  loading: 'Loading editor…',
  ready: 'Ready',
  error: 'Could not start the editor',
  opened: (n) => `${n} paragraphs`,
};

/** Map a theme object ({bg, accent, radius, …}) onto CSS custom properties. */
export function applyTheme(root, theme) {
  if (!theme) return;
  for (const [key, value] of Object.entries(theme)) {
    if (value == null) continue;
    const name = key.replace(/[A-Z]/g, (m) => `-${m.toLowerCase()}`);
    root.style.setProperty(`--docxed-${name}`, String(value));
  }
}

function el(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text != null) node.textContent = text;
  return node;
}

function button(className, label, text) {
  const b = el('button', className, text);
  b.type = 'button';
  b.title = label;
  b.setAttribute('aria-label', label);
  return b;
}

/**
 * Mount a complete editor (optional toolbar, canvas viewport, status bar).
 *
 * @param {HTMLElement|string} target element or selector
 * @param {Object} [options]
 * @param {File|Blob|ArrayBuffer|Uint8Array|string|URL} [options.file] document to open
 * @param {boolean} [options.toolbar=false]  build the small DOM toolbar
 * @param {boolean} [options.status=false]   build the status line
 * @param {boolean} [options.dropzone=true]  accept files dragged onto the canvas
 * @param {Object} [options.theme]           CSS variable overrides
 * @param {Object} [options.labels]          text overrides
 * @param {string} [options.className]
 * @param {string|number} [options.height='100%']
 * @param {string} [options.wasmUrl] / [options.dataUrl] / [options.moduleOptions]
 * @param {(editor: DocEditor) => void} [options.onReady]
 * @param {(info: Object) => void} [options.onOpen]
 * @param {(kind: 'docx'|'pdf', blob: Blob) => void} [options.onExport]
 * @param {(error: Error) => void} [options.onError]
 * @returns {{root: HTMLElement, canvas: HTMLCanvasElement, editor: DocEditor,
 *            ready: Promise<any>, open: Function, exportDocx: Function,
 *            exportPdf: Function, state: Function, setStatus: Function,
 *            destroy: Function}}
 */
export function mountDocEditor(target, options = {}) {
  const host = typeof target === 'string' ? document.querySelector(target) : target;
  if (!host) throw new Error('wasm-docx-editor: target element not found');

  const opts = {
    toolbar: false,
    status: false,
    dropzone: true,
    height: '100%',
    ...options,
  };
  const labels = { ...defaultLabels, ...(options.labels || {}) };

  const root = el('div', 'docxed-root' + (opts.className ? ' ' + opts.className : ''));
  // A percentage height only resolves when the host has a real height. Rather
  // than chase the chain, pixel sizes are set directly and a 100% mount fills
  // the host's box (absolute inset), so the editor can never inflate the page.
  const fixedHeight = typeof opts.height === 'number' ? `${opts.height}px` : opts.height;
  const fillHost = fixedHeight === '100%';
  if (fillHost) {
    if (getComputedStyle(host).position === 'static') host.style.position = 'relative';
    host.style.overflow = 'hidden';
    root.style.position = 'absolute';
    root.style.inset = '0';
  } else {
    root.style.height = fixedHeight;
  }
  applyTheme(root, opts.theme);
  host.appendChild(root);

  // ── toolbar ────────────────────────────────────────────────────────────────
  let statusEl = null;
  if (opts.toolbar) {
    const bar = el('div', 'docxed-toolbar');
    const openBtn = button('docxed-btn', labels.open, labels.open);
    const docxBtn = button('docxed-btn', labels.exportDocx, labels.exportDocx);
    const pdfBtn = button('docxed-btn', labels.exportPdf, labels.exportPdf);
    const spacer = el('span', 'docxed-spacer');
    bar.append(openBtn, docxBtn, pdfBtn, spacer);
    if (opts.status) {
      statusEl = el('span', 'docxed-status', labels.loading);
      bar.append(statusEl);
    }
    root.append(bar);

    openBtn.addEventListener('click', () => editor.pickDocx((f) => {
      editor.open(f, { filename: f.name })
        .then((info) => { opts.onOpen && opts.onOpen(info); setStatus(labels.opened(info.paragraphs)); })
        .catch(report);
    }));
    docxBtn.addEventListener('click', () => {
      editor.exportDocx().then((b) => opts.onExport && opts.onExport('docx', b)).catch(report);
    });
    pdfBtn.addEventListener('click', () => {
      editor.exportPdf().then((b) => opts.onExport && opts.onExport('pdf', b)).catch(report);
    });
  } else if (opts.status) {
    statusEl = el('div', 'docxed-status docxed-status--bar', labels.loading);
    root.append(statusEl);
  }

  // ── viewport + canvas ─────────────────────────────────────────────────────
  const viewport = el('div', 'docxed-viewport');
  const canvas = document.createElement('canvas');
  // Emscripten's SDL looks the element up by id, so this one matters.
  canvas.id = 'canvas';
  canvas.className = 'docxed-canvas';
  canvas.tabIndex = -1;
  canvas.addEventListener('contextmenu', (e) => e.preventDefault());
  viewport.append(canvas);

  // Hidden input: the mobile keyboard and IME need a real DOM target.
  const kb = document.createElement('input');
  kb.type = 'text';
  kb.autocomplete = 'off';
  kb.autocorrect = 'off';
  kb.setAttribute('autocapitalize', 'none');
  kb.spellcheck = false;
  kb.className = 'docxed-kb';
  kb.setAttribute('aria-hidden', 'true');

  // The C++ Insert ▸ Image button clicks this by id.
  const imgInput = document.createElement('input');
  imgInput.type = 'file';
  imgInput.accept = 'image/*';
  imgInput.id = 'img-file-input';
  imgInput.className = 'docxed-hidden-input';

  root.append(viewport, kb, imgInput);

  const setStatus = (text) => { if (statusEl) statusEl.textContent = text; };
  const report = (err) => {
    console.error(err);
    setStatus(err && err.message ? err.message : String(err));
    if (opts.onError) opts.onError(err);
  };

  const editor = new DocEditor(canvas, opts);
  editor.bindInput(kb);
  editor.bindPointer(canvas);

  imgInput.addEventListener('change', (e) => {
    const f = e.target.files && e.target.files[0];
    e.target.value = '';
    if (f) editor.insertImage(f).catch(report);
  });

  // ── drag & drop ───────────────────────────────────────────────────────────
  let dragDepth = 0;
  let overlay = null;
  if (opts.dropzone) {
    overlay = el('div', 'docxed-drop', labels.drop);
    viewport.append(overlay);
    viewport.addEventListener('dragenter', (e) => {
      e.preventDefault();
      dragDepth++;
      overlay.classList.add('docxed-drop--active');
    });
    viewport.addEventListener('dragover', (e) => e.preventDefault());
    viewport.addEventListener('dragleave', () => {
      if (--dragDepth <= 0) { dragDepth = 0; overlay.classList.remove('docxed-drop--active'); }
    });
    viewport.addEventListener('drop', (e) => {
      e.preventDefault();
      dragDepth = 0;
      overlay.classList.remove('docxed-drop--active');
      const f = e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0];
      if (!f) return;
      if (/\.(png|jpe?g|gif|webp|bmp)$/i.test(f.name)) {
        editor.insertImage(f).catch(report);
      } else {
        editor.open(f, { filename: f.name })
          .then((info) => { opts.onOpen && opts.onOpen(info); setStatus(labels.opened(info.paragraphs)); })
          .catch(report);
      }
    });
  }

  // The canvas must track its container, not the window: the stylesheet keeps
  // it at 100% of the viewport and emscripten re-measures on the resize event.
  // A host with no height at all (auto, empty) gets a usable fallback so the
  // editor is still visible instead of collapsing to nothing.
  const followHost = () => {
    if (fillHost && host.clientHeight < 40) host.style.height = '560px';
    const box = viewport.getBoundingClientRect();
    const dpr = window.devicePixelRatio || 1;
    // SDL sizes the drawing buffer itself and can get it wrong when the canvas
    // is stretched by CSS; set it from the viewport and let emscripten re-read.
    if (box.width > 0 && box.height > 0) {
      canvas.width = Math.max(1, Math.round(box.width * dpr));
      canvas.height = Math.max(1, Math.round(box.height * dpr));
    }
    if (!editor.destroyed) window.dispatchEvent(new Event('resize'));
  };
  followHost();

  let ro = null;
  if (typeof ResizeObserver !== 'undefined') {
    ro = new ResizeObserver(followHost);
    ro.observe(host);
  }

  const ready = editor.ready
    .then(() => {
      setStatus(labels.ready);
      if (opts.file) {
        return editor.open(opts.file).then((info) => {
          opts.onOpen && opts.onOpen(info);
          setStatus(labels.opened(info.paragraphs));
          opts.onReady && opts.onReady(editor);
          return info;
        });
      }
      opts.onReady && opts.onReady(editor);
      return null;
    })
    .catch((err) => { report(err); return null; });

  return {
    root,
    canvas,
    editor,
    ready,
    open: (source, o) => editor.open(source, o),
    exportDocx: (name) => editor.exportDocx(name),
    exportPdf: (name) => editor.exportPdf(name),
    state: () => editor.state(),
    setStatus,
    destroy() {
      editor.destroy();
      if (ro) ro.disconnect();
      root.remove();
    },
  };
}
