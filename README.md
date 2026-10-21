# wasm-ui — demo shell for two document libraries

This repository used to hold one large C++/Dear ImGui app that did both PDF reading and
`.docx` editing. It is now a thin **demo shell**: the two halves live in their own repositories,
each published as a JavaScript package with a vanilla API *and* React components.

| library | what it does | repo |
|---|---|---|
| **wasm-pdf-reader** | parse, rasterize and extract text from PDF files | [dionaditya/wasm-pdf-reader](https://github.com/dionaditya/wasm-pdf-reader) |
| **wasm-docx-editor** | open, edit and save Word `.docx`; export PDF | [dionaditya/wasm-docx-editor](https://github.com/dionaditya/wasm-docx-editor) |

**Live demos:** [PDF reader](https://dionaditya.github.io/wasm-ui/pdf/) ·
[DOCX editor](https://dionaditya.github.io/wasm-ui/docx/)

## Layout

```
index.html             landing page (links to both demos)
pdf/                   mountPdfReader() demo + sample.pdf
docx/                  mountDocEditor() demo + sample.docx
vendor/
  wasm-pdf-reader/dist/  latest build from that repo's main branch
  wasm-docx-editor/dist/ latest build from that repo's main branch
assets/                thumbnails for the landing page
```

There is no build step here: the shell imports each library straight from `vendor/`, so what you
see in the demo is exactly what those repositories publish (`dist/` is committed there too).
The vendored PDF reader is the latest one — 4 GB wasm heap, streamed file load, and
calibre-style object streams.

## Refresh a vendored library

```sh
# PDF reader — straight from its main branch
cd /path/to/wasm-pdf-reader && git archive main dist | tar -x -C /path/to/wasm-ui/vendor/wasm-pdf-reader

# DOCX editor — build it, then copy the output
cd /path/to/wasm-docx-editor && ./build.sh && node scripts/build-js.mjs
cp -r dist /path/to/wasm-ui/vendor/wasm-docx-editor/dist
```

## Run locally

Any static server works; the wasm `.wasm`/`.data` pair only needs correct MIME types.

```sh
python3 -m http.server 8080      # then open http://127.0.0.1:8080/
```

## Deployment

`.github/workflows/pages.yml` publishes the repository root to GitHub Pages on every push to
`main` (`.nojekyll` keeps the folder names intact). The same tree is also pushed to a `gh-pages`
branch.

## History

The original single-app source (C++ editor + native PDF parser + emscripten shell) is in this
repository's git history before this commit; the live code is now:
`wasm-docx-editor/main.cpp` (+ `doctext.hpp`, `duckx/`, `pdf_widths.h`, `fonts_data.h`) and
`wasm-pdf-reader/src/cpp/` (pdfdoc, pdfraster, wpdf_api).

## License

MIT — see [LICENSE](LICENSE). Both libraries are MIT as well.
