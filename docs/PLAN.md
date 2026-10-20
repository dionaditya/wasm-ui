# wasm-ui roadmap — from document editor to Word-class word processor

Status: October 2026. Written against the current `main.cpp` (single-file ImGui UI),
the PDF writer in the same file, and the `duckx` OOXML importer.

Two documents:

- this file — feature roadmap (what to build, in what order, and how to know it works);
- [OOXML-COMPATIBILITY.md](OOXML-COMPATIBILITY.md) — measured OOXML coverage and the
  plan to close it, including what "support all OOXML" can realistically mean.

---

## 0. Where the app stands today

Everything below is in the current build and was exercised through the app's own
debug hooks and exported artifacts (not by eyeballing a screenshot).

| Area | Working today | Notes |
|---|---|---|
| Typography | document styles parsed before the body (sizes, faces, indents, justification, spacing), continuous section breaks, line height matched to Word's single spacing | tables/figures still paginate differently from Word (see P0.5) |
| Editing | caret, selection, typing, works over the document model | per-run attributes, undo snapshots |
| Character formatting | bold, italic, underline, strike, super/subscript, colour, highlight, size; 8 embedded font families with real bold/italic faces (see below) | caps, small caps, character spacing are not modelled |
| Paragraphs | alignment (left/centre/right/justify), line spacing, space before/after, first-line/hanging indent, tab stops with dot/dash/underline leaders, bottom border | shading, all four borders, keep-with-next, widow/orphan control are not modelled |
| Lists | bullet and numbered, one level | no numbering.xml semantics, no restart/set-value/custom format |
| Tables | insert, rows/cols, uniform borders, header fill, body fill, caption | no merge/split, no per-cell borders/shading, no header repeat across pages, cells wrap but rows do not split across pages |
| Objects | inline images with width %, floating images (behind/front), shapes (rect/oval/line/triangle), LaTeX math via MathJax, Mermaid/Graphviz diagrams | no crop, recolour, transparency, wrap modes other than the three above |
| Pages | margins, orientation, size, newspaper columns, header/footer text with odd/even + first page flags, page numbers in header/footer text, tab stops, per-page setup | no watermark, page borders, line numbers, vertical alignment, drop caps, hyphenation |
| References | TOC / list of figures / list of tables generated from headings and captions, captions on images and tables | no footnotes/endnotes, citations, cross-references, index, table of authorities |
| Review | plain editing | no comments, track changes, compare, restrict, spell check, thesaurus, accessibility checker |
| Files | `.docx` import, `.docx` export, PDF export, PDF page raster import | no `.doc`, `.odt`, `.rtf`, `.html`, `.md`, `.tex`, `.txt`, no mail merge |
| View | one continuous canvas, zoom, rulers, dark UI | no print/web/draft/read/focus modes, no multi-page or navigation pane, English only |

The two weakest links for real users are **OOXML fidelity** and **PDF export geometry**.
The PDF side has just been repaired (below); OOXML is the subject of the companion doc.

### P0.5 — preview fidelity against Word (in progress)

Found while diffing `test-naskah.docx` against LibreOffice's render:

1. **`styles.xml` was never loaded before parsing.** The zip scan broke as soon as
   `word/document.xml` appeared, and Word writes `styles.xml` near the end — so
   every style-derived size, face, indent, spacing and justification was ignored,
   and a second pass loaded styles *after* the paragraphs were already built.
   Fixed: scan the package for both parts.
2. **Base typography.** Paragraphs now carry `baseFs` / `baseFamily` resolved from
   `w:pStyle` → `basedOn` chain → `w:docDefaults` → the default paragraph style
   (`w:default="1"`), including for paragraphs with no `<w:pPr>` at all. Before this
   the canvas drew every imported document at 15 pt DejaVu Sans.
3. **Line height.** `doctext::LineHeightFor` was `fs * 1.42`; Word's single spacing is
   ~1.15–1.20 em. Both the canvas and the PDF writer now use 1.20, and the paragraph
   multiplier (`w:line / 240`) still applies on top.
4. **Continuous section breaks.** A `w:type="continuous"` section (abstract over a
   two-column body) no longer starts a new page: the column band starts where the
   previous text ended, in both the canvas and the PDF writer.
5. **Run faces inherit.** A run without `w:rFonts` takes the paragraph's face instead
   of falling back to the editor default.

Result on the paper fixture: 9 pages vs Word's 10, page 1 content matches within 3
characters, body type is Liberation Serif 12 pt with a 14.4 pt pitch (Word: 14.45).

Still to do here: table row heights and table splitting (our fixed row model gains
pages differently), figure placement, and justification that distributes space the
way Word does on short lines.

---

## P0 — two-column PDF export (done, with the traps recorded)

Symptom reported: two-column documents exported to PDF with text overlapping,
columns colliding, and the abstract printing into the second column.

Root causes found in `BuildDocxPdf()` — all of them in the emit path, not in the
column maths itself:

1. **The column cursor was absolute.** `cursorX` started at `ML + colShift`, so in
   column 2 it was always `> ML`. The "is this the first chunk of a visual line?"
   test (`e.cont`) therefore never fired, no line advance happened, and every line
   of a column-2 paragraph was drawn on one baseline. Wrapping also computed
   `TW - (cursorX - ML)`, which went negative and produced one-character lines.
   → `cursorX` is now column-local; only the emitter adds `ML`.
2. **`flush()` was used to commit the last page.** With `cols > 1` a flush advances
   to the next column instead of closing the page, so a last page that ended in
   column 1 was dropped entirely (content silently missing).
   → the final page is committed with an explicit `pages.push_back(cur)`.
3. **`flush()` reset `TW` to the full text width** (via `syncPage()`). A paragraph
   that continued after a column break wrapped at page width and printed through
   the next column.
   → the paragraph's wrap width is captured once (`wrapW`) and used for all its lines.
4. **Sections were not page breaks in the export.** A one-column section (abstract)
   could be laid out into a page that already had a second column, at full width.
   → the writer now starts a new page on a section change, matching the canvas.
5. **Table cells and captions had no column offset**, and captions had no baseline
   at all (they rode the page cursor), so a caption landed inside its table.
   → every element is stamped with `col / colX / colW` when it is pushed; captions
   carry an absolute baseline.
6. **Alignment used the page width, not the column width** → centred/right-aligned
   text in column 2 drifted into the gutter. → the alignment offset uses the
   element's own column width.
7. **Highlights used the page cursor for their y**, so they appeared next to the
   text they belonged to. → they use the run's baseline.

While in there, three overlap sources that are not column-specific:

8. **Advance width was estimated at 0.5 em per byte.** Wide runs (bold, W/M) were
   wider than estimated, so the following run was drawn on top of them. Wrapping
   inherited the same error. → `pdf_widths.h` carries the standard-14 AFM widths
   (Helvetica/Times/Courier × regular/bold/italic/bold-italic), and both wrapping
   and advance use them.
9. **Line height came from the paragraph style**, so a 24 pt run inside a 12 pt
   paragraph overlapped the next line. → a line is as tall as its tallest run.
10. **Table rows were a fixed 15 pt** and cell text was never wrapped. → cells wrap
    to the column, rows grow, and a table that does not fit moves to the next column.

Verification (all in this repo's neighbourhood, reproducible with
`tools/ooxml-corpus` + the probe described in the compatibility doc):

| Check | Before | After |
|---|---|---|
| 2-column text + table, 1 page | 3 x-columns of glyphs at x≈560, interleaved text | 2 pages, strict parse OK, 0 overlapping glyph pairs |
| 2-column, table in column 2 | cells at column-1 x, text over rects | cells and text both at x≈319 in column 2 |
| Abstract (1 col) then 2-col body | abstract drawn across column 2 | abstract on its own page, body 2-col, 0 overlaps |
| Real paper (`test-naskah.docx`, 314 paragraphs, 5 sections, 11 pages) | 372 overlapping glyph pairs | 0 overlapping pairs, 13 pages, strict parse OK |

Known deviations left on purpose (recorded, not hidden):

- a **continuous** section break still starts a new page in the export (Word starts
  the columns under the preceding text on the same page);
- column balancing is not implemented (Word balances the last page of a continuous
  section);
- the .docx export still writes single-column sections (see OOXML doc).

### Fonts

`fonts_data.h` is generated (`tools/fonts/build_fonts.py`) and embeds eight
families × four faces, subset to WinAnsi + Latin-1 + typographic punctuation:

| # | Family | Stands in for | Class |
|---|---|---|---|
| 0 | DejaVu Sans | (default) | sans |
| 1 | DejaVu Serif | (default serif) | serif |
| 2 | DejaVu Sans Mono | (default mono) | mono |
| 3 | Liberation Sans | Arial | sans |
| 4 | Liberation Serif | Times New Roman | serif |
| 5 | Liberation Mono | Courier New | mono |
| 6 | Carlito | Calibri | sans |
| 7 | Caladea | Cambria | serif |

- All eight are free (DejaVu: Bitstream Vera licence; Liberation/Carlito/Caladea:
  SIL OFL) and the last five are **metric-compatible** with the Word defaults, so a
  document laid out in Word keeps its line breaks when opened here.
- `w:rFonts` is now read: a run asking for `Times New Roman` opens as Liberation
  Serif, `Calibri` as Carlito, unknown names fall back on shape (serif/mono/sans).
- The `.docx` writer emits the **Word name** (`Times New Roman`, `Calibri`, …) via
  `kDocFamilyAlias`, so a file saved here opens with the right metrics in Word.
- The PDF writer only carries the base-14 faces, so every family collapses onto its
  class (sans → Helvetica, serif → Times, mono → Courier). Embedding TTFs into the
  PDF is the follow-up if pixel-identical PDFs matter.
- Adding a family = one entry in `FAMILIES` in the generator, rerun it, rebuild.
  Each face costs ~25–40 KB of wasm.

---

## P1 — document model gaps that block the rest

These are prerequisites: implementing review/references/mailings on top of a model
that cannot express them means rewriting them twice.

1. **One model for all paragraph/run properties.** Today: `DocPara`/`DocAttr` with a
   handful of fields, plus style defaults resolved at import. Needed: a property bag
   (or explicit struct) covering borders (4 sides), shading, keep-lines/keep-next,
   widow control, tabs (with alignment + leader), character spacing, caps/small caps,
   font name per run, language per run, and a style reference that survives round-trip.
2. **Numbering model** (abstract numbering, level overrides, restart, start value,
   custom formats, list styles). Lists are currently "bullet or number".
3. **Field model** (begin/separate/end, instr text, cached result, dirty flag) so TOC,
   PAGE, NUMPAGES, REF, SEQ, HYPERLINK and cross-references survive editing.
4. **Notes model** (footnotes/endnotes with separator parts, numbering per section)
   and **annotation model** (comments with anchors and replies; revisions with
   author/date/type).
5. **Section model** (page size/orientation/margins/columns/header-footer references,
   type = nextPage/continuous/evenPage/oddPage, title page, page numbering).
6. **Table model** (grid definition, per-cell span/merge, borders/shading, row height,
   header repeat, split), not a `rows × cols` string matrix.

---

## P2 — Word-class features, in build order

Each workstream lists what the user gets, the model work it needs, and the acceptance
test that proves it. Estimates are for this codebase (C++/ImGui, no toolkit) and
assume the P1 model work is done.

### W1. Writing experience (1–2 weeks)
- AutoCorrect (smart quotes, dashes, `(c)`), list autoformat (`* `, `1. `), paragraph
  and sentence selection (triple-click / Ctrl+click), drag-select already works.
- Find & replace: regex, whole word, case, "replace all with count", search in headers.
- Format painter (single + double click lock), change case, clear formatting.
- *Acceptance*: keyboard-only pass over a 10-page document; no text-model regressions
  in the existing debug dump.

### W2. Styles (2–3 weeks)
- Style gallery + Styles pane, create/modify/update, style sets, document themes.
- Model: named styles with basedOn/next/linked, latent styles, theme colour/font maps.
- *Acceptance*: round-trip a Word file that uses `Heading 1/2/3`, `Quote`, `Normal`
  and a custom style; exported .docx must reference `w:pStyle`, not direct formatting.

### W3. Lists & numbering (1 week)
- Multilevel, restart, set value, custom formats, list styles, numbering from
  `numbering.xml` on import and back out on export.
- *Acceptance*: corpus case `numbering.docx` imports as an ordered list with the same
  levels/format; export contains `w:numPr` + `numbering.xml`.

### W4. Paragraph/page furniture (2 weeks)
- Paragraph borders (all sides + between), shading, keep-with-next, keep-lines,
  widow/orphan control, vertical alignment, line numbers, page borders, watermark,
  drop caps, hyphenation.
- *Acceptance*: each property has a canvas render, a .docx mapping and a PDF mapping;
  a three-way comparison (import → canvas → export) matches for a fixture per property.

### W5. References (3–4 weeks)
- Footnotes/endnotes (insert, edit, renumber, separator), captions, cross-references,
  citations + bibliography (APA/MLA/Chicago/IEEE), index, table of authorities.
- Depends on the field and notes models.
- *Acceptance*: footnote text survives import → edit → export; cross-reference updates;
  bibliography regenerates after inserting a source.

### W6. Review (4 weeks)
- Comments (margin balloons + pane), track changes (insert/delete/format), accept/reject,
  compare documents, restrict editing, accessibility checker, document inspector.
- *Acceptance*: a document with tracked changes round-trips with revisions intact;
  accept-all and reject-all produce the two expected texts.

### W7. Mailings (1–2 weeks)
- Mail merge from CSV, merge fields, address block, greeting line, rules, preview,
  finish to document, envelopes, labels.
- *Acceptance*: 100-row CSV produces 100 documents with no placeholder left behind.

### W8. View & i18n (2 weeks)
- Print/web/draft/read/focus modes, multi-page, page width, Navigation pane,
  gridlines, and the 21 UI languages listed in the request (JSON-ish string table,
  system-language default, RTL-safe layout).
- *Acceptance*: language switch without reload; every string in the table; no clipping
  at 390 px width (the phone layout already wraps the toolbar).

### W9. Files (2–3 weeks)
- `.doc` (legacy) import, `.odt`, `.rtf`, `.html`, `.md`, `.tex`, `.txt` both ways,
  page images (PNG per page) export.
- *Acceptance*: the same fixture set exported to each format re-imports with text and
  the top-level structure intact.

Suggested sequence: P1 model → W3 → W2 → W1 → W4 → OOXML closure (companion doc) →
W5 → W6 → W7 → W8 → W9. References and review are the expensive ones; they should not
start before the field/notes/annotation models exist.

---

## Engineering guardrails

- Every new property must be added in **four** places at once: model, canvas layout,
  PDF writer, .docx import/export — plus the debug dump. A property that exists in
  three of them is the definition of the bug class that produced the two-column
  export mess.
- Keep the export writer honest: no fixed row heights, no fixed character widths, no
  assumptions that a page has one text column.
- Extend `tools/ooxml-corpus` with a fixture per feature *before* implementing it; the
  corpus is the regression suite for import/export.
- `pikepdf.open(path, attempt_recovery=False)` and a glyph-overlap count are the two
  cheap gates for "the PDF is not broken" — both caught real bugs here while pdfium
  rendered the same file happily.
