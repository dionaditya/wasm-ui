#!/usr/bin/env python3
"""Headless check of the wasm app over CDP.

Loads the served app in a throwaway headless Chromium, switches to the PDF tab,
reads pdf_doc_debug() out of the module and screenshots the canvas.

    LD_LIBRARY_PATH=... ./.venv/bin/python tools/pdfcheck/headless_check.py http://127.0.0.1:5555
"""
import json
import os
import subprocess
import sys
import time
import urllib.request

import websocket

CHROME = os.path.expanduser("~/.cache/ms-playwright/chromium-1248/chrome-linux64/chrome")
PORT = 9333


class CDP:
    def __init__(self, ws_url):
        self.ws = websocket.create_connection(ws_url, timeout=60)
        self.id = 0

    def call(self, method, **params):
        self.id += 1
        self.ws.send(json.dumps({"id": self.id, "method": method, "params": params}))
        while True:
            msg = json.loads(self.ws.recv())
            if msg.get("id") == self.id:
                return msg

    def eval(self, expr):
        r = self.call("Runtime.evaluate", expression=expr, returnByValue=True,
                      awaitPromise=True, userGesture=True)
        return r.get("result", {}).get("result", {}).get("value")


def main():
    url = sys.argv[1] if len(sys.argv) > 1 else "http://127.0.0.1:5555"
    out = sys.argv[2] if len(sys.argv) > 2 else "/tmp/pdfout/app.png"
    profile = "/tmp/chrome-pdfcheck"
    subprocess.run(["rm", "-rf", profile], check=False)
    proc = subprocess.Popen([
        CHROME, "--headless=new", "--no-sandbox", "--disable-dev-shm-usage",
        f"--remote-debugging-port={PORT}", f"--user-data-dir={profile}",
        "--use-angle=swiftshader", "--enable-unsafe-swiftshader",
        "--window-size=1280,900", url,
    ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    try:
        target = None
        for _ in range(120):
            try:
                with urllib.request.urlopen(f"http://127.0.0.1:{PORT}/json", timeout=2) as f:
                    targets = json.load(f)
                page = [t for t in targets if t.get("type") == "page"]
                if page:
                    target = page[0]
                    break
            except Exception:
                pass
            time.sleep(0.25)
        if not target:
            print("no CDP target")
            return 1
        cdp = CDP(target["webSocketDebuggerUrl"])
        cdp.call("Runtime.enable")
        cdp.call("Emulation.setDeviceMetricsOverride", width=1280, height=900,
                 deviceScaleFactor=1, mobile=False)

        # wait for the module and the first frame
        for _ in range(240):
            ok = cdp.eval("!!(window.Module && Module.ccall && document.getElementById('badge')"
                          " && document.getElementById('badge').textContent)")
            if ok:
                break
            time.sleep(0.25)
        time.sleep(3)

        print("badge:", cdp.eval("document.getElementById('badge').textContent"))
        # click the PDF tab (find it by label text in the tab bar)
        # ImGui draws its own tabs, so click the PDF tab with real mouse events.
        tab = os.environ.get("PDF_TAB_XY", "663,116")
        tx, ty = [int(v) for v in tab.split(",")]
        cdp.call("Input.dispatchMouseEvent", type="mouseMoved", x=tx, y=ty, buttons=0)
        time.sleep(0.4)
        for _round in range(2):
            cdp.call("Input.dispatchMouseEvent", type="mousePressed", x=tx, y=ty,
                     button="left", clickCount=1, buttons=1)
            time.sleep(0.35)
            cdp.call("Input.dispatchMouseEvent", type="mouseReleased", x=tx, y=ty,
                     button="left", clickCount=1, buttons=0)
            time.sleep(0.35)
        time.sleep(float(os.environ.get("PDF_WAIT", "4")))
        # The ImGui UI has no DOM, so drive it through the exported API instead:
        n = cdp.eval("Module.ccall('pdf_page_count','number',[],[])")
        print("pdf_page_count after autoload:", n)
        if not n:
            n = cdp.eval("Module.ccall('pdf_open_file','number',['string'],"
                         "['/compressed.tracemonkey-pldi-09.pdf'])")
            print("pdf_open_file ->", n)
        print("debug:", cdp.eval("Module.ccall('pdf_doc_debug','string',[],[])"))
        print("text items on page:", cdp.eval("Module.ccall('pdf_text_item_count','number',[],[])"))

        # drag across the abstract to select text, then read what the layer yields
        def drag(x0, y0, x1, y1):
            cdp.call("Input.dispatchMouseEvent", type="mouseMoved", x=x0, y=y0, buttons=0)
            time.sleep(0.2)
            cdp.call("Input.dispatchMouseEvent", type="mousePressed", x=x0, y=y0,
                     button="left", clickCount=1, buttons=1)
            steps = 8
            for i in range(1, steps + 1):
                cdp.call("Input.dispatchMouseEvent", type="mouseMoved",
                         x=x0 + (x1 - x0) * i / steps, y=y0 + (y1 - y0) * i / steps,
                         button="left", buttons=1)
                time.sleep(0.08)
            cdp.call("Input.dispatchMouseEvent", type="mouseReleased", x=x1, y=y1,
                     button="left", clickCount=1, buttons=0)
            time.sleep(0.4)

        if os.environ.get("PDF_SELECT", "1") == "1":
            drag(310, 340, 700, 348)
            sel = cdp.eval("Module.ccall('pdf_selected_debug','string',[],[])")
            print("selection:", (sel or "")[:220].replace("\n", " | "))
        r = cdp.call("Page.captureScreenshot", format="png")
        import base64
        data = r.get("result", {}).get("data")
        if data:
            open(out, "wb").write(base64.b64decode(data))
            print("screenshot:", out)
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except Exception:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
