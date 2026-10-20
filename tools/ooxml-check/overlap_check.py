#!/usr/bin/env python3
"""Measure text overlap in a PDF: how many glyph boxes collide with a foreign glyph.

An overlapping export shows as characters landing on top of each other; this
counts them without a screenshot and reports where they are.
"""
import sys
import collections
import pypdfium2 as pdfium


def boxes(tp):
    out = []
    n = tp.count_chars()
    for i in range(n):
        try:
            l, b, r, t = tp.get_charbox(i)
        except Exception:
            continue
        ch = tp.get_text_range(i, 1)
        if r - l <= 0 or not ch.strip():
            continue
        out.append((l, b, r, t, ch))
    return out


def collisions(bs, cell=6.0):
    grid = collections.defaultdict(list)
    for idx, (l, b, r, t, ch) in enumerate(bs):
        x0, x1 = int(l // cell), int(r // cell)
        y0, y1 = int(b // cell), int(t // cell)
        for gx in range(x0, x1 + 1):
            for gy in range(y0, y1 + 1):
                grid[(gx, gy)].append(idx)
    hits = []
    seen = set()
    for idx, (l, b, r, t, ch) in enumerate(bs):
        cand = set()
        for gx in range(int(l // cell), int(r // cell) + 1):
            for gy in range(int(b // cell), int(t // cell) + 1):
                cand.update(grid[(gx, gy)])
        for j in cand:
            if j <= idx:
                continue
            l2, b2, r2, t2, ch2 = bs[j]
            ox = min(r, r2) - max(l, l2)
            oy = min(t, t2) - max(b, b2)
            if ox <= 0 or oy <= 0:
                continue
            a1 = (r - l) * (t - b)
            a2 = (r2 - l2) * (t2 - b2)
            inter = ox * oy
            if inter / max(1e-6, min(a1, a2)) > 0.55:
                key = (round((b + b2) / 2), round((l + l2) / 2))
                if key in seen:
                    continue
                seen.add(key)
                hits.append((round(l, 1), round(b, 1), ch, ch2))
    return hits


if __name__ == '__main__':
    path = sys.argv[1]
    doc = pdfium.PdfDocument(path)
    total = 0
    for pi, page in enumerate(doc):
        tp = page.get_textpage()
        bs = boxes(tp)
        hits = collisions(bs)
        total += len(hits)
        print(f'page {pi}: {len(bs)} glyphs, {len(hits)} overlapping pairs')
        for h in hits[:8]:
            print('   ', h)
    print('TOTAL overlapping pairs:', total)
