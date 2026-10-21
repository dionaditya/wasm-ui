// editor.js — one document editor instance on one canvas.
//
// Framework-agnostic: it owns the wasm module, the input bridge (ImGui reads
// the keyboard through a hidden <input>), the file handoff and the exporters.
// mountDocEditor() and the React component are thin layers over this class.

import { loadModule, defaultLocateFile } from './loader.js';

const DOCX_MIME = 'application/vnd.openxmlformats-officedocument.wordprocessingml.document';

/** Read any of File/Blob/ArrayBuffer/TypedArray/URL/string into bytes. */
export async function toBytes(source) {
  if (source == null) throw new TypeError('wasm-docx-editor: no document given');
  if (source instanceof Uint8Array) return source;
  if (source instanceof ArrayBuffer) return new Uint8Array(source);
  if (typeof source === 'string' || source instanceof URL) {
    const res = await fetch(source);
    if (!res.ok) throw new Error(`wasm-docx-editor: HTTP ${res.status} for ${source}`);
    return new Uint8Array(await res.arrayBuffer());
  }
  if (typeof Blob !== 'undefined' && source instanceof Blob) {
    return new Uint8Array(await source.arrayBuffer());
  }
  if (source && typeof source.arrayBuffer === 'function') {
    return new Uint8Array(await source.arrayBuffer());
  }
  throw new TypeError('wasm-docx-editor: unsupported document source');
}

function download(blob, filename) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  a.rel = 'noopener';
  a.style.display = 'none';
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  setTimeout(() => URL.revokeObjectURL(url), 1000);
}

// ── keyboard bridge ---------------------------------------------------------
// ImGui never sees DOM events; the shell feeds it through these exports. A
// hidden input is what keeps the mobile keyboard open and gives IME/paste a
// real target.
const KEY_MAP = {
  Backspace: 8, Enter: 13, Tab: 9, Escape: 27,
  ArrowLeft: 37, ArrowRight: 39, ArrowUp: 38, ArrowDown: 40,
  Delete: 46, Home: 36, End: 35, PageUp: 33, PageDown: 34,
  '.': 190, ',': 188, '-': 189, '=': 187,
};
const MOD_MAP = {
  ShiftLeft: 16, ShiftRight: 16, ControlLeft: 17, ControlRight: 17,
  AltLeft: 18, AltRight: 18, MetaLeft: 91, MetaRight: 93,
};

function keyCode(ev) {
  if (Object.prototype.hasOwnProperty.call(KEY_MAP, ev.key)) return KEY_MAP[ev.key];
  const code = ev.code || '';
  if (MOD_MAP[code] !== undefined) return MOD_MAP[code];
  if (/^Key[A-Z]$/.test(code)) return code.charCodeAt(3);
  if (/^Digit[0-9]$/.test(code)) return code.charCodeAt(5);
  return 0;
}

export class DocEditor {
  /**
   * @param {HTMLElement} canvas the canvas SDL renders into
   * @param {Object} [options] loader options (wasmUrl, dataUrl, moduleOptions, factory)
   */
  constructor(canvas, options = {}) {
    this.canvas = canvas;
    this.options = options;
    this.module = null;
    this.destroyed = false;
    /** Resolves once the C++ main loop is up and the document model answers. */
    this.ready = this._boot();
    this.ready.catch(() => { /* surfaced by the caller / onError */ });
  }

  async _boot() {
    const Module = await loadModule(this.canvas, this.options);
    if (this.destroyed) return null;
    this.module = Module;

    // SDL forces inline pixel sizes on the canvas when it creates the window;
    // the stylesheet keeps it at 100% of the viewport, so ask emscripten to
    // re-measure once it is up.
    this._kick = () => {
      if (!this.destroyed) window.dispatchEvent(new Event('resize'));
    };
    this._kick();
    setTimeout(this._kick, 120);
    setTimeout(this._kick, 600);

    // Everything the C++ side calls back into.
    const prev = {
      pick: globalThis.docPickDocx,
      math: globalThis.renderMath,
      diagram: globalThis.renderDiagram,
    };
    this._prevGlobals = prev;
    globalThis.docPickDocx = () => this.pickDocx();
    globalThis.renderMath = (tex) => this.insertMath(tex);
    globalThis.renderDiagram = (kind, src) => this.insertDiagram(kind, src);

    await this._waitForApp();
    return Module;
  }

  /** The C++ app is up once its debug hook returns the document model. */
  async _waitForApp(timeoutMs = 30000) {
    const t0 = Date.now();
    for (;;) {
      if (this.destroyed) return;
      try {
        const s = this.module.ccall('doc_debug_state', 'string', [], []);
        if (s && s.indexOf('"paras"') >= 0) return;
      } catch (e) { /* runtime not ready yet */ }
      if (Date.now() - t0 > timeoutMs) {
        throw new Error('wasm-docx-editor: the editor did not start (wasm/GL)');
      }
      await new Promise((r) => setTimeout(r, 50));
    }
  }

  // ── files ─────────────────────────────────────────────────────────────────
  /** Open a .docx from bytes, a File, or a URL. Resolves with the model summary. */
  async open(source, opts = {}) {
    await this.ready;
    const bytes = await toBytes(source);
    const { module } = this;
    const ptr = module._malloc(bytes.length);
    if (!ptr) throw new Error('wasm-docx-editor: out of memory');
    let n;
    try {
      module.HEAPU8.set(bytes, ptr);
      n = module.ccall('doc_import_docx', 'number', ['number', 'number'], [ptr, bytes.length]);
    } finally {
      module._free(ptr);
    }
    if (n < 0) {
      throw new Error(`wasm-docx-editor: could not read this file (code ${n});` +
                      ' it must be a modern Word/OOXML .docx');
    }
    this.filename = opts.filename || this.filename;
    await this._decodeImages();
    await this._renderImportedMath();
    this._kick();
    return { paragraphs: n, images: this._imageCount(), name: this.filename || null };
  }

  /** .docx bytes → Blob. */
  async toDocxBlob() {
    await this.ready;
    const { module } = this;
    const size = module.ccall('doc_export_docx_size', 'number', [], []);
    if (!size) throw new Error('wasm-docx-editor: docx export produced nothing');
    const ptr = module.ccall('doc_export_docx_data', 'number', [], []);
    const bytes = module.HEAPU8.slice(ptr, ptr + size);
    return new Blob([bytes], { type: DOCX_MIME });
  }

  /** PDF bytes → Blob (generated natively, base-14 fonts). */
  async toPdfBlob() {
    await this.ready;
    const { module } = this;
    const size = module.ccall('doc_export_pdf_size', 'number', [], []);
    if (!size) throw new Error('wasm-docx-editor: pdf export produced nothing');
    const ptr = module.ccall('doc_export_pdf_data', 'number', [], []);
    const bytes = module.HEAPU8.slice(ptr, ptr + size);
    return new Blob([bytes], { type: 'application/pdf' });
  }

  /** Download helpers — what the in-app ribbon buttons do. */
  async exportDocx(filename = 'document.docx') {
    const blob = await this.toDocxBlob();
    download(blob, filename);
    return blob;
  }

  async exportPdf(filename = 'document.pdf') {
    const blob = await this.toPdfBlob();
    download(blob, filename);
    return blob;
  }

  // ── model ─────────────────────────────────────────────────────────────────
  /** The editor's own debug/state snapshot (paragraphs, caret, pages, lines…). */
  state() {
    if (!this.module) return null;
    try {
      const json = this.module.ccall('doc_debug_state', 'string', [], []);
      return json ? JSON.parse(json) : null;
    } catch (e) {
      return null;
    }
  }

  /** Paragraph count of the open document. */
  paragraphCount() {
    const s = this.state();
    return s ? s.paras : 0;
  }

  paste(text) {
    if (!this.module || !text) return;
    this.module.ccall('doc_paste_text', null, ['string'], [text]);
  }

  // formatting (mirrors the ribbon, handy for scripted tests and integrations)
  setParagraphFormat({ align = -1, spaceBefore = -1, spaceAfter = -1, line = -1 } = {}) {
    this._call('doc_debug_para_fmt', [align, spaceBefore, spaceAfter, line]);
  }

  setPageColumns(page, columns, gap = 12) {
    this._call('doc_debug_page_columns', [page, columns, gap], true);
  }

  setPageSetup(page, { landscape = false, ml = 72, mr = 72, mt = 72, mb = 72,
                       header = true, footer = true } = {}) {
    this._call('doc_debug_page_setup',
      [page, landscape ? 1 : 0, ml, mr, mt, mb, header ? 1 : 0, footer ? 1 : 0], true);
  }

  setHeaderFooter({ header, footer, headerDistance = -1, footerDistance = -1 } = {}) {
    this._call('doc_debug_hf', [header || null, footer || null, headerDistance, footerDistance], true);
  }

  addTabStop(index, xPt, leader = 0) {
    this._call('doc_debug_tab', [index, xPt, leader], true);
  }

  /** op codes are the editor's own formatting operations (see docs/PLAN.md). */
  apply(op, rgba = [0, 0, 0, 1]) {
    const [r, g, b, a] = rgba;
    this._call('doc_debug_apply', [op, r, g, b, a], true);
  }

  _call(name, args, relayout = false) {
    if (!this.module) return;
    const types = args.map((a) => (typeof a === 'string' || a === null ? 'string' : 'number'));
    this.module.ccall(name, null, types, args);
    if (relayout) this._kick();
  }

  // ── images / math / diagrams ──────────────────────────────────────────────
  _imageCount() {
    if (!this.module) return 0;
    return this.module.ccall('doc_imported_image_count', 'number', [], []);
  }

  /** Decode the media embedded in an imported .docx and hand RGBA to C++. */
  async _decodeImages() {
    const { module } = this;
    const count = this._imageCount();
    for (let i = 0; i < count; ++i) {
      try {
        const para = module.ccall('doc_imported_image_para', 'number', ['number'], [i]);
        const path = module.ccall('doc_imported_image_path', 'string', ['number'], [i]);
        if (para < 0 || !path) continue;
        const bytes = module.FS.readFile(path);
        const bmp = await createImageBitmap(new Blob([bytes]));
        const c = document.createElement('canvas');
        c.width = bmp.width; c.height = bmp.height;
        c.getContext('2d').drawImage(bmp, 0, 0);
        const data = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
        this._attachImage(para, data, c.width, c.height);
        if (bmp.close) bmp.close();
      } catch (e) {
        console.warn('wasm-docx-editor: image ' + i + ' failed', e);
      }
    }
  }

  /** Imported display equations arrive as LaTeX; rasterize them with MathJax. */
  async _renderImportedMath() {
    const { module } = this;
    const n = module.ccall('doc_imported_math_count', 'number', [], []);
    for (let i = 0; i < n; ++i) {
      try {
        const para = module.ccall('doc_imported_math_para', 'number', ['number'], [i]);
        const tex = module.ccall('doc_imported_math_tex', 'string', ['number'], [i]);
        if (para < 0 || !tex) continue;
        const raster = await this._texToRaster(tex);
        if (raster) this._attachImage(para, raster.data, raster.width, raster.height);
      } catch (e) {
        console.warn('wasm-docx-editor: equation ' + i + ' failed', e);
      }
    }
  }

  _attachImage(para, rgba, width, height) {
    const { module } = this;
    const ptr = module._malloc(rgba.length);
    if (!ptr) return;
    module.HEAPU8.set(rgba, ptr);
    module.ccall('doc_attach_image', null,
      ['number', 'number', 'number', 'number'], [para, ptr, width, height]);
    module._free(ptr);
  }

  async _mathJax() {
    if (!globalThis.MathJax || !MathJax.tex2svgPromise) {
      await new Promise((resolve, reject) => {
        const sc = document.createElement('script');
        sc.src = 'https://cdn.jsdelivr.net/npm/mathjax@3/es5/tex-svg.js';
        sc.onload = resolve;
        sc.onerror = () => reject(new Error('could not load MathJax'));
        document.head.appendChild(sc);
      });
      if (MathJax.startup && MathJax.startup.promise) await MathJax.startup.promise;
    }
    return MathJax;
  }

  /** LaTeX → RGBA raster (white background, no fonts to install). */
  async _texToRaster(tex, { em = 16, scale = 1 } = {}) {
    const MathJax = await this._mathJax();
    const node = await MathJax.tex2svgPromise(tex, { display: true });
    const svgEl = node.querySelector('svg');
    if (!svgEl) throw new Error('MathJax returned no SVG');
    const svg = new XMLSerializer().serializeToString(svgEl);
    const vb = /viewBox="([-\d.]+) ([-\d.]+) ([-\d.]+) ([-\d.]+)"/.exec(svg);
    // MathJax viewBox units are 1/1000 em
    const w = vb ? Math.max(8, Math.round((parseFloat(vb[3]) / 1000) * em * scale)) : 320;
    const h = vb ? Math.max(8, Math.round((parseFloat(vb[4]) / 1000) * em * scale)) : 70;
    return this._svgToRaster(svg, w, h);
  }

  async _svgToRaster(svgText, width, height) {
    const img = new Image();
    img.src = 'data:image/svg+xml;charset=utf-8,' + encodeURIComponent(svgText);
    await new Promise((res, rej) => { img.onload = res; img.onerror = rej; });
    const c = document.createElement('canvas');
    c.width = width; c.height = height;
    const ctx = c.getContext('2d');
    ctx.fillStyle = '#ffffff';
    ctx.fillRect(0, 0, width, height);
    ctx.drawImage(img, 0, 0, width, height);
    return { data: ctx.getImageData(0, 0, width, height).data, width, height };
  }

  /** Insert a LaTeX formula at the caret (also reachable from the Insert tab). */
  async insertMath(tex) {
    await this.ready;
    const raster = await this._texToRaster(tex);
    this._receiveImage(raster.data, raster.width, raster.height);
    return { width: raster.width, height: raster.height };
  }

  /** Insert an image File from disk at the caret. */
  async insertImage(file) {
    await this.ready;
    const bmp = await createImageBitmap(file);
    const c = document.createElement('canvas');
    c.width = bmp.width; c.height = bmp.height;
    c.getContext('2d').drawImage(bmp, 0, 0);
    const data = c.getContext('2d').getImageData(0, 0, c.width, c.height).data;
    this._receiveImage(data, c.width, c.height);
    if (bmp.close) bmp.close();
    return { width: c.width, height: c.height };
  }

  _receiveImage(rgba, width, height) {
    const { module } = this;
    const ptr = module._malloc(rgba.length);
    if (!ptr) return;
    module.HEAPU8.set(rgba, ptr);
    module.ccall('doc_receive_image', null, ['number', 'number', 'number'], [ptr, width, height]);
    module._free(ptr);
  }

  /**
   * Diagrams: kind 0 = Mermaid, 1 = Graphviz DOT, loaded from a CDN on demand.
   * Called by the C++ Insert-diagram dialog through `window.renderDiagram`.
   */
  async insertDiagram(kind, src) {
    await this.ready;
    let svgText = '';
    let scale = 1.6;
    if (Number(kind) === 0) {
      const mermaid = (await import('https://cdn.jsdelivr.net/npm/mermaid@11/dist/mermaid.esm.min.mjs')).default;
      mermaid.initialize({
        startOnLoad: false, theme: 'default', securityLevel: 'loose',
        flowchart: { htmlLabels: false, useMaxWidth: false },
      });
      const res = await mermaid.render('dgm' + Date.now(), src);
      svgText = res.svg;
      scale = 2;
    } else {
      const vizMod = await import('https://cdn.jsdelivr.net/npm/@viz-js/viz@3/lib/viz-standalone.mjs');
      const viz = await vizMod.instance();
      svgText = viz.renderString(src, { format: 'svg', engine: 'dot' });
      scale = 1.6;
    }
    const size = this._svgIntrinsicSize(svgText);
    const w = Math.max(1, Math.round(size.w * (scale === 2 ? 2 : 1.4)));
    const h = Math.max(1, Math.round(size.h * (scale === 2 ? 2 : 1.4)));
    const padded = /<svg[^>]*\swidth=/.test(svgText) ? svgText
      : svgText.replace(/<svg([^>]*)>/, (m, a) =>
          `<svg${a} width="${size.w}" height="${size.h}" xmlns="http://www.w3.org/2000/svg">`);
    const raster = await this._svgToRaster(padded, w, h);
    this._receiveImage(raster.data, raster.width, raster.height);
    return { width: w, height: h };
  }

  _svgIntrinsicSize(svgText) {
    const vb = /viewBox="([-\d.]+)[ ,]+([-\d.]+)[ ,]+([\d.]+)[ ,]+([\d.]+)"/.exec(svgText);
    const wPx = /<svg[^>]*\swidth="([\d.]+)(px)?"(?=[\s>/])/.exec(svgText);
    const hPx = /<svg[^>]*\sheight="([\d.]+)(px)?"(?=[\s>/])/.exec(svgText);
    if (wPx && hPx) return { w: parseFloat(wPx[1]), h: parseFloat(hPx[1]) };
    if (vb) return { w: parseFloat(vb[3]), h: parseFloat(vb[4]) };
    return { w: 900, h: 600 };
  }

  // ── input bridge ──────────────────────────────────────────────────────────
  /** Attach the hidden input that owns the keyboard for this instance. */
  bindInput(input) {
    this.input = input;
    const send = (code, down) =>
      this.module && this.module.ccall('imgui_add_key', null, ['number', 'number'], [code, down ? 1 : 0]);

    this._onInput = (e) => {
      const v = input.value;
      input.value = '';
      for (let i = 0; i < v.length; i++) {
        let c = v.charCodeAt(i);
        if (c >= 0xd800 && c <= 0xdbff && i + 1 < v.length) {
          const c2 = v.charCodeAt(i + 1);
          if (c2 >= 0xdc00 && c2 <= 0xdfff) {
            c = 0x10000 + ((c - 0xd800) << 10) + (c2 - 0xdc00);
            i++;
          }
        }
        this.module.ccall('imgui_add_char', null, ['number'], [c]);
      }
      e.stopPropagation();
    };
    this._onKeyDown = (e) => {
      if (e.isComposing) return;              // let the IME finish
      const code = keyCode(e);
      if (!code) return;
      send(code, true);                       // ImGui repeats held keys itself
      if (e.ctrlKey || e.metaKey || code === 9) e.preventDefault();
    };
    this._onKeyUp = (e) => {
      const code = keyCode(e);
      if (code) send(code, false);
    };
    this._onPaste = (e) => {
      const cd = e.clipboardData || window.clipboardData;
      if (!cd) return;
      const t = cd.getData('text');
      if (!t) return;
      e.preventDefault();
      this.module.ccall('doc_paste_text', null, ['string'], [t]);
    };
    this._onBlur = () => send(-1, 0);

    input.addEventListener('input', this._onInput);
    input.addEventListener('keydown', this._onKeyDown);
    input.addEventListener('keyup', this._onKeyUp);
    input.addEventListener('paste', this._onPaste);
    input.addEventListener('blur', this._onBlur);
    window.addEventListener('blur', this._onBlur);

    // Hand the keyboard back whenever ImGui stops wanting text input.
    this._kbTimer = setInterval(() => {
      if (document.activeElement !== input) return;
      let want = 0;
      try { want = this.module.ccall('imgui_want_text_input', 'number', [], []); } catch (e) { return; }
      if (!want) input.blur();
    }, 200);
  }

  /** Let a pointer gesture on the canvas take the keyboard. */
  bindPointer(canvas) {
    this._onDown = () => this.input && this.input.focus();
    this._onTouch = () => this.input && this.input.focus();
    canvas.addEventListener('mousedown', this._onDown);
    canvas.addEventListener('touchend', this._onTouch, { passive: true });
  }

  // ── pickers ───────────────────────────────────────────────────────────────
  /** Open the OS file picker for a .docx (the ribbon's "Open .docx" calls this). */
  pickDocx(onPicked) {
    const inp = document.createElement('input');
    inp.type = 'file';
    inp.accept = '.docx,' + DOCX_MIME;
    inp.style.position = 'fixed';
    inp.style.left = '-9999px';
    document.body.appendChild(inp);
    inp.addEventListener('change', () => {
      const f = inp.files && inp.files[0];
      document.body.removeChild(inp);
      if (!f) return;
      if (onPicked) onPicked(f);
      else this.open(f, { filename: f.name }).catch((e) => console.error(e));
    });
    inp.click();
  }

  destroy() {
    if (this.destroyed) return;
    this.destroyed = true;
    clearInterval(this._kbTimer);
    if (this.input) {
      this.input.removeEventListener('input', this._onInput);
      this.input.removeEventListener('keydown', this._onKeyDown);
      this.input.removeEventListener('keyup', this._onKeyUp);
      this.input.removeEventListener('paste', this._onPaste);
      this.input.removeEventListener('blur', this._onBlur);
    }
    window.removeEventListener('blur', this._onBlur);
    if (this.canvas) {
      this.canvas.removeEventListener('mousedown', this._onDown);
      this.canvas.removeEventListener('touchend', this._onTouch);
    }
    if (this._prevGlobals) {
      globalThis.docPickDocx = this._prevGlobals.pick;
      globalThis.renderMath = this._prevGlobals.math;
      globalThis.renderDiagram = this._prevGlobals.diagram;
    }
  }
}

export { defaultLocateFile };
