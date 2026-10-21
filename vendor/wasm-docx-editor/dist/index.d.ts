// Type definitions for wasm-docx-editor (core, framework-agnostic).

export type Source =
  | ArrayBuffer
  | Uint8Array
  | Blob
  | File
  | string
  | URL
  | Response
  | { arrayBuffer(): Promise<ArrayBuffer> };

export interface ModuleOptions {
  /** URL of docx-editor.wasm; defaults to the file next to the JS glue. */
  wasmUrl?: string;
  /** URL of docx-editor.data (the .docx template); defaults next to the glue. */
  dataUrl?: string;
  /** Pre-fetched wasm bytes, for CSP or offline use. */
  wasmBinary?: ArrayBuffer | Uint8Array;
  /** Extra fields passed to the emscripten factory. */
  moduleOptions?: Record<string, any>;
  /** A different emscripten factory (advanced). */
  factory?: (options: any) => Promise<any>;
}

export interface OpenInfo {
  /** Number of paragraphs the importer produced. */
  paragraphs: number;
  /** Images embedded in the package that were decoded and attached. */
  images: number;
  /** File name when one was known. */
  name: string | null;
}

export interface DocState {
  paras: number;
  caret: [number, number];
  anchor: [number, number];
  sel: number;
  lo: number;
  hi: number;
  focus: number;
  hf: number;
  hfpage: number;
  hdr: string;
  ftr: string;
  pages: number;
  cols: number[];
  selText: string;
  lines: number[];
  lists: number[];
  dump: { n: number; r: number; s: number; al: number; sb: number; sa: number;
          ls: number; tb: number; t: string }[];
  [key: string]: any;
}

/**
 * One editor on one canvas. The wasm module owns an SDL window and a GL
 * context, so an instance is not shareable between containers.
 */
export class DocEditor {
  constructor(canvas: HTMLCanvasElement, options?: ModuleOptions);
  readonly canvas: HTMLCanvasElement;
  readonly module: any;
  /** Resolves once the C++ main loop answers. */
  readonly ready: Promise<any>;
  filename?: string;

  open(source: Source, opts?: { filename?: string }): Promise<OpenInfo>;
  toDocxBlob(): Promise<Blob>;
  toPdfBlob(): Promise<Blob>;
  exportDocx(filename?: string): Promise<Blob>;
  exportPdf(filename?: string): Promise<Blob>;

  state(): DocState | null;
  paragraphCount(): number;
  paste(text: string): void;

  setParagraphFormat(o?: { align?: number; spaceBefore?: number; spaceAfter?: number; line?: number }): void;
  setPageColumns(page: number, columns: number, gap?: number): void;
  setPageSetup(page: number, o?: {
    landscape?: boolean; ml?: number; mr?: number; mt?: number; mb?: number;
    header?: boolean; footer?: boolean;
  }): void;
  setHeaderFooter(o?: { header?: string; footer?: string;
                        headerDistance?: number; footerDistance?: number }): void;
  addTabStop(index: number, xPt: number, leader?: number): void;
  apply(op: number, rgba?: [number, number, number, number]): void;

  insertMath(tex: string): Promise<{ width: number; height: number }>;
  insertImage(file: File | Blob): Promise<{ width: number; height: number }>;
  insertDiagram(kind: 0 | 1, src: string): Promise<{ width: number; height: number }>;

  bindInput(input: HTMLInputElement): void;
  bindPointer(canvas: HTMLCanvasElement): void;
  pickDocx(onPicked?: (file: File) => void): void;
  destroy(): void;
}

export function toBytes(source: Source): Promise<Uint8Array>;
export function loadModule(canvas: HTMLCanvasElement, options?: ModuleOptions): Promise<any>;
export function defaultLocateFile(file: string): string;

export interface ViewerLabels {
  open: string;
  exportDocx: string;
  exportPdf: string;
  drop: string;
  loading: string;
  ready: string;
  error: string;
  opened: (n: number) => string;
}

export interface MountOptions extends ModuleOptions {
  file?: Source;
  /** Build the small DOM toolbar (open / export). Default false. */
  toolbar?: boolean;
  /** Build a status line. Default false. */
  status?: boolean;
  /** Accept files dropped on the canvas. Default true. */
  dropzone?: boolean;
  /** CSS height of the root (number = px). Default '100%'. */
  height?: string | number;
  /** CSS variable overrides (bg, surface, border, text, muted, accent, radius). */
  theme?: Record<string, string>;
  labels?: Partial<ViewerLabels>;
  className?: string;
  onReady?: (editor: DocEditor) => void;
  onOpen?: (info: OpenInfo) => void;
  onExport?: (kind: 'docx' | 'pdf', blob: Blob) => void;
  onError?: (error: Error) => void;
}

export interface MountController {
  root: HTMLElement;
  canvas: HTMLCanvasElement;
  editor: DocEditor;
  ready: Promise<OpenInfo | null>;
  open(source: Source, opts?: { filename?: string }): Promise<OpenInfo>;
  exportDocx(filename?: string): Promise<Blob>;
  exportPdf(filename?: string): Promise<Blob>;
  state(): DocState | null;
  setStatus(text: string): void;
  destroy(): void;
}

export function mountDocEditor(target: HTMLElement | string, options?: MountOptions): MountController;
export function applyTheme(root: HTMLElement, theme?: Record<string, string>): void;
export const defaultLabels: ViewerLabels;
export const version: string;
