# OOXML / PDF export checks

Small headless harness used while auditing `wasm-ui` against WordprocessingML. It
drives the real wasm module in a throwaway Chromium (never the user's tab), imports a
`.docx` through `doc_import_docx`, and pulls artifacts back out through the app's own
export hooks.

## Prerequisites

- the app running: `./.venv/bin/python main.py` (serves `index.html` on :5555)
- Node with `puppeteer-core` (`npm i puppeteer-core` in a directory on the module path,
  or run these scripts from that directory)
- Python with `pypdfium2` + `pikepdf` for the PDF checks (the project venv has both)
- headless Chromium; set `CHROME=/path/to/chrome` if the default
  `~/.cache/ms-playwright/chromium-*/chrome-linux64/chrome` does not exist

## Build the corpus

```
python3 ../ooxml-corpus/corpus.py            # writes ./ooxml/*.docx
```

One file per feature area: run formatting, fields, notes, review marks, headers,
numbering, paragraph properties, tables, sections, images.

## Import all fixtures and dump the model

```
node import_corpus.js ooxml corpus-result.json http://127.0.0.1:5555/
```

Each entry contains the imported paragraph count, the `doc_debug_state()` JSON (text,
alignment, style, spacing, runs, list flags) and the re-exported `.docx` (base64) for
zip inspection.

## Export one fixture to PDF

```
node export_probe.js ooxml/runs.docx out.pdf http://127.0.0.1:5555/
```

The second console line is `doc_pdf_debug()`: pages, paragraphs visited, flushes per
export and element count per page — enough to tell "the writer paginated" from "the
writer dropped everything after column 1".

## Validate the PDF itself

```
python3 check_pdf.py out.pdf [expected-pages]   # strict open, page objects, glyph bands
python3 overlap_check.py out.pdf                # count colliding glyph pairs
```

`check_pdf.py` uses `pikepdf.open(..., attempt_recovery=False)`: a repaired-by-pdfium
file that a phone will refuse must fail here. `overlap_check.py` catches the failure
mode that a screenshot hides — two text runs drawn on top of each other.

## What "passing" looks like

- `check_pdf.py`: `strict open: ok`, every page exposes `/Type /Parent /MediaBox
  /Contents /Resources`, page count as expected.
- `overlap_check.py`: `TOTAL overlapping pairs: 0`.
- `import_corpus.js`: the fixture's text is present in `dump[]`, with the expected
  `al` (alignment) and `lists` values from the manifest in
  [`docs/OOXML-COMPATIBILITY.md`](../../docs/OOXML-COMPATIBILITY.md).
