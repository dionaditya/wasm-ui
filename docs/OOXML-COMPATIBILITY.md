# OOXML compatibility — audit, measurement, and the road to full support

Scope: how well `wasm-ui` reads and writes `.docx` (Office Open XML, WordprocessingML),
what is missing, and what "support all OOXML" has to mean in practice.

Method: (a) static audit of every `w:`/`m:`/`a:` element the importer and exporter
reference; (b) a 10-file hand-written corpus (`tools/ooxml-corpus/corpus.py`), each file
isolating one feature area, imported through the real wasm module and dumped via
`doc_debug_state()`; (c) the exported `.docx` inspected as a zip; (d) the PDF export of
the same documents checked with a strict parser and a glyph-overlap count.

Corpus results are reproducible: `python3 tools/ooxml-corpus/corpus.py`, then the
import probe described at the bottom of this file.

---

## 1. Verdict in one paragraph

`wasm-ui` is a **layout-faithful reader of simple documents and a lossy writer**.
Text, run formatting (bold/italic/underline/strike/super/sub/colour/highlight/size),
paragraph alignment, section column counts, inline images and basic OMML maths come
through. Everything *structural* is currently dropped on the way in — footnotes,
endnotes, comments, tracked changes as such, fields, headers and footers, numbering
definitions, table merges, page size/orientation — and the exporter writes a much
smaller subset than it reads. So a Word file opens looking roughly right, but any
document that relies on Word's structure (an academic paper with footnotes and
header, a contract with tracked changes, a report with multilevel numbering) loses
that structure, and re-saving it from the app produces a much simpler file.

Measured on the corpus: **6 of 10 feature areas import with the right content**, 4
lose content entirely (notes, comments, headers/footers, numbering semantics), and
of the 6, 4 lose attributes (merges, borders/shading, tabs, caps/spacing). Export
writes **7 of the 10** areas' *text* but only 4 areas' *structure*.

Typography is the one area where the importer is now close to Word. The chain is:
`styles.xml` (defaults + `basedOn` chain) → paragraph style → direct `w:pPr`/`w:rPr`
→ run. Two bugs used to break it and are fixed: the package scan stopped at
`word/document.xml`, so `styles.xml` (written last by Word) was never loaded before
the body walk — every style-derived size, face, indent and justification was
ignored; and a paragraph without `<w:pPr>` never received the document defaults.
Measured on the 314-paragraph paper fixture (`test-naskah.docx`, Word/LO = 10 pages):

| Metric | Word / LibreOffice | wasm-ui (canvas + PDF) |
|---|---|---|
| body face | Times New Roman 12 pt | Liberation Serif 12 pt (metric-compatible) |
| line pitch | 14.45 pt | 14.4 pt |
| page count | 10 | 9 |
| page 1 text | title + abstract + two-column body start | same, 3317 vs 3314 chars |

Remaining differences on that fixture are pagination around tables/figures (our
tables do not split across pages and rows do not carry Word's exact heights), not
type metrics.

---

## 2. Measured import results (corpus, current build)

| # | Fixture | What was tested | Result | Detail |
|---|---|---|---|---|
| 1 | `runs.docx` | bold, italic, strike, super/sub, colour, highlight, caps, small caps, char spacing, font name, size | partial | all text and b/i/strike/vertAlign/colour/highlight/size kept; `w:rFonts` is mapped onto the 8 embedded families (Times New Roman → Liberation Serif, Calibri → Carlito, unknown names fall back on shape); **caps, small caps and character spacing are still ignored** |
| 2 | `fields.docx` | hyperlink, `fldSimple PAGE`, TOC field with cached result, bookmark | partial | all four texts kept; **field codes, hyperlink target and bookmark identity are lost** (only the rendered text survives) |
| 3 | `notes.docx` | footnote + endnote references and bodies | **fail** | neither the marker nor the note text is imported; `footnotes.xml`/`endnotes.xml` are never opened |
| 4 | `review.docx` | comment + reference, tracked insertion, tracked deletion | **fail** | insertion text kept as plain text; **comment and its text dropped**; deletion dropped (right visual result, no revision model) |
| 5 | `header.docx` | `headerReference`/`footerReference`, header1.xml/footer1.xml | **fail** | header and footer text are not read; the importer now clears the editor's demo header instead of printing "Document Header" over the file |
| 6 | `numbering.docx` | multilevel numbering, decimal + lowerLetter | **fail** | all three paragraphs become the same bullet list; level, format and start value are lost (`numbering.xml` is not parsed) |
| 7 | `paragraphs.docx` | centre/justify, space before/after, first-line + hanging indent, shading, paragraph borders, custom tab stops with leaders, keepNext | partial | alignment, spacing, first-line/hanging indent kept; **shading, side borders, custom tabs and keepNext dropped** (only a bottom border becomes a divider) |
| 8 | `table.docx` | header row, `gridSpan`, `vMerge`, borders, cell shading, caption after table | partial | cell text and grid kept; **merge, header repeat, per-cell shading/borders ignored** — merged cells collapse into the first column |
| 9 | `section.docx` | landscape page, custom margins, 2 columns, `titlePg`, page-number start, line numbering | partial | column count and margins honoured; **orientation, page size, title page, page numbering and line numbers ignored**; a continuous break is treated as a page break |
| 10 | `image.docx` | inline picture with `wp:extent` | ok | image decoded and attached, extent applied |

Also verified outside the corpus (existing fixtures in the repo): a 314-paragraph,
5-section, 9-table, 13-image real paper imports completely enough to export an
11–13 page PDF, and OMML equations are converted to LaTeX → rendered images.

---

## 3. Coverage by package part

Legend: **read** = parsed into the model; **ignored** = present in real files, not read;
**write** = produced on export.

| Part | Use | Read | Write |
|---|---|---|---|
| `word/document.xml` | body: paragraphs, runs, tables, drawings, math | partial | partial |
| `word/styles.xml` | paragraph + character styles, basedOn chain | partial (jc, indents, spacing, line, bold) | no (`pStyle` only from the template paragraph) |
| `word/numbering.xml` | list definitions | no | no |
| `word/footnotes.xml`, `endnotes.xml` | notes | no | no |
| `word/comments.xml` | comments | no | no |
| `word/header*.xml`, `footer*.xml` | headers/footers | no | no |
| `word/settings.xml` | even/odd headers, compat, zoom, track-changes flag | no | no |
| `word/fontTable.xml`, `theme/theme1.xml` | font names, theme colours/fonts | no (theme colour explicitly ignored) | no |
| `word/media/*` + rels | images | yes | yes (PNG/JPEG re-injected) |
| `word/_rels/document.xml.rels` | relationships (images, notes, comments, headers, hyperlinks) | images only | images only |
| `docProps/*` | metadata | no | template's |
| `m:oMath`/`oMathPara` | equations | partial → LaTeX → image | as image (not OMML) |
| `a:*`/`wp:*` drawings | pictures, shapes, text boxes, charts, SmartArt | pictures only | pictures only |
| `w:altChunk`, OLE objects, VML, custom XML, content controls | embedded content | content-control text only | no |

### Inline-level elements

Read: `w:p`, `w:r`, `w:t`, `w:tab` (→ 4 spaces), `w:br`/`w:cr` (→ space),
`w:noBreakHyphen`, `w:hyperlink` (text), `w:ins`/`w:moveTo` (text), `w:smartTag`,
`w:sdt`/`w:sdtContent`, `w:fldSimple` (cached text), `w:customXml`, `w:footnoteReference`
/`w:commentReference`/`w:del` (skipped), `w:pPr` (`w:pStyle`, `w:jc`, `w:spacing`,
`w:ind`, `w:numPr` as a boolean, `w:pBdr/w:bottom`), `w:rPr` (`w:b`, `w:i`, `w:u`
existence, `w:strike`, `w:dstrike`, `w:vertAlign`, `w:sz`, `w:color`, `w:highlight`).

Not read, and each one is a visible fidelity loss: `w:caps`, `w:smallCaps`,
`w:spacing` (character), `w:lang`, `w:shd`, `w:pBdr` (top/left/
right/between), `w:tabs`, `w:keepNext`, `w:keepLines`, `w:widowControl`, `w:vAlign`,
`w:outlineLvl` (beyond style mapping), `w:pageBreakBefore`, `w:lastRenderedPageBreak`,
`w:br w:type="page"` (becomes a space), `w:tblPr` merges/widths/alignment,
`w:trPr/w:tblHeader`/`w:cantSplit`, `w:tcPr` shading/borders, `w:sectPr` beyond
`w:cols` + `w:pgMar`, `w:bookmarkStart`, `w:fldChar`/`w:instrText`, `w:del`'s
`w:delText` (intentionally), `w:commentRangeStart/End`, `w:footnotePr`, `w:docGrid`.

### Export-side losses (what a re-saved file no longer has)

- **No styles**: paragraphs carry direct formatting; `w:pStyle` is not written.
- **No numbering**: lists are a literal `•` + tab (or `n.` + tab) inside the text.
- **Justification is lost**: `DA_Justify` is written as `left`.
- **No headers/footers, sections, columns or page setup**: the export reuses
  `template.docx` page properties, so a two-column document exports as one column
  even though the PDF and the canvas show columns.
- **Superscript/subscript are dropped** by the writer (verified: no `w:vertAlign` in
  the exported XML) even though the importer reads them.
- No footnotes/comments/revisions/fields/bookmarks/hyperlinks; no table merges,
  header repeat, per-cell shading; no `w:tabs`, `w:keepNext`, `w:shd`, char spacing,
  caps; maths is exported as a raster image, not OMML.
- Character/paragraph properties the model does hold (colour, highlight, size, font
  family, indents, spacing, borders-bottom, alignment left/centre/right) do survive;
  run fonts are written as the Word name of the metric-compatible face
  (`w:rFonts w:ascii="Times New Roman"` for Liberation Serif).

---

## 4. What "support all OOXML" actually means

OOXML (ECMA-376 / ISO/IEC 29500) is not one format: it is an OPC package containing
several markup languages — WordprocessingML, SpreadsheetML, PresentationML,
DrawingML, Chart, Diagram, Math, VML legacy plus Strict/Transitional variants. No word
processor implements "all of it"; Word itself ignores large parts.

A defensible target for this app, in tiers:

**Tier 1 — WordprocessingML core, Transitional (the 95 % that users notice)**
Paragraph/run/character properties, styles, numbering, tables with merges and
borders, sections and page setup, headers/footers with fields, images with basic
positioning, hyperlinks, bookmarks, tabs, page/section breaks, OMML maths as maths.
Import + export both, round-trip stable.

**Tier 2 — Document machinery**
Footnotes/endnotes, comments, tracked changes, fields (TOC/PAGE/REF/SEQ), citations
and bibliography parts, index, table of authorities, content controls (`w:sdt`),
custom XML, document settings, `docProps`, themes and theme colours.

**Tier 3 — Rich objects**
DrawingML shapes and text boxes, charts, SmartArt, WordArt, OLE/embedded objects,
`altChunk`, VML legacy drawing, digital signatures, protection/IRM, macros
(`.docm`), embedded fonts.

**Out of scope by definition** for a word processor: SpreadsheetML and
PresentationML as *authoring* formats (they only matter for embedded objects).

"All OOXML" in this project should therefore be read as **Tier 1 + Tier 2 complete
for WordprocessingML, Tier 3 for the objects the editor can actually render**. That is
still a large, multi-quarter programme — realistically 6–9 months of focused work for
Tier 1+2, because each property has to be modelled, laid out, drawn, exported and
tested four times over.

---

## 5. Plan to get there

### Phase A — stop losing structure (2–3 weeks)
1. Parse the package properly: relationship graph first, then parts by content type,
   instead of scanning zip entries for `document.xml` and `styles.xml`.
2. Read `w:sectPr` completely (size, orientation, margins, cols, type, titlePg,
   pgNumType, header/footer references) into the section model, and map it to the
   canvas page setup (the canvas already supports per-page setup).
3. Read `header*.xml`/`footer*.xml` (default/first/even) into the header/footer model,
   including PAGE/NUMPAGES fields as fields.
4. Read `numbering.xml` and `w:numPr` (`numId` + `ilvl`) into a real list model.
5. Read tables properly: `w:gridCol` widths, `gridSpan`, `vMerge`, `tcPr` borders and
   shading, `tblHeader`, `cantSplit`, row heights.
6. Read `w:tabs`, `w:shd`, all four `w:pBdr` sides, `keepNext`/`keepLines`/`widowControl`,
   `w:caps`/`smallCaps`/`w:spacing`, `w:rFonts`, `w:lang`.
7. Read footnote/endnote/comment parts and keep the references in the model (render in
   Phase B).

Acceptance: every corpus fixture imports with all of its content and attributes
visible in the debug dump; the real-paper fixture imports with its footnotes,
headers and numbering intact.

### Phase B — write what we read (3–4 weeks)
1. Replace the "template + direct formatting" writer with a part-by-part writer:
   `document.xml` from the model, `styles.xml` from the style model, `numbering.xml`
   from the list model, `headers/footers`, `footnotes/endnotes`, `comments`,
   `sectPr` per section, `settings.xml` minimal, `theme1.xml` passthrough.
2. Emit `w:pStyle` + style definitions instead of flattening; keep `w:jc="both"`.
3. Emit runs with `w:vertAlign` (fix the current loss), `w:rFonts`, `w:caps`,
   `w:shd`, `w:highlight`, `w:color`, `w:sz`, `w:spacing`.
4. Emit tables with real merges, widths, borders, header repeat.
5. Round-trip test: import → export → import and diff the model; the diff must be
   empty for the corpus.

### Phase C — fields and references (3–4 weeks)
Field engine (`fldChar` begin/separate/end, `instrText`, cached result, update on
demand) + TOC/TOF/TOT regenerated as fields rather than plain text, cross-references,
captions with SEQ, and citations/bibliography (a bibliography part plus a CSL-ish
source list).

### Phase D — review and collaboration (4–6 weeks)
`w:ins`/`w:del`/`w:rPrChange`/`w:pPrChange` with author/date, accept/reject, compare
(`w:ins`/`w:del` diff), comments with ranges and replies, document protection.

### Phase E — Tier 3 objects (open-ended)
DrawingML shapes/text boxes (round-trip at least as pictures + geometry), charts,
SmartArt, OLE (render as image, keep the part untouched on save), `altChunk`,
VML legacy, digital signatures, `.docm`.

### Cross-cutting: the validation harness

Keep it in the repo, not in a scratch dir:

- `tools/ooxml-corpus/` — one fixture per feature, plus a manifest of expected model
  facts (alignment, style id, list level, merge, note text, header text…).
- `tools/ooxml-check/` — headless runner: import each fixture through the wasm module,
  dump `doc_debug_state()`, export `.docx` and `.pdf`, then assert: manifest facts,
  strict PDF parse (`pikepdf`, no repair), zero overlapping glyph pairs, and an
  import→export→import model diff.
- A **third-party oracle**: LibreOffice headless (`soffice --convert-to pdf`) on the
  original and on our exported file, then compare page count and text per page. This
  is the only cheap way to catch "we think it looks right" drift.
- A **round-trip preserve** rule for parts we do not understand: if the editor opens a
  file containing parts it cannot model (SmartArt, charts, custom XML), it must carry
  the original bytes through on save instead of dropping them. This single rule buys
  most of "doesn't destroy my document" without implementing Tier 3.

---

## 6. Honest scoreboard

| Question | Answer |
|---|---|
| Can it open a normal Word document and show the text and basic formatting? | yes |
| Does a two-column academic paper survive? | text, headings, images and columns yes; abstract/body layout approximately; footnotes, header and numbering no |
| Does a contract with tracked changes survive? | no — revisions are flattened |
| Does a document with headings/styles round-trip into Word with its styles? | no — re-saved files carry direct formatting only |
| Does exporting and re-importing lose anything? | yes: superscript/subscript, justification, headers, sections/columns, lists, merges |
| Percentage of WordprocessingML *elements* handled | roughly a third read, under a fifth written — but the elements handled are the high-frequency ones, which is why simple documents look right |

The fastest way to move the number is Phase A+B with the round-trip harness in place;
everything else (review, references, mail merge) builds on that model.
