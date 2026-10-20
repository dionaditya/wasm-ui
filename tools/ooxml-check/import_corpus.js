// Import every .docx in the corpus and dump the model the editor actually got.
// One browser, one module instance, sequential imports (import resets state).
const fs = require('fs');
const path = require('path');
const puppeteer = require('puppeteer-core');

const CHROME = '/home/hermes/.cache/ms-playwright/chromium-1248/chrome-linux64/chrome';

(async () => {
  const dir = process.argv[2];
  const outJson = process.argv[3];
  const url = process.argv[4] || 'http://127.0.0.1:5555/';
  const files = fs.readdirSync(dir).filter(f => f.endsWith('.docx')).sort();

  const browser = await puppeteer.launch({
    executablePath: CHROME, headless: 'new',
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--use-angle=swiftshader',
           '--enable-unsafe-swiftshader'],
  });
  const page = await browser.newPage();
  page.on('pageerror', e => console.error('[pageerror]', e.message));
  page.on('console', m => { if (m.type() === 'error') console.error('[page]', m.text()); });
  await page.goto(url, { waitUntil: 'load', timeout: 120000 });
  await page.waitForFunction(() => {
    try {
      return window.Module && Module.ccall &&
             Module.ccall('doc_debug_state', 'string', [], []).indexOf('paras') >= 0;
    } catch (e) { return false; }
  }, { timeout: 120000, polling: 200 });

  const out = {};
  for (const f of files) {
    const bytes = fs.readFileSync(path.join(dir, f));
    const b64 = bytes.toString('base64');
    const r = await page.evaluate(async (b64) => {
      const bin = atob(b64);
      const u8 = new Uint8Array(bin.length);
      for (let i = 0; i < bin.length; i++) u8[i] = bin.charCodeAt(i);
      const ptr = Module._malloc(u8.length);
      Module.HEAPU8.set(u8, ptr);
      const n = Module.ccall('doc_import_docx', 'number', ['number', 'number'], [ptr, u8.length]);
      Module._free(ptr);
      await new Promise(r => requestAnimationFrame(r));
      await new Promise(r => requestAnimationFrame(r));
      const state = Module.ccall('doc_debug_state', 'string', [], []);
      // second export: the .docx writer, to test round-tripping
      const size = Module.ccall('doc_export_docx_size', 'number', [], []);
      const dp = Module.ccall('doc_export_docx_data', 'number', [], []);
      const data = Module.HEAPU8.slice(dp, dp + size);
      let s = '';
      for (let i = 0; i < data.length; i += 0x8000)
        s += String.fromCharCode.apply(null, data.subarray(i, i + 0x8000));
      return { n, state, docx: btoa(s), size };
    }, b64);
    out[f] = { imported: r.n, state: JSON.parse(r.state), docx_b64: r.docx, docx_size: r.size };
    console.log(f, '->', r.n, 'paras, docx export', r.size, 'bytes');
  }
  fs.writeFileSync(outJson, JSON.stringify(out));
  await browser.close();
})().catch(e => { console.error('FAIL', e); process.exit(1); });
