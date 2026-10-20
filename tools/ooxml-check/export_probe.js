// Headless probe: import a .docx into the running wasm-ui and pull the PDF export
// back out. Uses a throwaway browser profile; never touches the user's tab.
const fs = require('fs');
const path = require('path');
const puppeteer = require('puppeteer-core');

const CHROME = '/home/hermes/.cache/ms-playwright/chromium-1248/chrome-linux64/chrome';

(async () => {
  const docxPath = process.argv[2];
  const outPdf = process.argv[3] || '/tmp/out.pdf';
  const url = process.argv[4] || 'http://127.0.0.1:5555/';
  const bytes = fs.readFileSync(docxPath);
  const b64 = bytes.toString('base64');

  const browser = await puppeteer.launch({
    executablePath: CHROME,
    headless: 'new',
    args: ['--no-sandbox', '--disable-dev-shm-usage', '--use-angle=swiftshader',
           '--enable-unsafe-swiftshader', '--disable-gpu-sandbox'],
  });
  const page = await browser.newPage();
  page.on('console', m => { if (m.type() === 'error') console.error('[page]', m.text()); });
  page.on('pageerror', e => console.error('[pageerror]', e.message));

  await page.goto(url, { waitUntil: 'load', timeout: 120000 });
  await page.waitForFunction(() => {
    try {
      return window.Module && Module.ccall &&
             Module.ccall('doc_debug_state', 'string', [], []).indexOf('paras') >= 0;
    } catch (e) { return false; }
  }, { timeout: 120000, polling: 200 });

  const res = await page.evaluate(async (b64) => {
    const bin = atob(b64);
    const bytes = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
    const ptr = Module._malloc(bytes.length);
    Module.HEAPU8.set(bytes, ptr);
    const n = Module.ccall('doc_import_docx', 'number', ['number', 'number'], [ptr, bytes.length]);
    Module._free(ptr);
    await new Promise(r => requestAnimationFrame(r));
    await new Promise(r => requestAnimationFrame(r));
    await new Promise(r => requestAnimationFrame(r));
    const state = Module.ccall('doc_debug_state', 'string', [], []);
    const size = Module.ccall('doc_export_pdf_size', 'number', [], []);
    const pdbg = Module.ccall('doc_pdf_debug', 'string', [], []);
    const dptr = Module.ccall('doc_export_pdf_data', 'number', [], []);
    const data = Module.HEAPU8.slice(dptr, dptr + size);
    let s = '';
    for (let i = 0; i < data.length; i += 0x8000) {
      s += String.fromCharCode.apply(null, data.subarray(i, i + 0x8000));
    }
    return { n, state, pdbg, pdf: btoa(s), size };
  }, b64);

  fs.writeFileSync(outPdf, Buffer.from(res.pdf, 'base64'));
  const st = JSON.parse(res.state);
  console.log('imported paras:', res.n, 'pdf bytes:', res.size, '|', res.pdbg);
  console.log(res.state.slice(0, 1500));
  await browser.close();
})().catch(e => { console.error('FAIL', e); process.exit(1); });
