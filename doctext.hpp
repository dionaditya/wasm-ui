// ══════════════════════════════════════════════════════════════════════════════
// doctext.hpp — document text layout engine for the in-browser (wasm) docx
// editor.  Header-only, no dependencies besides Dear ImGui.
//
// Why this exists: the editor used to draw every paragraph with its own
// ImGui::InputTextMultiline() widget and fake bold/underline by overdrawing
// text on top of the widget.  That made real text layout impossible:
//   * each widget owns its own selection, so a selection can never span two
//     paragraphs (or even survive a click in another paragraph),
//   * ImGui's InputText can only render one font face, so italic was never
//     drawn at all (bold was faked with a 0.6px double-draw),
//   * there was no way to express superscript/subscript or per-character runs,
//     because ImGui widgets are one-style-per-widget.
//
// So this module produces an explicit line/character layout: visual lines,
// per-character x positions, and per-run pieces (each piece has its own font
// face, size, rise, colour and highlight).  The editor then does its own
// hit-testing, caret placement, run-level selection and rendering.
// ══════════════════════════════════════════════════════════════════════════════
#pragma once

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace dt {

// ─── UTF-8 helpers ───────────────────────────────────────────────────────────
inline int U8Seq(const char* s, int i, int n) {          // byte length of codepoint at i
    if (i < 0 || i >= n) return 0;
    unsigned char c = (unsigned char)s[i];
    if (c < 0x80)           return 1;
    if ((c & 0xE0) == 0xC0) return (i + 1 < n) ? 2 : 1;
    if ((c & 0xF0) == 0xE0) return (i + 2 < n) ? 3 : 1;
    if ((c & 0xF8) == 0xF0) return (i + 3 < n) ? 4 : 1;
    return 1;
}

inline int U8Prev(const char* s, int i) {                // start of codepoint before i
    if (i <= 0) return 0;
    int j = i - 1;
    while (j > 0 && ((unsigned char)s[j] & 0xC0) == 0x80) --j;
    return j;
}

inline int U8Decode(const char* s, int i, int n) {       // codepoint value at i
    if (i < 0 || i >= n) return 0;
    unsigned char c = (unsigned char)s[i];
    int k = U8Seq(s, i, n);
    if (k == 1) return c;
    int v = c & (0xFF >> (k + 1));
    for (int t = 1; t < k; ++t) v = (v << 6) | ((unsigned char)s[i + t] & 0x3F);
    return v;
}

inline int U8Encode(unsigned int cp, char* out) {        // returns byte count
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

inline bool U8IsWordChar(int cp) {
    return (cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') ||
           (cp >= 'a' && cp <= 'z') || cp == '_' || cp == 0x27 /* apostrophe */ || cp > 0x7F;
}

// ─── Fonts ───────────────────────────────────────────────────────────────────
// One family, four faces.  Picked per run so bold/italic are real faces
// instead of double-drawn / missing glyphs.
struct FontSet {
    ImFont* regular    = nullptr;
    ImFont* bold       = nullptr;
    ImFont* italic     = nullptr;
    ImFont* boldItalic = nullptr;

    bool Valid() const { return regular != nullptr; }
    bool HasItalic() const { return italic != nullptr || boldItalic != nullptr; }

    ImFont* Pick(bool wantBold, bool wantItalic) const {
        if (wantBold && wantItalic) {
            if (boldItalic) return boldItalic;
            if (italic)     return italic;
            if (bold)       return bold;
            return regular;
        }
        if (wantBold)   return bold   ? bold   : regular;
        if (wantItalic) return italic ? italic : regular;
        return regular;
    }
};

// ─── Style of one run ────────────────────────────────────────────────────────
struct TextStyle {
    ImFont* font      = nullptr;
    float   fs        = 15.f;   // requested size in px (before vert-align shrink)
    bool    underline = false;
    bool    strike    = false;
    int     vert      = 0;      // 0 = baseline, +1 = superscript, -1 = subscript
};

// Word's "single" line spacing is the font's own ascent+descent+gap: ~1.15 em
// for Times/Arial, ~1.22 em for Calibri. 1.42 (the old value) made every imported
// document ~25 % taller than Word and pushed its page count up by a third.
inline float LineHeightFor(float fs) { return fs * 1.20f; }

// Effective glyph size / baseline shift for a vert-align run.
inline void VertMetrics(int vert, float fs, float& fsEff, float& rise) {
    if (vert > 0)       { fsEff = fs * 0.70f; rise = -fs * 0.42f; }
    else if (vert < 0)  { fsEff = fs * 0.70f; rise =  fs * 0.16f; }
    else                { fsEff = fs;         rise =  0.f; }
}

// ─── A styled byte range of one paragraph ────────────────────────────────────
struct StyleSpan {
    int       b0 = 0, b1 = 0;      // byte range inside the paragraph
    TextStyle st;
    ImU32     color  = IM_COL32(20, 20, 30, 255);
    bool      hasHL  = false;
    ImVec4    hl     = ImVec4(0, 0, 0, 0);
};

inline float SpanFs(const StyleSpan& s)  { return s.st.vert ? s.st.fs * 0.70f : s.st.fs; }
inline float SpanRise(const StyleSpan& s) {
    return s.st.vert > 0 ? -s.st.fs * 0.42f : (s.st.vert < 0 ? s.st.fs * 0.16f : 0.f);
}

// ─── Layout ──────────────────────────────────────────────────────────────────
struct Piece {                     // one uniformly-styled chunk of a visual line
    int   b0 = 0, b1 = 0;
    int   span = 0;                // index into ParaLayout::spans
    float x = 0.f, w = 0.f;
    bool  tab = false;             // a '\t': draws a leader, not a glyph
};

struct Line {
    int   b0 = 0, b1 = 0;          // byte range covered by this visual line
    float w  = 0.f;                // advance width
    float y  = 0.f;                // top, relative to the paragraph origin
    std::vector<float> xs;         // xs[k] = x of byte (b0+k), k in [0, b1-b0]
    std::vector<Piece> pieces;
};

struct ParaLayout {
    std::vector<Line>      lines;
    std::vector<StyleSpan> spans;
    float lineH = 0.f;
    float h     = 0.f;
    float firstIndent = 0.f;        // extra x on the first visual line (px)

    bool Empty() const { return lines.empty(); }
};

// x offset of a visual line inside the text column, from paragraph alignment.
// align: 0 = left, 1 = centre, 2 = right.  wrapW is the column width in px.
inline float LineOffX(const Line& ln, int align, float wrapW)
{
    if (wrapW <= 0.f || align == 0) return 0.f;
    float d = wrapW - ln.w;
    if (d <= 0.f) return 0.f;
    if (align == 1) return d * 0.5f;
    if (align == 2) return d;
    return 0.f;                    // 0 = left, 3 = justified (drawn stretched)
}

// Index of the span containing byte `off`.
inline int SpanAt(const std::vector<StyleSpan>& spans, int off)
{
    for (int i = 0; i < (int)spans.size(); ++i)
        if (off >= spans[i].b0 && off < spans[i].b1) return i;
    return spans.empty() ? 0 : (int)spans.size() - 1;
}

inline void FillLineXs(Line& ln, const char* text, const std::vector<StyleSpan>& spans)
{
    ln.xs.assign((size_t)(ln.b1 - ln.b0) + 1, 0.f);
    for (const Piece& pc : ln.pieces) {
        if (pc.tab) {                       // caret sits at the gap start, then after it
            int first = pc.b0 - ln.b0, last = pc.b1 - ln.b0;
            if (first >= 0 && first < (int)ln.xs.size()) ln.xs[(size_t)first] = pc.x;
            if (last >= 0 && last < (int)ln.xs.size())   ln.xs[(size_t)last]  = pc.x + pc.w;
            continue;
        }
        if (pc.span < 0 || pc.span >= (int)spans.size()) continue;
        const StyleSpan& sp = spans[pc.span];
        const float fs = SpanFs(sp);
        int k = pc.b0;
        while (k < pc.b1) {
            int adv = U8Seq(text, k, pc.b1);
            if (k + adv > pc.b1) adv = pc.b1 - k;
            float x0 = pc.x;
            if (sp.st.font)
                x0 = pc.x + sp.st.font->CalcTextSizeA(fs, FLT_MAX, 0.f, text + pc.b0, text + k).x;
            for (int t = 1; t < adv; ++t) ln.xs[(size_t)(k - ln.b0) + t] = x0;
            float nx = pc.x;
            if (sp.st.font)
                nx = pc.x + sp.st.font->CalcTextSizeA(fs, FLT_MAX, 0.f, text + pc.b0, text + k + adv).x;
            if (!(nx >= x0)) nx = x0;
            ln.xs[(size_t)(k - ln.b0) + adv] = nx;
            k += adv;
        }
    }
}

// Lay out one paragraph made of styled spans.  wrapW is the available width in
// the same px space as the runs' font sizes.
inline ParaLayout LayoutParagraph(const char* text, std::vector<StyleSpan> spans, float wrapW,
                                  const std::vector<float>* tabStops = nullptr,
                                  float defTabPx = 36.f, float lineMul = 1.f, int align = 0)
{
    ParaLayout L;
    const int n = text ? (int)strlen(text) : 0;
    if (defTabPx < 4.f) defTabPx = 36.f;
    if (lineMul < 0.3f || lineMul > 6.f) lineMul = 1.f;
    if (wrapW < 1.f) wrapW = 1.f;
    if (spans.empty()) {
        StyleSpan s; s.b0 = 0; s.b1 = n;
        spans.push_back(s);
    }
    L.lineH = 0.f;
    for (const StyleSpan& s : spans) L.lineH = std::max(L.lineH, LineHeightFor(s.st.fs));
    if (L.lineH <= 0.f) L.lineH = LineHeightFor(15.f);
    L.lineH *= lineMul;          // paragraph line spacing (1.0 = the font's own)
    L.spans = spans;

    if (n == 0) {
        Line ln; ln.xs.assign(1, 0.f);
        L.lines.push_back(ln);
        L.h = L.lineH;
        return L;
    }

    int p = 0;
    while (p < n) {
        Line ln;
        ln.b0 = p;
        ln.y  = (float)L.lines.size() * L.lineH;
        float x = 0.f;
        int i = p;
        bool done = false;
        while (i < n && !done) {
            const int si = SpanAt(spans, i);
            const StyleSpan& sp = spans[si];

            // ── Tab: advance to the next tab stop and record the gap ────────
            if (text[i] == '\t') {
                float target = 0.f;
                bool  found  = false;
                if (tabStops) {
                    for (float s : *tabStops) if (s > x + 0.5f) { target = s; found = true; break; }
                }
                if (!found) {
                    float k = floorf((x + 0.5f) / defTabPx) + 1.f;
                    target  = k * defTabPx;
                }
                if (target <= x) target = x + defTabPx;
                Piece pc;
                pc.b0 = i; pc.b1 = i + 1; pc.span = si;
                pc.x  = x; pc.w = target - x; pc.tab = true;
                ln.pieces.push_back(pc);
                x = target;
                ++i;
                if (i >= n) done = true;
                continue;
            }

            int segEnd = std::min(sp.b1, n);
            // Stop the chunk before a newline *or* a tab: the tab is handled at
            // the top of this loop, so it must never be swallowed by a longer
            // word-wrap piece (which is what happens when fonts are absent or
            // the wrap position lands past it).
            for (int k = i; k < segEnd; ++k)
                if (text[k] == '\n' || text[k] == '\t') { segEnd = k; break; }

            if (segEnd <= i) {                       // span starts on a newline
                if (i < n && text[i] == '\n') ++i;
                done = true;
                break;
            }
            const float avail = wrapW - x;
            if (avail <= 1.f) { done = true; break; }

            int j;
            if (sp.st.font) {
                const char* q = sp.st.font->CalcWordWrapPosition(SpanFs(sp), text + i, text + segEnd, avail);
                j = (int)(q - text);
            } else {
                j = segEnd;
            }
            if (j <= i) j = std::min(segEnd, i + std::max(1, U8Seq(text, i, n)));
            if (j > segEnd) j = segEnd;

            // ImGui's wrap can only back up to a blank *inside the span it was
            // given*. When the blank sits in a previous run (Word splits runs at
            // arbitrary points), the break landed inside the word -- "atribut"
            // became "atrib" + "ut." on the next line. If the break is mid-word
            // and the line already has content, push the whole word to the next
            // line instead.
            if (j > i && j < segEnd && x > 0.5f &&
                text[j] != ' ' && text[j] != '\t' && text[j] != '\n' &&
                text[j - 1] != ' ') {
                done = true;
                break;                      // nothing consumed: new line starts at i
            }

            Piece pc;
            pc.b0 = i; pc.b1 = j; pc.span = si; pc.x = x;
            if (sp.st.font)
                pc.w = sp.st.font->CalcTextSizeA(SpanFs(sp), FLT_MAX, 0.f, text + i, text + j).x;
            ln.pieces.push_back(pc);
            x += pc.w;

            const bool spanEnded = (j >= segEnd);
            i = j;
            if (!spanEnded)                 done = true;   // word-wrapped mid span
            else if (i < n && text[i] == '\n') { ++i; done = true; }
            else if (i >= n)                done = true;
            // otherwise: next span continues on the same visual line
        }
        if (i == ln.b0 && i < n) ++i;                    // always make progress
        ln.b1 = i;
        ln.w  = x;
        FillLineXs(ln, text, L.spans);
        L.lines.push_back(ln);
        p = ln.b1;
    }

    // Justification is baked into the per-byte x table (xs) instead of being
    // applied only while drawing: the caret, selection rects and hit-testing all
    // read xs, and a draw-only stretch made the caret drift right of the glyphs.
    if (align == 3 && wrapW > 8.f) {
        for (size_t li = 0; li + 1 < L.lines.size(); ++li) {
            Line& ln = L.lines[li];
            if (ln.xs.size() < 2) continue;
            int spaces = 0;
            const int b1 = ln.b0 + (int)ln.xs.size() - 1;
            for (int b = ln.b0; b < b1; ++b)
                if (text[b] == ' ' && b + 1 < b1 && text[b + 1] != ' ') ++spaces;
            if (spaces <= 0) continue;
            const float slack = wrapW - ln.w;
            if (slack <= 0.f || slack > wrapW * 0.35f) continue;   // full lines only
            const float per = slack / (float)spaces;
            int seen = 0;
            for (size_t k = 0; k < ln.xs.size(); ++k) {
                ln.xs[k] += per * (float)seen;
                const int b = ln.b0 + (int)k;
                if (b < b1 && text[b] == ' ' && b + 1 < b1 && text[b + 1] != ' ') ++seen;
            }
            ln.w = wrapW;
        }
    }
    L.h = (float)L.lines.size() * L.lineH;
    return L;
}

// ─── Queries ─────────────────────────────────────────────────────────────────
inline int LineIndexOf(const ParaLayout& L, int off)
{
    if (L.lines.empty()) return 0;
    for (int i = 0; i < (int)L.lines.size(); ++i)
        if (off <= L.lines[i].b1 && off >= L.lines[i].b0) return i;
    return (int)L.lines.size() - 1;
}

inline float XForOffset(const ParaLayout& L, int off, int* outLine = nullptr)
{
    int li = LineIndexOf(L, off);
    const Line& ln = L.lines[li];
    int rel = off - ln.b0;
    if (rel < 0) rel = 0;
    if (rel >= (int)ln.xs.size()) rel = (int)ln.xs.size() - 1;
    if (outLine) *outLine = li;
    return ln.xs[rel];
}

inline int OffsetAtX(const Line& ln, const char* text, float x)
{
    const int n = (int)ln.xs.size() - 1;
    if (n <= 0) return ln.b0;
    if (x <= 0.f) return ln.b0;
    if (x >= ln.xs[(size_t)n]) return ln.b0 + n;
    int best = ln.b0;
    float bestD = FLT_MAX;
    for (int k = 0; k <= n; ++k) {
        if (k > 0 && k < n && ((unsigned char)text[ln.b0 + k] & 0xC0) == 0x80) continue;
        float d = fabsf(ln.xs[(size_t)k] - x);
        if (d < bestD) { bestD = d; best = ln.b0 + k; }
    }
    return best;
}

inline int OffsetAtPoint(const ParaLayout& L, const char* text, float x, float y, int* outLine = nullptr,
                         int align = 0, float wrapW = 0.f)
{
    if (L.lines.empty()) return 0;
    float lh = std::max(1.f, L.lineH);
    int li = (int)floorf(y / lh);
    li = std::max(0, std::min((int)L.lines.size() - 1, li));
    if (outLine) *outLine = li;
    // aligned lines are indented inside the column: undo that before hit-testing
    return OffsetAtX(L.lines[li], text, x - LineOffX(L.lines[li], align, wrapW));
}

// ─── Drawing ─────────────────────────────────────────────────────────────────
// AddText + optional shear, used only if a real oblique face is unavailable.
inline void DrawTextEx(ImDrawList* dl, ImFont* font, float fs, ImVec2 pos, ImU32 col,
                       const char* b, const char* e, bool shear = false, float shearAmt = 0.21f)
{
    if (!dl || !font || !b || e <= b) return;
    if (!shear) {
        dl->AddText(font, fs, pos, col, b, e);
        return;
    }
    const int v0 = dl->VtxBuffer.Size;
    dl->AddText(font, fs, pos, col, b, e);
    const float baseY = pos.y + fs * 0.82f;
    for (int v = v0; v < dl->VtxBuffer.Size; ++v) {
        ImDrawVert& vt = dl->VtxBuffer[v];
        vt.pos.x += (baseY - vt.pos.y) * shearAmt;
    }
}

// Draw a paragraph: highlight backgrounds first, then glyphs + decorations.
inline void DrawParagraph(ImDrawList* dl, const ParaLayout& L, ImVec2 origin, const char* text,
                          int align = 0, float wrapW = 0.f)
{
    if (!dl || !text) return;

    // align 3 = justified: stretch the gaps of every line except the last one
    auto countSpaces = [&](const Line& ln) {
        int n = 0;
        for (int b = ln.b0; b < ln.b1; ++b) if (text[b] == ' ') ++n;
        return n;
    };
    auto lineIndent = [&](size_t li) { return (li == 0) ? L.firstIndent : 0.f; };

    for (const Line& ln : L.lines) {
        const float off = LineOffX(ln, align, wrapW);
        const float top = origin.y + ln.y;
        const float bot = top + L.lineH;
        for (const Piece& pc : ln.pieces) {
            if (pc.span < 0 || pc.span >= (int)L.spans.size()) continue;
            const StyleSpan& sp = L.spans[pc.span];
            if (!sp.hasHL) continue;
            ImVec4 c = sp.hl;
            if (c.w <= 0.01f) c.w = 0.5f;
            dl->AddRectFilled({origin.x + off + pc.x, top}, {origin.x + off + pc.x + pc.w, bot},
                              ImGui::ColorConvertFloat4ToU32(c), 1.f);
        }
    }

    for (size_t li = 0; li < L.lines.size(); ++li) {
        const Line& ln = L.lines[li];
        float off = LineOffX(ln, align, wrapW) + lineIndent(li);

        // Justification is already in ln.xs (see LayoutParagraph): draw each word
        // at its table position instead of letting DrawTextEx lay the whole run at
        // the font's natural advance, which is what left the line ragged.
        const bool just = (align == 3) && (li + 1 < L.lines.size());
        for (const Piece& pc : ln.pieces) {
            if (pc.tab) continue;                  // leaders are drawn by the caller
            if (pc.span < 0 || pc.span >= (int)L.spans.size()) continue;
            const StyleSpan& sp = L.spans[pc.span];
            const float fs   = SpanFs(sp);
            const float rise = SpanRise(sp);
            ImFont* fnt      = sp.st.font ? sp.st.font : ImGui::GetFont();
            ImVec2 p(origin.x + off + pc.x, origin.y + ln.y + rise);
            const float thick = std::max(1.f, fs * 0.055f);
            const float baseY = p.y + fs * 0.82f;

            auto decoration = [&](float x, float w) {
                if (sp.st.underline)
                    dl->AddLine({x, baseY + fs * 0.13f}, {x + w, baseY + fs * 0.13f}, sp.color, thick);
                if (sp.st.strike)
                    dl->AddLine({x, baseY - fs * 0.28f}, {x + w, baseY - fs * 0.28f}, sp.color, thick);
            };

            if (!just) {
                DrawTextEx(dl, fnt, fs, p, sp.color, text + pc.b0, text + pc.b1, false);
                decoration(p.x, pc.w);
                continue;
            }
            int b = pc.b0;
            while (b < pc.b1) {
                int e = b;
                while (e < pc.b1 && text[e] != ' ') ++e;
                if (e > b) {
                    const size_t k = (size_t)(b - ln.b0);
                    const float wx = (k < ln.xs.size()) ? origin.x + off + ln.xs[k] : p.x;
                    DrawTextEx(dl, fnt, fs, {wx, p.y}, sp.color, text + b, text + e, false);
                    const size_t ke = (size_t)(e - ln.b0);
                    float w = (ke < ln.xs.size()) ? ln.xs[ke] - ln.xs[k] : 0.f;
                    if (w <= 0.f) w = fnt->CalcTextSizeA(fs, FLT_MAX, 0.f, text + b, text + e).x;
                    decoration(wx, w);
                }
                b = (e < pc.b1) ? e + 1 : e;
            }
        }
    }
}

// Selection rectangles for byte range [from,to) of this paragraph.
inline void DrawSelectionRects(ImDrawList* dl, const ParaLayout& L, ImVec2 origin,
                               const char* text, int from, int to, ImU32 col,
                               int align = 0, float wrapW = 0.f)
{
    if (!dl || !text) return;
    const int n = (int)strlen(text);
    from = std::max(0, std::min(from, n));
    to   = std::max(0, std::min(to, n));
    if (to < from) std::swap(from, to);
    if (from == to) return;

    for (const Line& ln : L.lines) {
        int s = std::max(from, ln.b0);
        int e = std::min(to,   ln.b1);
        const float top = origin.y + ln.y;
        const float bot = top + L.lineH;
        if (s >= e) continue;
        float x0 = origin.x + LineOffX(ln, align, wrapW) + XForOffset(L, s);
        float x1 = origin.x + LineOffX(ln, align, wrapW) + XForOffset(L, e);
        if (x1 < x0) std::swap(x0, x1);
        dl->AddRectFilled({x0, top}, {std::max(x1, x0 + 1.f), bot}, col);
    }
}

// Caret geometry for a byte offset.
inline void CaretMetrics(const ParaLayout& L, int off, ImVec2 origin,
                         float& outX, float& outY, float& outH,
                         int align = 0, float wrapW = 0.f)
{
    int li = 0;
    float x = XForOffset(L, off, &li);
    outX = origin.x + x + LineOffX(L.lines[li], align, wrapW);
    outY = origin.y + L.lines[li].y;
    outH = L.lineH;
}

} // namespace dt
