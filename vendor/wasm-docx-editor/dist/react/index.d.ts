// Type definitions for wasm-docx-editor/react.

import type { CSSProperties, ReactNode, Ref } from 'react';
import type { DocEditor, DocState, MountOptions, OpenInfo } from '../index';

export interface DocEditorHandle {
  readonly editor: DocEditor | null;
  open: (source: MountOptions['file'], opts?: { filename?: string }) => Promise<OpenInfo>;
  exportDocx: (filename?: string) => Promise<Blob>;
  exportPdf: (filename?: string) => Promise<Blob>;
  state: () => DocState | null;
}

export interface DocEditorProps extends MountOptions {
  /** Show the built-in DOM toolbar (open / export). Default true. */
  toolbar?: boolean | ReactNode;
  /** Show the status line. Default true. */
  status?: boolean;
  /** Height of the component. Default 560. */
  height?: number | string;
  className?: string;
  style?: CSSProperties;
  children?: ReactNode;
  ref?: Ref<DocEditorHandle>;
}

export const DocEditor: (props: DocEditorProps) => JSX.Element;
export { DocEditor as DocxEditor };
export default DocEditor;

export interface UseDocEditorResult {
  ref: Ref<HTMLDivElement>;
  editor: DocEditor | null;
  status: 'loading' | 'ready' | 'error';
  error: Error | null;
  info: OpenInfo | null;
  open: (source: MountOptions['file'], opts?: { filename?: string }) => Promise<OpenInfo>;
  exportDocx: (filename?: string) => Promise<Blob>;
  exportPdf: (filename?: string) => Promise<Blob>;
  state: () => DocState | null;
}

export function useDocEditor(options?: MountOptions): UseDocEditorResult;
