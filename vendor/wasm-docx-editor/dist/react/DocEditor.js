import { jsx, jsxs } from "react/jsx-runtime";
import React, { forwardRef, useImperativeHandle } from "react";
import { useDocEditor } from "./hooks.js";
const DocEditor = forwardRef(function DocEditor2(props, ref) {
  const {
    file,
    toolbar = true,
    status = true,
    dropzone = true,
    height = 560,
    theme,
    labels,
    className = "",
    style,
    wasmUrl,
    dataUrl,
    moduleOptions,
    onReady,
    onOpen,
    onExport,
    onError,
    children
  } = props;
  const api = useDocEditor({
    file,
    toolbar,
    status,
    dropzone,
    height,
    theme,
    labels,
    className,
    wasmUrl,
    dataUrl,
    moduleOptions,
    onReady,
    onOpen,
    onExport,
    onError
  });
  useImperativeHandle(ref, () => ({
    get editor() {
      return api.editor;
    },
    open: api.open,
    exportDocx: api.exportDocx,
    exportPdf: api.exportPdf,
    state: api.state
  }), [api.editor, api.open, api.exportDocx, api.exportPdf, api.state]);
  return /* @__PURE__ */ jsxs("div", { className: `docxed-react ${className}`.trim(), style: { height, ...style }, children: [
    /* @__PURE__ */ jsx("div", { ref: api.ref, style: { height: "100%" }, "data-status": api.status }),
    children,
    api.status === "error" && api.error ? /* @__PURE__ */ jsx("div", { className: "docxed-status docxed-status--bar", role: "alert", children: String(api.error.message || api.error) }) : null
  ] });
});
var DocEditor_default = DocEditor;
export {
  DocEditor,
  DocEditor_default as default
};
