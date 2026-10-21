// loader.js — instantiate the WebAssembly module.
//
// Unlike a pure renderer, this app owns an SDL window and a GL context, so one
// wasm instance backs exactly one editor. `loadModule()` therefore builds a new
// instance per call (no singleton); the editor class created it for you.
//
// The emscripten factory is imported statically so bundlers (Vite, webpack,
// esbuild, Rollup) resolve both the glue and `docx-editor.wasm` next to it.

import createDocxEditorModule from './wasm/docx-editor.mjs';

/** Default URL of the wasm/data pair, resolved next to the glue module. */
export function defaultLocateFile(file) {
  return new URL(`./wasm/${file}`, import.meta.url).href;
}

/**
 * @typedef {Object} ModuleOptions
 * @property {string} [wasmUrl]       URL of docx-editor.wasm (overrides the default).
 * @property {string} [dataUrl]       URL of docx-editor.data (the .docx template).
 * @property {ArrayBuffer|Uint8Array} [wasmBinary] Pre-fetched wasm bytes.
 * @property {Object} [moduleOptions] Extra emscripten Module fields.
 * @property {Function} [factory]     A different emscripten factory (advanced).
 */

/**
 * Boot one emscripten module bound to `canvas`.
 *
 * @param {HTMLCanvasElement} canvas the element SDL renders into
 * @param {ModuleOptions} [options]
 * @returns {Promise<Object>} the emscripten Module
 */
export async function loadModule(canvas, options = {}) {
  const moduleOptions = { canvas, ...(options.moduleOptions || {}) };

  if (options.wasmBinary) moduleOptions.wasmBinary = options.wasmBinary;
  if (options.wasmUrl || options.dataUrl) {
    moduleOptions.locateFile = (file) => {
      if (file.endsWith('.wasm') && options.wasmUrl) return options.wasmUrl;
      if (file.endsWith('.data') && options.dataUrl) return options.dataUrl;
      return defaultLocateFile(file);
    };
  } else if (!moduleOptions.locateFile) {
    moduleOptions.locateFile = defaultLocateFile;
  }

  const factory = options.factory || createDocxEditorModule;
  const Module = await factory(moduleOptions);
  if (typeof Module.ccall !== 'function') {
    throw new Error('wasm-docx-editor: module loaded without the ccall runtime');
  }
  return Module;
}
