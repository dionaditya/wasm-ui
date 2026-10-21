// wasm-docx-editor — public entry point (framework-agnostic).
//
//   import { DocEditor, mountDocEditor } from '@dionaditya/wasm-docx-editor';
//
// React users get the same core plus components from
// '@dionaditya/wasm-docx-editor/react'.

export { DocEditor, toBytes } from './editor.js';
export { loadModule, defaultLocateFile } from './loader.js';
export { mountDocEditor, defaultLabels, applyTheme } from './mount.js';

export const version = '0.1.0';
