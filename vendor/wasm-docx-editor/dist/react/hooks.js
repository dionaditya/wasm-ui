import { useCallback, useEffect, useRef, useState } from "react";
import { mountDocEditor } from "../mount.js";
function useDocEditor(options = {}) {
  const ref = useRef(null);
  const controller = useRef(null);
  const [status, setStatus] = useState("loading");
  const [error, setError] = useState(null);
  const [info, setInfo] = useState(null);
  const [editor, setEditor] = useState(null);
  const opts = useRef(options);
  opts.current = options;
  useEffect(() => {
    if (!ref.current) return void 0;
    let alive = true;
    const ctl = mountDocEditor(ref.current, {
      ...opts.current,
      onReady: (ed) => {
        if (!alive) return;
        setEditor(ed);
        setStatus("ready");
        if (opts.current.onReady) opts.current.onReady(ed);
      },
      onOpen: (i) => {
        if (!alive) return;
        setInfo(i);
        if (opts.current.onOpen) opts.current.onOpen(i);
      },
      onError: (err) => {
        if (!alive) return;
        setError(err);
        setStatus("error");
        if (opts.current.onError) opts.current.onError(err);
      }
    });
    controller.current = ctl;
    return () => {
      alive = false;
      controller.current = null;
      ctl.destroy();
    };
  }, []);
  const open = useCallback((source, o) => controller.current && controller.current.open(source, o), []);
  const exportDocx = useCallback((n) => controller.current && controller.current.exportDocx(n), []);
  const exportPdf = useCallback((n) => controller.current && controller.current.exportPdf(n), []);
  const state = useCallback(() => controller.current && controller.current.state(), []);
  return { ref, editor, status, error, info, open, exportDocx, exportPdf, state };
}
export {
  useDocEditor
};
