# wasm-ui — a document editor in WebAssembly

A word-processor style editor written in C++ with Dear ImGui, compiled to WebAssembly and
running entirely in the browser tab. It reads and writes Word `.docx` (OOXML), exports PDF,
and needs no backend: the document never leaves the device.

![Home ribbon](docs/screenshots/ribbon-home.png)

## What it does today

- **Editing** — caret/selection across paragraphs, typing with undo/redo, find and replace,
  clipboard with formatting, drag-select.
- **Character formatting** — bold, italic, underline, strike, super/subscript, colour,
  highlight, size, and eight embedded font families with real bold/italic faces
  (DejaVu + Liberation/Carlito/Caladea as free metric-compatible replacements for
  Arial/Times New Roman/Courier New/Calibri/Cambria).
- **Paragraphs** — left/centre/right/justified, space before/after, line spacing, indents,
  tab stops with leaders, lists, horizontal rules.
- **Tables** — insert, edit in place, per-column widths, per-side borders, cell shading and
  alignment imported from `w:tblGrid`/`w:tblPr`/`w:tcPr`.
- **Pages** — margins, orientation, size, newspaper columns, header/footer text, page setup,
  page numbers, per-page setup.
- **Objects** — inline and floating images, shapes, LaTeX maths and Mermaid/Graphviz diagrams
  rendered to embedded images.
- **References** — table of contents, list of figures, list of tables (generated from headings
  and captions).
- **Files** — `.docx` import, `.docx` export, PDF export. Section breaks, continuous sections,
  `styles.xml` (sizes, faces, indents, justification, spacing) and typography come from the file.

Ribbon tabs: File, Home, Insert, Layout, References, Review, View (zoom).

| Insert | Layout | View |
|---|---|---|
| ![Insert](docs/screenshots/ribbon-insert.png) | ![Layout](docs/screenshots/ribbon-layout.png) | ![View](docs/screenshots/ribbon-view.png) |

## Known limitations

OOXML support is partial and measured, not assumed: footnotes/endnotes, comments, tracked
changes, fields, numbering definitions and table merges are not imported yet, and the `.docx`
writer emits direct formatting rather than named styles. The full audit, per-part matrices and
the roadmap are in [docs/OOXML-COMPATIBILITY.md](docs/OOXML-COMPATIBILITY.md) and
[docs/PLAN.md](docs/PLAN.md).

## Build

Requirements: [emsdk](https://emscripten.org/docs/getting_started/downloads.html) on `PATH`
(`. ~/emsdk/emsdk_env.sh`), and Dear ImGui cloned next to `main.cpp`:

```bash
git clone --depth 1 https://github.com/ocornut/imgui imgui
./build.sh          # produces index.html, index.js, index.wasm, index.data
```

`fonts_data.h` is generated from the embedded TTFs by `tools/fonts/build_fonts.py`; it is
committed so the build works without the font sources.

## Run

```bash
python3 -m venv .venv && ./.venv/bin/pip install flask
./.venv/bin/python main.py        # http://127.0.0.1:5555/
```

Any static file server works too; `main.py` just disables caching so rebuilds show up.

## Layout

```
main.cpp                 UI, document model, layout, PDF writer, OOXML import/export
doctext.hpp              line layout, hit-testing, justified text, selection rects
pdf_widths.h             standard-14 font metrics used by the PDF writer
fonts_data.h             embedded subset TTFs + font family table
shell.html               the JS shell (file pickers, image/maths decode, exports)
build.sh                 emscripten build
main.py                  tiny Flask host
duckx/                   patched duckx (OOXML writer) + pugixml + miniz
tools/fonts/             font subsetter / header generator
tools/ooxml-corpus/      one .docx fixture per OOXML feature area
tools/ooxml-check/       headless import/export probes, strict PDF + overlap checks
docs/                    roadmap and OOXML compatibility audit
```

## Tests

`tools/ooxml-check` drives the real wasm module in headless Chromium: it imports every corpus
fixture, dumps the model, exports `.docx`/`.pdf` and asserts strict-PDF structure plus zero
overlapping glyphs. See [tools/ooxml-check/README.md](tools/ooxml-check/README.md).

## Licences

Application code: MIT (see `LICENSE`). Dear ImGui: MIT. duckx, pugixml, miniz: MIT.
DejaVu fonts: Bitstream Vera licence. Liberation, Carlito, Caladea: SIL Open Font Licence.
