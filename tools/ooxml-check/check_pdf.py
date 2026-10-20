#!/usr/bin/env python3
"""Check a two-column PDF export: strict structure + where the glyphs actually are."""
import sys
import pikepdf
import pypdfium2 as pdfium

path = sys.argv[1]
expect = int(sys.argv[2]) if len(sys.argv) > 2 else 0

pdf = pikepdf.open(path, attempt_recovery=False)
print('strict open: ok, pages =', len(pdf.pages))
for i, page in enumerate(pdf.pages):
    need = ['/Type', '/Parent', '/MediaBox', '/Contents', '/Resources']
    missing = [k for k in need if k not in page]
    print(f'  page {i}: objkeys ok' if not missing else f'  page {i}: MISSING {missing}')
if expect and len(pdf.pages) != expect:
    print('FAIL expected', expect, 'pages')
    sys.exit(1)

doc = pdfium.PdfDocument(path)
for pi, page in enumerate(doc):
    tp = page.get_textpage()
    n = tp.count_chars()
    boxes = []
    text = tp.get_text_bounded()
    # per-character boxes to find the gutter
    for i in range(n):
        try:
            l, b, r, t = tp.get_charbox(i)
        except Exception:
            continue
        if r - l <= 0:
            continue
        boxes.append((l, r, b, t))
    if not boxes:
        print(f'page {pi}: no text')
        continue
    xs = [b[0] for b in boxes]
    xmin, xmax = min(xs), max(xs)
    # gutter: split at page midpoint
    mid = (xmin + xmax) / 2
    left = sum(1 for b in boxes if b[1] < mid)
    right = sum(1 for b in boxes if b[0] > mid)
    straddle = len(boxes) - left - right
    print(f'page {pi}: {n} chars, x range {xmin:.1f}..{xmax:.1f}, '
          f'left={left} right={right} straddling={straddle}')
    print('   sample:', text[:70].replace('\n', ' | '))
    # rightmost glyph of each column should stay left of the gutter centre
    if right:
        rmin = min(b[0] for b in boxes if b[0] > mid)
        lmax = max(b[1] for b in boxes if b[1] < mid)
        print(f'   column edges: left ends {lmax:.1f}, right starts {rmin:.1f}, gap {rmin-lmax:.1f}')
