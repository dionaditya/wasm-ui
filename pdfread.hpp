// pdfread.hpp — a small PDF *reader* in plain C++ for the wasm build.
//
// Replaces the MuPDF.js dependency that used to render the PDF tab: the file
// never leaves the tab, nothing is fetched from a CDN, and the page bitmap is
// produced in the same module that uploads it as a GL texture.
//
// Scope (deliberate, this is a viewer for the PDF tab, not a print engine):
//   objects/streams   classic xref tables, xref streams (/Type/XRef), object
//                     streams (/Type/ObjStm), /Prev chains, scan recovery for
//                     files with a broken xref
//   filters           FlateDecode (+PNG/TIFF predictors), ASCIIHex, ASCII85,
//                     RunLength
//   content           path construction, fills (nonzero/evenodd), strokes with
//                     dash/cap/join, rect+path clipping, text (all show
//                     operators, rise, char/word spacing, horizontal scaling),
//                     Form and Image XObjects, inline images (raw/Gray/RGB)
//   fonts             simple fonts with /Widths, standard-14 via the embedded
//                     metric-compatible faces, Type0/Identity-H with an
//                     embedded TrueType program, /ToUnicode CMaps
//   not supported     encryption, CFF/Type1 glyph programs (those fall back to
//                     a substituted face using the font's encoding or
//                     ToUnicode), DCT/JPX images, shadings, patterns,
//                     transparency groups (painted opaque), annotations
//
// Rasterizing happens in software (4x vertical supersampling, exact horizontal
// coverage) and the result is handed to OpenGL as a texture by the caller.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <list>
#include <algorithm>

#include <zlib.h>

#include "fonts_data.h"
#include "pdf_widths.h"      // standard-14 AFM advances (shared with the PDF writer)
#include "jpegdec.hpp"       // /DCTDecode (baseline JPEG) images

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"

namespace pdfr {

// ─────────────────────────────────────────────────────────────────────────────
// Geometry: PDF matrices are [a b c d e f], x' = a*x + c*y + e, y' = b*x + d*y + f
// ─────────────────────────────────────────────────────────────────────────────
struct Mat {
    float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
};

// Apply `m` first, then `outer`  →  result = outer ∘ m
inline Mat Concat(const Mat& m, const Mat& outer)
{
    Mat o;
    o.a = m.a * outer.a + m.b * outer.c;
    o.b = m.a * outer.b + m.b * outer.d;
    o.c = m.c * outer.a + m.d * outer.c;
    o.d = m.c * outer.b + m.d * outer.d;
    o.e = m.e * outer.a + m.f * outer.c + outer.e;
    o.f = m.e * outer.b + m.f * outer.d + outer.f;
    return o;
}

inline void Apply(const Mat& m, float x, float y, float& ox, float& oy)
{
    ox = m.a * x + m.c * y + m.e;
    oy = m.b * x + m.d * y + m.f;
}

inline bool Invert(const Mat& m, Mat& out)
{
    float det = m.a * m.d - m.b * m.c;
    if (fabsf(det) < 1e-9f) return false;
    float id = 1.f / det;
    out.a =  m.d * id;
    out.b = -m.b * id;
    out.c = -m.c * id;
    out.d =  m.a * id;
    out.e = (m.c * m.f - m.d * m.e) * id;
    out.f = (m.b * m.e - m.a * m.f) * id;
    return true;
}

struct Pt { float x = 0, y = 0; };

// ─────────────────────────────────────────────────────────────────────────────
// Object model
// ─────────────────────────────────────────────────────────────────────────────
enum OType : uint8_t { O_NULL, O_BOOL, O_NUM, O_STR, O_NAME, O_ARR, O_DICT, O_STREAM, O_REF, O_KEYWORD };

struct Obj;
struct Ref { int num = 0, gen = 0; };

struct Obj {
    OType t = O_NULL;
    bool  bl = false;
    double num = 0;
    std::string s;                                  // string bytes / name / keyword
    std::vector<Obj> arr;
    std::vector<std::pair<std::string, Obj>> d;     // dict / stream dictionary
    Ref   ref;
    size_t streamOff = 0, streamLen = 0;            // stream payload inside Doc::buf

    bool IsNull() const { return t == O_NULL; }
    bool IsNum()  const { return t == O_NUM; }
    bool IsDict() const { return t == O_DICT || t == O_STREAM; }
    bool IsName(const char* n) const { return t == O_NAME && s == n; }
    double Num(double def = 0) const { return t == O_NUM ? num : def; }
    int    Int(int def = 0) const { return t == O_NUM ? (int)num : def; }
    const Obj* Get(const char* k) const {
        if (t != O_DICT && t != O_STREAM) return nullptr;
        for (size_t i = 0; i < d.size(); ++i) if (d[i].first == k) return &d[i].second;
        return nullptr;
    }
    void Set(const char* k, Obj v) { d.push_back(std::make_pair(std::string(k), v)); }
};

static const char* kWS = "\x00\x09\x0a\x0c\x0d\x20";

inline bool IsWS(int c)  { return c == 0 || c == 9 || c == 10 || c == 12 || c == 13 || c == 32; }
inline bool IsDelim(int c) {
    return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']'
        || c == '{' || c == '}' || c == '/' || c == '%';
}

// ─────────────────────────────────────────────────────────────────────────────
// Lexer / object parser
// ─────────────────────────────────────────────────────────────────────────────
struct Lexer {
    const uint8_t* p;
    size_t n;
    size_t i;

    Lexer(const uint8_t* data, size_t len, size_t pos = 0) : p(data), n(len), i(pos) {}

    void Ws() {
        while (i < n) {
            uint8_t c = p[i];
            if (c == '%') { while (i < n && p[i] != '\n' && p[i] != '\r') ++i; }
            else if (IsWS(c)) ++i;
            else break;
        }
    }
    bool Eof() { Ws(); return i >= n; }

    std::string Word() {                     // keyword / bare token
        Ws();
        size_t s = i;
        while (i < n && !IsWS(p[i]) && !IsDelim(p[i])) ++i;
        return std::string((const char*)p + s, i - s);
    }

    std::string Name() {                     // after '/'
        std::string out;
        while (i < n && !IsWS(p[i]) && !IsDelim(p[i])) {
            if (p[i] == '#' && i + 2 < n && isxdigit(p[i + 1]) && isxdigit(p[i + 2])) {
                char h[3] = { (char)p[i + 1], (char)p[i + 2], 0 };
                out.push_back((char)strtol(h, nullptr, 16));
                i += 3;
            } else out.push_back((char)p[i++]);
        }
        return out;
    }

    std::string LiteralString() {            // after '('
        std::string out;
        int depth = 1;
        while (i < n) {
            uint8_t c = p[i++];
            if (c == '\\') {
                if (i >= n) break;
                uint8_t e = p[i++];
                switch (e) {
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case '(': case ')': case '\\': out.push_back((char)e); break;
                    case '\r': if (i < n && p[i] == '\n') ++i; break;
                    case '\n': break;
                    default:
                        if (e >= '0' && e <= '7') {
                            int v = e - '0';
                            for (int k = 0; k < 2 && i < n && p[i] >= '0' && p[i] <= '7'; ++k)
                                v = v * 8 + (p[i++] - '0');
                            out.push_back((char)(v & 0xFF));
                        } else out.push_back((char)e);
                }
            } else if (c == '(') { ++depth; out.push_back('('); }
            else if (c == ')') { if (--depth == 0) break; out.push_back(')'); }
            else out.push_back((char)c);
        }
        return out;
    }

    std::string HexString() {                 // after '<', not '<<'
        std::string out;
        int hi = -1;
        while (i < n && p[i] != '>') {
            int c = p[i++];
            int v;
            if (c >= '0' && c <= '9') v = c - '0';
            else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
            else continue;
            if (hi < 0) hi = v;
            else { out.push_back((char)((hi << 4) | v)); hi = -1; }
        }
        if (hi >= 0) out.push_back((char)(hi << 4));
        if (i < n) ++i;                       // '>'
        return out;
    }

    bool Number(double& out) {
        Ws();
        size_t s = i;
        if (i < n && (p[i] == '+' || p[i] == '-')) ++i;
        bool digits = false, dot = false;
        while (i < n) {
            uint8_t c = p[i];
            if (c >= '0' && c <= '9') { digits = true; ++i; }
            else if (c == '.' && !dot) { dot = true; ++i; }
            else break;
        }
        if (!digits) { i = s; return false; }
        std::string tok((const char*)p + s, i - s);
        out = atof(tok.c_str());
        return true;
    }

    // Parse one object. `kw` receives a bare keyword (content-stream operator)
    // and the return value is O_KEYWORD.
    Obj Parse() {
        Ws();
        Obj o;
        if (i >= n) return o;
        uint8_t c = p[i];
        if (c == '/') { ++i; o.t = O_NAME; o.s = Name(); return o; }
        if (c == '(') { ++i; o.t = O_STR; o.s = LiteralString(); return o; }
        if (c == '<') {
            if (i + 1 < n && p[i + 1] == '<') {
                i += 2;
                o.t = O_DICT;
                for (;;) {
                    Ws();
                    if (i >= n) break;
                    if (p[i] == '>' && i + 1 < n && p[i + 1] == '>') { i += 2; break; }
                    if (p[i] != '/') { ++i; continue; }     // junk: skip one byte
                    ++i;
                    std::string k = Name();
                    Obj v = Parse();
                    if (v.t == O_KEYWORD && v.s == ">>") break;
                    o.d.push_back(std::make_pair(k, v));
                }
                return o;
            }
            ++i;
            o.t = O_STR;
            o.s = HexString();
            return o;
        }
        if (c == '[') {
            ++i;
            o.t = O_ARR;
            for (;;) {
                Ws();
                if (i >= n) break;
                if (p[i] == ']') { ++i; break; }
                Obj v = Parse();
                if (v.t == O_KEYWORD) { if (v.s == "]") break; else continue; }
                o.arr.push_back(v);
            }
            return o;
        }
        if (c == ']' || c == '>' || c == ')' || c == '}' || c == '{') {
            ++i;
            o.t = O_KEYWORD;
            o.s = std::string(1, (char)c);
            return o;
        }
        if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.') {
            size_t save = i;
            double v = 0;
            if (!Number(v)) { i = save + 1; o.t = O_KEYWORD; o.s = std::string(1, (char)c); return o; }
            o.t = O_NUM;
            o.num = v;
            // "N G R" reference?
            size_t after = i;
            Ws();
            size_t p2 = i;
            double g = 0;
            if (floor(v) == v && v > 0 && Number(g) && floor(g) == g && g >= 0) {
                Ws();
                if (i < n && p[i] == 'R' && (i + 1 >= n || IsWS(p[i + 1]) || IsDelim(p[i + 1]))) {
                    ++i;
                    o.t = O_REF;
                    o.ref.num = (int)v;
                    o.ref.gen = (int)g;
                    return o;
                }
            }
            i = after;
            (void)p2;
            return o;
        }
        std::string w = Word();
        if (w == "true")  { o.t = O_BOOL; o.bl = true;  return o; }
        if (w == "false") { o.t = O_BOOL; o.bl = false; return o; }
        if (w == "null")  { o.t = O_NULL; return o; }
        o.t = O_KEYWORD;
        o.s = w;
        return o;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Filters
// ─────────────────────────────────────────────────────────────────────────────
static bool Inflate(const uint8_t* src, size_t n, std::vector<uint8_t>& out, bool raw = false)
{
    z_stream zs;
    memset(&zs, 0, sizeof zs);
    if (inflateInit2(&zs, raw ? -15 : 15) != Z_OK) return false;
    zs.next_in = (Bytef*)src;
    zs.avail_in = (uInt)n;
    uint8_t chunk[16384];
    out.clear();
    int ret = Z_OK;
    for (;;) {
        zs.next_out = chunk;
        zs.avail_out = sizeof chunk;
        ret = inflate(&zs, Z_NO_FLUSH);
        size_t got = sizeof chunk - zs.avail_out;
        if (got) out.insert(out.end(), chunk, chunk + got);
        if (ret == Z_STREAM_END) break;
        if (ret != Z_OK) break;
        if (zs.avail_in == 0 && got == 0) break;
    }
    inflateEnd(&zs);
    return !out.empty();
}

static void Predictor(std::vector<uint8_t>& d, int pred, int colors, int bpc, int columns)
{
    if (pred < 2) return;
    int bpp = std::max(1, (colors * bpc + 7) / 8);
    int rowBytes = (columns * colors * bpc + 7) / 8;
    if (pred == 2) {                                  // TIFF predictor, 8-bit only
        if (bpc != 8) return;
        for (size_t r = 0; r + rowBytes <= d.size(); r += rowBytes)
            for (int i = bpp; i < rowBytes; ++i)
                d[r + i] = (uint8_t)(d[r + i] + d[r + i - bpp]);
        return;
    }
    // PNG predictors: every row is prefixed with a filter type byte
    std::vector<uint8_t> out;
    out.reserve(d.size());
    std::vector<uint8_t> prev(rowBytes, 0), cur(rowBytes, 0);
    size_t pos = 0;
    while (pos + 1 + (size_t)rowBytes <= d.size()) {
        int ft = d[pos++];
        memcpy(cur.data(), &d[pos], rowBytes);
        pos += rowBytes;
        for (int i = 0; i < rowBytes; ++i) {
            int a = i >= bpp ? cur[i - bpp] : 0;
            int b = prev[i];
            int c = i >= bpp ? prev[i - bpp] : 0;
            int x = cur[i];
            switch (ft) {
                case 0: break;
                case 1: x += a; break;
                case 2: x += b; break;
                case 3: x += (a + b) / 2; break;
                case 4: {
                    int pp = a + b - c;
                    int pa = abs(pp - a), pb = abs(pp - b), pc = abs(pp - c);
                    x += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                    break;
                }
                default: break;
            }
            cur[i] = (uint8_t)x;
        }
        out.insert(out.end(), cur.begin(), cur.end());
        prev = cur;
    }
    if (!out.empty()) d.swap(out);
}

static bool AsciiHexDecode(const uint8_t* s, size_t n, std::vector<uint8_t>& out)
{
    out.clear();
    int hi = -1;
    for (size_t i = 0; i < n; ++i) {
        uint8_t c = s[i];
        if (c == '>') break;
        int v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else continue;
        if (hi < 0) hi = v;
        else { out.push_back((uint8_t)((hi << 4) | v)); hi = -1; }
    }
    if (hi >= 0) out.push_back((uint8_t)(hi << 4));
    return true;
}

static bool Ascii85Decode(const uint8_t* s, size_t n, std::vector<uint8_t>& out)
{
    out.clear();
    uint32_t acc = 0;
    int cnt = 0;
    for (size_t i = 0; i < n; ++i) {
        uint8_t c = s[i];
        if (c == '~') break;
        if (c == 'z' && cnt == 0) { out.push_back(0); out.push_back(0); out.push_back(0); out.push_back(0); continue; }
        if (c < '!' || c > 'u') continue;
        acc = acc * 85 + (uint32_t)(c - '!');
        if (++cnt == 5) {
            for (int k = 3; k >= 0; --k) out.push_back((uint8_t)((acc >> (k * 8)) & 0xFF));
            acc = 0;
            cnt = 0;
        }
    }
    if (cnt > 0) {
        for (int k = cnt; k < 5; ++k) acc = acc * 85 + 84;
        for (int k = 3; k >= 4 - (cnt - 1); --k) out.push_back((uint8_t)((acc >> (k * 8)) & 0xFF));
    }
    return true;
}

static bool RunLengthDecode(const uint8_t* s, size_t n, std::vector<uint8_t>& out)
{
    out.clear();
    size_t i = 0;
    while (i < n) {
        uint8_t L = s[i++];
        if (L == 128) break;
        if (L < 128) {
            size_t len = (size_t)L + 1;
            if (i + len > n) len = n - i;
            out.insert(out.end(), s + i, s + i + len);
            i += len;
        } else {
            if (i >= n) break;
            uint8_t v = s[i++];
            out.insert(out.end(), (size_t)(257 - L), v);
        }
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Document
// ─────────────────────────────────────────────────────────────────────────────
struct Page {
    Obj dict;
    Obj resources;
    float mb[4] = { 0, 0, 612, 792 };
    int   rot = 0;
};

struct Doc {
    std::vector<uint8_t> buf;
    std::map<int, Obj> objs;
    Obj   trailer;
    Obj   root;
    std::vector<Page> pages;
    std::string err;
    std::set<int> objStmDone;

    // ── object access ────────────────────────────────────────────────────────
    const Obj* ObjByNum(int num) const {
        std::map<int, Obj>::const_iterator it = objs.find(num);
        return it == objs.end() ? nullptr : &it->second;
    }
    const Obj* Resolve(const Obj* o) const {
        int guard = 0;
        while (o && o->t == O_REF && guard++ < 32) o = ObjByNum(o->ref.num);
        return o;
    }
    const Obj* Get(const Obj* dict, const char* key) const {
        const Obj* o = Resolve(dict);
        if (!o || !o->IsDict()) return nullptr;
        return Resolve(o->Get(key));
    }
    double NumOf(const Obj* dict, const char* key, double def) const {
        const Obj* o = Get(dict, key);
        return (o && o->t == O_NUM) ? o->num : def;
    }

    // ── stream decoding ──────────────────────────────────────────────────────
    std::vector<uint8_t> StreamData(const Obj& s) const
    {
        std::vector<uint8_t> raw = RawBytes(s);

        const Obj* f = s.Get("Filter");
        f = f ? Resolve(f) : nullptr;
        std::vector<std::string> filters;
        if (f) {
            if (f->t == O_NAME) filters.push_back(f->s);
            else if (f->t == O_ARR)
                for (size_t i = 0; i < f->arr.size(); ++i) {
                    const Obj* fo = Resolve(&f->arr[i]);
                    if (fo && fo->t == O_NAME) filters.push_back(fo->s);
                }
        }
        const Obj* dp = s.Get("DecodeParms");
        dp = dp ? Resolve(dp) : nullptr;
        if (dp && dp->t == O_ARR) dp = dp->arr.empty() ? nullptr : Resolve(&dp->arr[0]);

        for (size_t k = 0; k < filters.size(); ++k) {
            const std::string& name = filters[k];
            std::vector<uint8_t> out;
            if (name == "FlateDecode" || name == "Fl") {
                if (!Inflate(raw.data(), raw.size(), out) &&
                    !Inflate(raw.data(), raw.size(), out, true)) return std::vector<uint8_t>();
                if (dp) {
                    int pred = (int)NumOf(dp, "Predictor", 1);
                    if (pred > 1)
                        Predictor(out, pred,
                                  (int)NumOf(dp, "Colors", 1),
                                  (int)NumOf(dp, "BitsPerComponent", 8),
                                  (int)NumOf(dp, "Columns", 1));
                }
            } else if (name == "ASCIIHexDecode" || name == "AHx") {
                AsciiHexDecode(raw.data(), raw.size(), out);
            } else if (name == "ASCII85Decode" || name == "A85") {
                Ascii85Decode(raw.data(), raw.size(), out);
            } else if (name == "RunLengthDecode" || name == "RL") {
                RunLengthDecode(raw.data(), raw.size(), out);
            } else {
                return std::vector<uint8_t>();   // DCTDecode / JPXDecode / LZW / CCITT
            }
            raw.swap(out);
        }
        return raw;
    }

    std::vector<uint8_t> RawBytes(const Obj& s) const
    {
        std::vector<uint8_t> raw;
        if (s.t == O_STREAM) {
            size_t off = s.streamOff, len = s.streamLen;
            if (off >= buf.size()) return raw;
            if (off + len > buf.size()) len = buf.size() - off;
            raw.assign(buf.begin() + off, buf.begin() + off + len);
        } else raw.assign(s.s.begin(), s.s.end());
        return raw;
    }

    std::string FirstFilter(const Obj& s) const
    {
        const Obj* f = Resolve(s.Get("Filter"));
        if (!f) return std::string();
        if (f->t == O_NAME) return f->s;
        if (f->t == O_ARR && !f->arr.empty()) {
            const Obj* f0 = Resolve(&f->arr[0]);
            if (f0 && f0->t == O_NAME) return f0->s;
        }
        return std::string();
    }

    // Decode image bytes. DCTDecode goes through the bundled baseline JPEG
    // decoder and brings its own dimensions; everything else is a bit-packed
    // sample array whose geometry comes from the dictionary.
    bool DecodeImage(const Obj& xo, std::vector<uint8_t>& out, int& w, int& h, int& nc) const
    {
        std::string filt = FirstFilter(xo);
        if (filt == "DCTDecode" || filt == "DCT") {
            std::vector<uint8_t> raw = RawBytes(xo);
            JpegImage img;
            if (!DecodeJpeg(raw.data(), raw.size(), img)) return false;
            out.swap(img.px);
            w = img.w; h = img.h; nc = img.nc;
            return true;
        }
        out = StreamData(xo);
        if (out.empty()) return false;
        w = (int)NumOf(&xo, "Width", 0);
        h = (int)NumOf(&xo, "Height", 0);
        nc = 0;                                  // caller derives components
        return w > 0 && h > 0;
    }

    // ── loading ──────────────────────────────────────────────────────────────
    size_t FindLast(const char* needle) const {
        size_t nl = strlen(needle);
        if (buf.size() < nl) return std::string::npos;
        for (size_t i = buf.size() - nl + 1; i-- > 0;)
            if (memcmp(buf.data() + i, needle, nl) == 0) return i;
        return std::string::npos;
    }

    // Parse "num gen obj … endobj" at `off`. Returns object number (0 = fail).
    int ParseIndirectAt(size_t off, Obj* out)
    {
        if (off >= buf.size()) return 0;
        Lexer lx(buf.data(), buf.size(), off);
        double num = 0, gen = 0;
        if (!lx.Number(num)) return 0;
        if (!lx.Number(gen)) return 0;
        std::string kw = lx.Word();
        if (kw != "obj") return 0;
        Obj o = lx.Parse();
        // stream?
        size_t save = lx.i;
        lx.Ws();
        bool isStream = false;
        if (lx.i + 6 <= buf.size() && memcmp(buf.data() + lx.i, "stream", 6) == 0) isStream = true;
        if (isStream) {
            size_t p = lx.i + 6;
            if (p < buf.size() && buf[p] == '\r') ++p;
            if (p < buf.size() && buf[p] == '\n') ++p;
            size_t start = p;
            size_t len = 0;
            const Obj* lo = o.Get("Length");
            bool haveLen = false;
            if (lo && lo->t == O_NUM) { len = (size_t)lo->num; haveLen = true; }
            else if (lo && lo->t == O_REF) {
                const Obj* lr = ObjByNum(lo->ref.num);
                if (lr && lr->t == O_NUM) { len = (size_t)lr->num; haveLen = true; }
            }
            if (haveLen && start + len <= buf.size()) {
                // trust /Length if "endstream" follows (allow EOL noise)
                size_t q = start + len;
                while (q < buf.size() && IsWS(buf[q])) ++q;
                if (q + 9 <= buf.size() && memcmp(buf.data() + q, "endstream", 9) == 0) {
                    o.t = O_STREAM;
                    o.streamOff = start;
                    o.streamLen = len;
                    if (out) *out = o;
                    return (int)num;
                }
            }
            // fall back to scanning for "endstream"
            size_t e = start;
            while (e + 9 <= buf.size() && memcmp(buf.data() + e, "endstream", 9) != 0) ++e;
            if (e + 9 > buf.size()) e = buf.size();
            size_t end = e;
            while (end > start && (buf[end - 1] == '\n' || buf[end - 1] == '\r')) --end;
            o.t = O_STREAM;
            o.streamOff = start;
            o.streamLen = end - start;
            (void)save;
            if (out) *out = o;
            return (int)num;
        }
        if (out) *out = o;
        return (int)num;
    }

    // Parse one xref section at `off`; returns trailer dict (O_NULL on failure).
    bool ReadXrefAt(size_t off, Obj& trailerOut, size_t& prevOff, bool& hadPrev, bool& isStream)
    {
        hadPrev = false;
        if (off >= buf.size()) return false;
        Lexer lx(buf.data(), buf.size(), off);
        lx.Ws();
        isStream = false;
        if (lx.i + 4 <= buf.size() && memcmp(buf.data() + lx.i, "xref", 4) == 0) {
            lx.i += 4;
            for (;;) {
                lx.Ws();
                if (lx.i >= buf.size()) break;
                if (memcmp(buf.data() + lx.i, "trailer", 7) == 0) { lx.i += 7; break; }
                double start = 0, count = 0;
                if (!lx.Number(start) || !lx.Number(count)) break;
                for (int k = 0; k < (int)count; ++k) {
                    lx.Ws();
                    double o1 = 0, o2 = 0;
                    if (!lx.Number(o1)) break;
                    if (!lx.Number(o2)) break;
                    std::string typ = lx.Word();
                    int n = (int)start + k;
                    if (typ == "n" && o1 > 0 && objs.find(n) == objs.end()) {
                        Obj o;
                        if (ParseIndirectAt((size_t)o1, &o) == n) objs[n] = o;
                    }
                }
            }
            Obj tr = lx.Parse();
            if (tr.IsDict()) {
                trailerOut = tr;
                const Obj* prev = tr.Get("Prev");
                if (prev && prev->t == O_NUM) { prevOff = (size_t)prev->num; hadPrev = true; }
                const Obj* xs = tr.Get("XRefStm");
                if (xs && xs->t == O_NUM) {
                    Obj t2; size_t p2; bool hp, st2;
                    ReadXrefAt((size_t)xs->num, t2, p2, hp, st2);
                }
                return true;
            }
            return false;
        }
        // xref stream
        Obj st;
        int num = ParseIndirectAt(off, &st);
        if (!num || st.t != O_STREAM) return false;
        isStream = true;
        const Obj* type = st.Get("Type");
        if (!type || !(type->t == O_NAME && type->s == "XRef")) return false;
        std::vector<uint8_t> data = StreamData(st);
        const Obj* w = st.Get("W");
        if (!w || w->t != O_ARR || w->arr.size() < 3) return false;
        int w0 = w->arr[0].Int(0), w1 = w->arr[1].Int(0), w2 = w->arr[2].Int(0);
        int rec = w0 + w1 + w2;
        if (rec <= 0) return false;

        std::vector<std::pair<int, int>> index;      // first, count
        const Obj* idx = st.Get("Index");
        if (idx && idx->t == O_ARR) {
            for (size_t i = 0; i + 1 < idx->arr.size(); i += 2)
                index.push_back(std::make_pair(idx->arr[i].Int(0), idx->arr[i + 1].Int(0)));
        } else {
            index.push_back(std::make_pair(0, (int)NumOf(&st, "Size", 0)));
        }
        size_t pos = 0;
        for (size_t ie = 0; ie < index.size(); ++ie) {
            for (int k = 0; k < index[ie].second; ++k) {
                if (pos + rec > data.size()) break;
                const uint8_t* f = data.data() + pos;
                pos += rec;
                unsigned long f0 = 0, f1 = 0, f2 = 0;
                for (int i = 0; i < w0; ++i) f0 = (f0 << 8) | f[i];
                for (int i = 0; i < w1; ++i) f1 = (f1 << 8) | f[w0 + i];
                for (int i = 0; i < w2; ++i) f2 = (f2 << 8) | f[w0 + w1 + i];
                if (w0 == 0) f0 = 1;                       // default type
                int n = index[ie].first + k;
                if (f0 == 1 && objs.find(n) == objs.end()) {
                    Obj o;
                    if (ParseIndirectAt((size_t)f1, &o) == n) objs[n] = o;
                } else if (f0 == 2) {
                    pendingStm[n] = std::make_pair((int)f1, (int)f2);
                }
            }
        }
        trailerOut = st;
        const Obj* prev = st.Get("Prev");
        if (prev && prev->t == O_NUM) { prevOff = (size_t)prev->num; hadPrev = true; }
        return true;
    }

    std::map<int, std::pair<int, int>> pendingStm;    // objnum -> (stm, index)

    void ExpandObjStreams()
    {
        std::map<int, std::vector<std::pair<int, size_t>>> groups;   // stm -> (objnum, offset)
        for (std::map<int, std::pair<int, int>>::iterator it = pendingStm.begin(); it != pendingStm.end(); ++it) {
            const Obj* st = ObjByNum(it->second.first);
            if (!st || st->t != O_STREAM) continue;
            std::vector<uint8_t> data = StreamData(*st);
            if (data.empty()) continue;
            int count = (int)NumOf(st, "N", 0), first = (int)NumOf(st, "First", 0);
            if (count <= 0) continue;
            Lexer lx(data.data(), data.size(), 0);
            std::vector<std::pair<int, size_t>> entries;
            for (int k = 0; k < count; ++k) {
                double on = 0, oo = 0;
                if (!lx.Number(on) || !lx.Number(oo)) break;
                entries.push_back(std::make_pair((int)on, (size_t)first + (size_t)oo));
            }
            groups[it->second.first] = entries;
            for (size_t k = 0; k < entries.size(); ++k) {
                int n = entries[k].first;
                if (objs.find(n) != objs.end()) continue;
                Lexer ol(data.data(), data.size(), entries[k].second);
                Obj v = ol.Parse();
                v.t = (v.t == O_KEYWORD) ? O_NULL : v.t;
                objs[n] = v;
            }
        }
        pendingStm.clear();
    }

    bool ReadXrefChain(size_t off)
    {
        std::set<size_t> seen;
        bool first = true;
        for (int guard = 0; guard < 64 && off != 0; ++guard) {
            if (seen.count(off)) break;
            seen.insert(off);
            Obj tr;
            size_t prev = 0;
            bool hadPrev = false, isStream = false;
            if (!ReadXrefAt(off, tr, prev, hadPrev, isStream)) return false;
            if (first) { trailer = tr; first = false; }
            else {
                for (size_t i = 0; i < tr.d.size(); ++i)
                    if (!trailer.Get(tr.d[i].first.c_str())) trailer.d.push_back(tr.d[i]);
            }
            if (!hadPrev) break;
            off = prev;
        }
        ExpandObjStreams();
        return true;
    }

    void Reconstruct()
    {
        // no usable xref: scan for "N G obj" headers, last occurrence wins
        size_t i = 0;
        while (i + 3 < buf.size()) {
            if (buf[i] == 'o' && buf[i + 1] == 'b' && buf[i + 2] == 'j' &&
                (i == 0 || IsWS(buf[i - 1]) || IsDelim(buf[i - 1]))) {
                // walk back: ws, gen digits, ws, num digits
                size_t q = i;
                while (q > 0 && IsWS(buf[q - 1])) --q;
                size_t ge = q;
                while (q > 0 && isdigit(buf[q - 1])) --q;
                if (ge == q) { ++i; continue; }
                while (q > 0 && IsWS(buf[q - 1])) --q;
                size_t ne = q;
                while (q > 0 && isdigit(buf[q - 1])) --q;
                if (ne == q) { ++i; continue; }
                size_t off = q;
                Obj o;
                int num = ParseIndirectAt(off, &o);
                if (num > 0) objs[num] = o;
                i += 3;
            } else ++i;
        }
        // trailer: prefer a real /trailer dict
        size_t t = FindLast("trailer");
        if (t != std::string::npos) {
            Lexer lx(buf.data(), buf.size(), t + 7);
            Obj tr = lx.Parse();
            if (tr.IsDict()) trailer = tr;
        }
        if (!trailer.Get("Root")) {
            for (std::map<int, Obj>::iterator it = objs.begin(); it != objs.end(); ++it) {
                const Obj* ty = it->second.Get("Type");
                if (ty && ty->t == O_NAME && ty->s == "Catalog") {
                    Obj r;
                    r.t = O_REF;
                    r.ref.num = it->first;
                    trailer.Set("Root", r);
                    break;
                }
            }
        }
        if (trailer.Get("Encrypt")) err = "encrypted PDF - not supported";
    }

    bool Load(const uint8_t* data, size_t len)
    {
        objs.clear();
        pages.clear();
        trailer = Obj();
        root = Obj();
        err.clear();
        buf.assign(data, data + len);
        if (buf.size() < 8) { err = "file too small"; return false; }

        size_t sx = FindLast("startxref");
        bool ok = false;
        if (sx != std::string::npos) {
            Lexer lx(buf.data(), buf.size(), sx + 9);
            double off = 0;
            if (lx.Number(off) && off > 0 && off < (double)buf.size())
                ok = ReadXrefChain((size_t)off);
        }
        if (!ok || objs.empty()) {
            objs.clear();
            trailer = Obj();
            Reconstruct();
        }
        const Obj* enc = trailer.Get("Encrypt");
        if (enc) { err = "encrypted PDF - not supported"; return false; }

        const Obj* r = Resolve(trailer.Get("Root"));
        if (!r || !r->IsDict()) { err = "no document catalog"; return false; }
        root = *r;

        // walk the page tree
        Obj pagesNode = root.Get("Pages") ? *root.Get("Pages") : Obj();
        std::set<int> seenRefs;
        struct Inherit { bool mb; const Obj* mbObj; int rot; const Obj* res; };
        CollectPages(pagesNode, nullptr, nullptr, 0, 0, seenRefs);
        if (pages.empty()) { err = "no pages"; return false; }
        return true;
    }

    void CollectPages(const Obj& node, const Obj* inhMb, const Obj* inhRes, int inhRot, int depth, std::set<int>& seen)
    {
        if (depth > 64) return;
        const Obj* n = Resolve(&node);
        if (!n || !n->IsDict()) return;
        const Obj* kids = Get(n, "Kids");
        const Obj* mb = Get(n, "MediaBox");
        const Obj* res = Get(n, "Resources");
        int rot = (int)NumOf(n, "Rotate", 999);
        if (!mb) mb = inhMb;
        if (!res) res = inhRes;
        if (rot == 999) rot = inhRot;

        if (node.t == O_REF) {
            if (seen.count(node.ref.num)) return;
            seen.insert(node.ref.num);
        }
        const Obj* type = Get(n, "Type");
        bool isPage = (type && type->t == O_NAME && type->s == "Page") || (!kids && mb);
        if (isPage && mb) {
            Page p;
            p.dict = *n;
            if (res) p.resources = *res;
            if (mb->t == O_ARR && mb->arr.size() == 4) {
                for (int i = 0; i < 4; ++i) p.mb[i] = (float)Resolve(&mb->arr[i])->Num(p.mb[i]);
                if (p.mb[2] < p.mb[0]) std::swap(p.mb[0], p.mb[2]);
                if (p.mb[3] < p.mb[1]) std::swap(p.mb[1], p.mb[3]);
            }
            p.rot = ((rot % 360) + 360) % 360;
            if (p.rot % 90) p.rot = 0;
            pages.push_back(p);
            seen.erase(node.t == O_REF ? node.ref.num : -1);
            return;
        }
        if (!kids || kids->t != O_ARR) return;
        for (size_t i = 0; i < kids->arr.size(); ++i)
            CollectPages(kids->arr[i], mb, res, rot, depth + 1, seen);
        if (node.t == O_REF) seen.erase(node.ref.num);
    }

    int PageCount() const { return (int)pages.size(); }

    void PageSize(int idx, float& w, float& h) const {
        if (idx < 0 || idx >= (int)pages.size()) { w = h = 0; return; }
        const Page& p = pages[idx];
        w = p.mb[2] - p.mb[0];
        h = p.mb[3] - p.mb[1];
        if (p.rot == 90 || p.rot == 270) std::swap(w, h);
    }

};

// ─────────────────────────────────────────────────────────────────────────────
// Canvas: RGBA integer buffer, software scanline rasterizer
// ─────────────────────────────────────────────────────────────────────────────
struct Canvas {
    int w = 0, h = 0;
    std::vector<uint8_t> px;             // RGBA8, white page
    float cx0 = 0, cy0 = 0, cx1 = 0, cy1 = 0;   // clip, device pixels

    void Init(int W, int H) {
        w = W > 0 ? W : 1;
        h = H > 0 ? H : 1;
        px.assign((size_t)w * h * 4, 255);
        mCov.assign((size_t)w, 0.f);
        cx0 = 0; cy0 = 0; cx1 = (float)w; cy1 = (float)h;
    }
    void SetClip(float x0, float y0, float x1, float y1) {
        cx0 = std::max(cx0, x0); cy0 = std::max(cy0, y0);
        cx1 = std::min(cx1, x1); cy1 = std::min(cy1, y1);
    }
    void ResetClip() { cx0 = 0; cy0 = 0; cx1 = (float)w; cy1 = (float)h; }

    inline void Blend(int x, int y, float r, float g, float b, float a) {
        if (a <= 0.f || x < 0 || y < 0 || x >= w || y >= h) return;
        if (a > 1.f) a = 1.f;
        uint8_t* p = &px[((size_t)y * w + x) * 4];
        p[0] = (uint8_t)(p[0] + (r * 255.f - p[0]) * a + 0.5f);
        p[1] = (uint8_t)(p[1] + (g * 255.f - p[1]) * a + 0.5f);
        p[2] = (uint8_t)(p[2] + (b * 255.f - p[2]) * a + 0.5f);
        p[3] = 255;
    }

    struct Poly { std::vector<Pt> pts; bool closed = true; };

    std::vector<float> mCov;            // per-row coverage scratch (size w)

    // Flatten + fill a set of polygons. `normWinding` re-orients each polygon
    // (used for stroke outlines); glyph contours must keep their direction so
    // nonzero winding keeps the counters (holes) open.
    void Fill(const std::vector<Poly>& polys, bool evenOdd, const float col[3],
              float alpha = 1.f, bool normWinding = false)
    {
        struct Edge { float x0, y0, x1, y1; int dir; };
        std::vector<Edge> edges;
        float ymin = 1e30f, ymax = -1e30f;
        for (size_t pi = 0; pi < polys.size(); ++pi) {
            std::vector<Pt> pts = polys[pi].pts;
            if (pts.size() < 2) continue;
            if (normWinding) {
                double area = 0;
                for (size_t i = 0; i < pts.size(); ++i) {
                    const Pt& a = pts[i];
                    const Pt& b = pts[(i + 1) % pts.size()];
                    area += (double)a.x * b.y - (double)b.x * a.y;
                }
                if (area < 0) std::reverse(pts.begin(), pts.end());
            }
            size_t cnt = pts.size();
            size_t last = polys[pi].closed ? cnt : cnt - 1;
            for (size_t i = 0; i < last; ++i) {
                const Pt& a = pts[i];
                const Pt& b = pts[(i + 1) % cnt];
                if (a.y == b.y) continue;
                Edge e;
                e.x0 = a.x; e.y0 = a.y; e.x1 = b.x; e.y1 = b.y;
                e.dir = (b.y > a.y) ? 1 : -1;
                edges.push_back(e);
                if (a.y < ymin) ymin = a.y;
                if (a.y > ymax) ymax = a.y;
            }
        }
        if (edges.empty()) return;
        ymin = std::max(ymin, std::max(0.f, cy0));
        ymax = std::min(ymax, std::min((float)h, cy1));
        if (ymax <= ymin) return;
        if ((int)mCov.size() != w) mCov.assign((size_t)w, 0.f);

        std::vector<float> xs;
        std::vector<int>   dirs;
        int y0 = (int)floorf(ymin), y1 = (int)ceilf(ymax);
        for (int y = y0; y < y1; ++y) {
            bool any = false;
            for (int s = 0; s < 4; ++s) {
                float sy = (float)y + (s + 0.5f) * 0.25f;
                xs.clear(); dirs.clear();
                for (size_t i = 0; i < edges.size(); ++i) {
                    const Edge& e = edges[i];
                    if ((e.y0 <= sy) == (e.y1 <= sy)) continue;
                    float t = (sy - e.y0) / (e.y1 - e.y0);
                    xs.push_back(e.x0 + (e.x1 - e.x0) * t);
                    dirs.push_back(e.dir);
                }
                if (xs.size() < 2) continue;
                for (size_t i = 1; i < xs.size(); ++i) {     // insertion sort
                    float xv = xs[i]; int dv = dirs[i];
                    size_t j = i;
                    while (j > 0 && xs[j - 1] > xv) { xs[j] = xs[j - 1]; dirs[j] = dirs[j - 1]; --j; }
                    xs[j] = xv; dirs[j] = dv;
                }
                if (evenOdd) {
                    for (size_t i = 0; i + 1 < xs.size(); i += 2)
                        AddSpan(mCov, xs[i], xs[i + 1], 0.25f);
                } else {
                    int wind = 0;
                    bool inSpan = false;
                    float spanX = 0.f;
                    for (size_t i = 0; i < xs.size(); ++i) {
                        wind += dirs[i];
                        bool now = wind != 0;
                        if (now && !inSpan) { spanX = xs[i]; inSpan = true; }
                        else if (!now && inSpan) { AddSpan(mCov, spanX, xs[i], 0.25f); inSpan = false; }
                    }
                }
                any = true;
            }
            if (!any) continue;
            int xa = (int)floorf(std::max(0.f, cx0)), xb = (int)ceilf(std::min((float)w, cx1));
            if (xa < 0) xa = 0;
            if (xb > w) xb = w;
            for (int x = xa; x < xb; ++x) {
                float a = mCov[x] * alpha;
                mCov[x] = 0.f;
                if (a <= 0.002f) continue;
                Blend(x, y, col[0], col[1], col[2], a);
            }
            std::fill(mCov.begin(), mCov.end(), 0.f);
        }
    }

    static void AddSpan(std::vector<float>& cov, float xa, float xb, float wt)
    {
        if (xb <= xa) return;
        int ia = (int)floorf(xa), ib = (int)floorf(xb);
        int W = (int)cov.size();
        if (ia < 0) ia = 0;
        if (ib >= W) ib = W - 1;
        if (ia == ib) {
            if (ia >= 0 && ia < W) cov[ia] += (xb - xa) * wt;
            return;
        }
        if (ia >= 0 && ia < W) cov[ia] += ((float)(ia + 1) - xa) * wt;
        for (int x = ia + 1; x < ib; ++x) cov[x] += wt;
        if (ib >= 0 && ib < W) cov[ib] += (xb - (float)ib) * wt;
    }

    // Nearest-neighbour-free image draw: bilinear sample of an `nc`-component
    // image into the parallelogram given by `m` (unit square → device).
    void DrawImage(const std::vector<uint8_t>& img, int iw, int ih, int nc,
                   const Mat& m, const float col[3], bool stencil, float alpha = 1.f)
    {
        Mat inv;
        if (!Invert(m, inv)) return;
        float xs[4], ys[4];
        Apply(m, 0, 0, xs[0], ys[0]);
        Apply(m, 1, 0, xs[1], ys[1]);
        Apply(m, 1, 1, xs[2], ys[2]);
        Apply(m, 0, 1, xs[3], ys[3]);
        float x0 = std::min(std::min(xs[0], xs[1]), std::min(xs[2], xs[3]));
        float x1 = std::max(std::max(xs[0], xs[1]), std::max(xs[2], xs[3]));
        float y0 = std::min(std::min(ys[0], ys[1]), std::min(ys[2], ys[3]));
        float y1 = std::max(std::max(ys[0], ys[1]), std::max(ys[2], ys[3]));
        int px0 = (int)floorf(std::max(x0, std::max(0.f, cx0)));
        int px1 = (int)ceilf(std::min(x1, std::min((float)w, cx1)));
        int py0 = (int)floorf(std::max(y0, std::max(0.f, cy0)));
        int py1 = (int)ceilf(std::min(y1, std::min((float)h, cy1)));
        if (px1 <= px0 || py1 <= py0) return;
        for (int y = py0; y < py1; ++y) {
            for (int x = px0; x < px1; ++x) {
                float u, v;
                Apply(inv, x + 0.5f, y + 0.5f, u, v);
                if (u < 0.f || u >= 1.f || v < 0.f || v >= 1.f) continue;
                // PDF image space: row 0 is the top edge, so flip v
                float fx = u * (iw - 1), fy = (1.f - v) * (ih - 1);
                int ix = (int)fx, iy = (int)fy;
                int ix1 = std::min(ix + 1, iw - 1), iy1 = std::min(iy + 1, ih - 1);
                float tx = fx - ix, ty = fy - iy;
                float c[4] = { 0, 0, 0, 1 };
                for (int k = 0; k < nc && k < 4; ++k) {
                    float a0 = img[((size_t)iy * iw + ix) * nc + k];
                    float a1 = img[((size_t)iy * iw + ix1) * nc + k];
                    float b0 = img[((size_t)iy1 * iw + ix) * nc + k];
                    float b1 = img[((size_t)iy1 * iw + ix1) * nc + k];
                    c[k] = ((a0 * (1 - tx) + a1 * tx) * (1 - ty) + (b0 * (1 - tx) + b1 * tx) * ty) / 255.f;
                }
                if (stencil) Blend(x, y, col[0], col[1], col[2], c[0] * alpha);
                else if (nc == 1) Blend(x, y, c[0], c[0], c[0], alpha);
                else if (nc == 2) Blend(x, y, c[0], c[0], c[0], c[1] * alpha);
                else if (nc == 3) Blend(x, y, c[0], c[1], c[2], alpha);
                else if (nc >= 4) Blend(x, y, c[0], c[1], c[2], c[3] * alpha);
            }
        }
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Encoding tables and glyph names
// ─────────────────────────────────────────────────────────────────────────────
static const unsigned short kWinAnsiHigh[128] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
    0x00A0, 0x00A1, 0x00A2, 0x00A3, 0x00A4, 0x00A5, 0x00A6, 0x00A7,
    0x00A8, 0x00A9, 0x00AA, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x00AF,
    0x00B0, 0x00B1, 0x00B2, 0x00B3, 0x00B4, 0x00B5, 0x00B6, 0x00B7,
    0x00B8, 0x00B9, 0x00BA, 0x00BB, 0x00BC, 0x00BD, 0x00BE, 0x00BF,
    0x00C0, 0x00C1, 0x00C2, 0x00C3, 0x00C4, 0x00C5, 0x00C6, 0x00C7,
    0x00C8, 0x00C9, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF,
    0x00D0, 0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D5, 0x00D6, 0x00D7,
    0x00D8, 0x00D9, 0x00DA, 0x00DB, 0x00DC, 0x00DD, 0x00DE, 0x00DF,
    0x00E0, 0x00E1, 0x00E2, 0x00E3, 0x00E4, 0x00E5, 0x00E6, 0x00E7,
    0x00E8, 0x00E9, 0x00EA, 0x00EB, 0x00EC, 0x00ED, 0x00EE, 0x00EF,
    0x00F0, 0x00F1, 0x00F2, 0x00F3, 0x00F4, 0x00F5, 0x00F6, 0x00F7,
    0x00F8, 0x00F9, 0x00FA, 0x00FB, 0x00FC, 0x00FD, 0x00FE, 0x00FF,
};

struct NameUni { const char* n; unsigned short u; };
static const NameUni kGlyphNames[] = {
    {"space",32},{"exclam",33},{"quotedbl",34},{"numbersign",35},{"dollar",36},{"percent",37},
    {"ampersand",38},{"quotesingle",39},{"parenleft",40},{"parenright",41},{"asterisk",42},{"plus",43},
    {"comma",44},{"hyphen",45},{"minus",45},{"period",46},{"slash",47},{"zero",48},{"one",49},{"two",50},
    {"three",51},{"four",52},{"five",53},{"six",54},{"seven",55},{"eight",56},{"nine",57},{"colon",58},
    {"semicolon",59},{"less",60},{"equal",61},{"greater",62},{"question",63},{"at",64},
    {"bracketleft",91},{"backslash",92},{"bracketright",93},{"asciicircum",94},{"underscore",95},
    {"grave",96},{"quoteleft",0x2018},{"quoteright",0x2019},{"quotedblleft",0x201C},{"quotedblright",0x201D},
    {"braceleft",123},{"bar",124},{"braceright",125},{"asciitilde",126},
    {"bullet",0x2022},{"endash",0x2013},{"emdash",0x2014},{"ellipsis",0x2026},{"dagger",0x2020},
    {"daggerdbl",0x2021},{"perthousand",0x2030},{"guilsinglleft",0x2039},{"guilsinglright",0x203A},
    {"quotesinglbase",0x201A},{"quotedblbase",0x201E},{"florin",0x0192},{"fraction",0x2044},
    {"guillemotleft",0x00AB},{"guillemotright",0x00BB},{"Euro",0x20AC},{"trademark",0x2122},
    {"circumflex",0x02C6},{"tilde",0x02DC},{"caron",0x02C7},{"breve",0x02D8},{"dotaccent",0x02D9},
    {"ring",0x02DA},{"ogonek",0x02DB},{"hungarumlaut",0x02DD},{"germandbls",0x00DF},
    {"AE",0x00C6},{"ae",0x00E6},{"OE",0x0152},{"oe",0x0153},{"Oslash",0x00D8},{"oslash",0x00F8},
    {"Lslash",0x0141},{"lslash",0x0142},{"dotlessi",0x0131},{"Scaron",0x0160},{"scaron",0x0161},
    {"Zcaron",0x017D},{"zcaron",0x017E},{"Ydieresis",0x0178},{"fi",0xFB01},{"fl",0xFB02},
    {"ff",0xFB00},{"ffi",0xFB03},{"ffl",0xFB04},{"degree",0x00B0},{"plusminus",0x00B1},
    {"multiply",0x00D7},{"divide",0x00F7},{"mu",0x00B5},{"copyright",0x00A9},{"registered",0x00AE},
    {"section",0x00A7},{"paragraph",0x00B6},{"periodcentered",0x00B7},{"nbspace",0x00A0},
    {"sfthyphen",0x00AD},{"logicalnot",0x00AC},{"brokenbar",0x00A6},{"currency",0x00A4},
    {"cent",0x00A2},{"sterling",0x00A3},{"yen",0x00A5},{"onehalf",0x00BD},{"onequarter",0x00BC},
    {"threequarters",0x00BE},{"onesuperior",0x00B9},{"twosuperior",0x00B2},{"threesuperior",0x00B3},
    {"ordfeminine",0x00AA},{"ordmasculine",0x00BA},{"exclamdown",0x00A1},{"questiondown",0x00BF},
    {"Aacute",0x00C1},{"aacute",0x00E1},{"Acircumflex",0x00C2},{"acircumflex",0x00E2},
    {"Adieresis",0x00C4},{"adieresis",0x00E4},{"Agrave",0x00C0},{"agrave",0x00E0},
    {"Aring",0x00C5},{"aring",0x00E5},{"Atilde",0x00C3},{"atilde",0x00E3},
    {"Ccedilla",0x00C7},{"ccedilla",0x00E7},{"Eacute",0x00C9},{"eacute",0x00E9},
    {"Ecircumflex",0x00CA},{"ecircumflex",0x00EA},{"Edieresis",0x00CB},{"edieresis",0x00EB},
    {"Egrave",0x00C8},{"egrave",0x00E8},{"Iacute",0x00CD},{"iacute",0x00ED},
    {"Icircumflex",0x00CE},{"icircumflex",0x00EE},{"Idieresis",0x00CF},{"idieresis",0x00EF},
    {"Igrave",0x00CC},{"igrave",0x00EC},{"Ntilde",0x00D1},{"ntilde",0x00F1},
    {"Oacute",0x00D3},{"oacute",0x00F3},{"Ocircumflex",0x00D4},{"ocircumflex",0x00F4},
    {"Odieresis",0x00D6},{"odieresis",0x00F6},{"Ograve",0x00D2},{"ograve",0x00F2},
    {"Otilde",0x00D5},{"otilde",0x00F5},{"Uacute",0x00DA},{"uacute",0x00FA},
    {"Ucircumflex",0x00DB},{"ucircumflex",0x00FB},{"Udieresis",0x00DC},{"udieresis",0x00FC},
    {"Ugrave",0x00D9},{"ugrave",0x00F9},{"Yacute",0x00DD},{"yacute",0x00FD},
    {"ydieresis",0x00FF},{"germandbls",0x00DF},{"Thorn",0x00DE},{"thorn",0x00FE},
    {"Eth",0x00D0},{"eth",0x00F0},
    // common math / symbols seen in technical PDFs (mapped for the substitute face)
    {"Sigma",0x03A3},{"Pi",0x03A0},{"Omega",0x03A9},{"summation",0x2211},{"product",0x220F},
    {"integral",0x222B},{"radical",0x221A},{"infinity",0x221E},{"lessequal",0x2264},
    {"greaterequal",0x2265},{"notequal",0x2260},{"approxequal",0x2248},{"arrowright",0x2192},
    {"arrowleft",0x2190},{"partialdiff",0x2202},{"element",0x2208},{"universal",0x2200},
    {"existential",0x2203},{"nabla",0x2207},{"angle",0x2220},{"gradient",0x2207},
    {"alpha",0x03B1},{"beta",0x03B2},{"gamma",0x03B3},{"delta",0x03B4},{"epsilon",0x03B5},
    {"zeta",0x03B6},{"eta",0x03B7},{"theta",0x03B8},{"iota",0x03B9},{"kappa",0x03BA},
    {"lambda",0x03BB},{"nu",0x03BD},{"xi",0x03BE},{"omicron",0x03BF},{"pi",0x03C0},
    {"rho",0x03C1},{"sigma",0x03C3},{"tau",0x03C4},{"upsilon",0x03C5},{"phi",0x03C6},
    {"chi",0x03C7},{"psi",0x03C8},{"omega",0x03C9},{"Delta",0x0394},{"Theta",0x0398},
    {"Lambda",0x039B},{"Xi",0x039E},{"Phi",0x03A6},{"Psi",0x03A8},
};

inline int GlyphNameToUni(const std::string& nm)
{
    if (nm.size() == 1) return (unsigned char)nm[0];
    if (nm.size() > 3 && nm.compare(0, 3, "uni") == 0) {
        int v = 0;
        for (size_t i = 3; i < nm.size() && i < 7; ++i) {
            char c = nm[i];
            int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'A' && c <= 'F') ? c - 'A' + 10
                  : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
            if (d < 0) { v = 0; break; }
            v = v * 16 + d;
        }
        if (v) return v;
    }
    if (nm.size() > 1 && nm[0] == 'u') {
        int v = 0; bool ok = true;
        for (size_t i = 1; i < nm.size() && i < 6; ++i) {
            char c = nm[i];
            int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'A' && c <= 'F') ? c - 'A' + 10
                  : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : -1;
            if (d < 0) { ok = false; break; }
            v = v * 16 + d;
        }
        if (ok && v) return v;
    }
    for (size_t i = 0; i < sizeof(kGlyphNames) / sizeof(kGlyphNames[0]); ++i)
        if (nm == kGlyphNames[i].n) return kGlyphNames[i].u;
    // accented names built as base + accent (e.g. "eacute" handled above; "uniXXXX" above)
    return 0;
}

// UTF-16BE (as used by CMap destination strings) → UTF-8
inline void AppendUtf8(std::string& out, unsigned int cp)
{
    if (cp < 0x80) out.push_back((char)cp);
    else if (cp < 0x800) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

inline std::string Utf16BeToUtf8(const std::string& s)
{
    std::string out;
    for (size_t i = 0; i + 1 < s.size(); i += 2) {
        unsigned int c = ((unsigned char)s[i] << 8) | (unsigned char)s[i + 1];
        if (c >= 0xD800 && c < 0xDC00 && i + 3 < s.size()) {
            unsigned int lo = ((unsigned char)s[i + 2] << 8) | (unsigned char)s[i + 3];
            c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
            i += 2;
        }
        AppendUtf8(out, c);
    }
    if (s.size() == 1) AppendUtf8(out, (unsigned char)s[0]);
    return out;
}

// First codepoint of a UTF-8 string
inline unsigned int FirstCodepoint(const std::string& s)
{
    if (s.empty()) return 0;
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) return c;
    if ((c & 0xE0) == 0xC0 && s.size() >= 2)
        return ((c & 0x1F) << 6) | ((unsigned char)s[1] & 0x3F);
    if ((c & 0xF0) == 0xE0 && s.size() >= 3)
        return ((c & 0x0F) << 12) | (((unsigned char)s[1] & 0x3F) << 6) | ((unsigned char)s[2] & 0x3F);
    if ((c & 0xF8) == 0xF0 && s.size() >= 4)
        return ((c & 0x07) << 18) | (((unsigned char)s[1] & 0x3F) << 12)
             | (((unsigned char)s[2] & 0x3F) << 6) | ((unsigned char)s[3] & 0x3F);
    return c;
}

// ─────────────────────────────────────────────────────────────────────────────
// Fonts
// ─────────────────────────────────────────────────────────────────────────────
struct Font {
    std::string base;
    bool  twoByte = false;
    bool  hasProg = false;                  // usable TrueType program
    stbtt_fontinfo tt;
    std::vector<uint8_t> prog;
    float w1k[256];                         // simple-font widths, 1/1000 em
    float missingW = 0;
    std::map<int, float> wcid;
    float dw = 1000.f;
    int   enc[256];
    std::map<int, int> cid2gid;
    std::map<int, std::string> tou;         // code -> UTF-8 text (ToUnicode CMap)
    // substitute face (bundled metric-compatible TTFs)
    mutable stbtt_fontinfo sub;
    mutable bool subInit = false;
    mutable const unsigned char* subData = nullptr;
    mutable stbtt_fontinfo sub2;            // wide-coverage fallback (symbols, maths)
    mutable bool sub2Init = false;
    mutable const unsigned char* sub2Data = nullptr;
    int   subFam = 3, subFace = 0;

    Font() {
        for (int i = 0; i < 256; ++i) { w1k[i] = 0; enc[i] = 0; }
    }

    std::string TextOf(int code) const {
        std::map<int, std::string>::const_iterator it = tou.find(code);
        if (it != tou.end() && !it->second.empty()) return it->second;
        std::string one;
        int u = UniOf(code);
        if (u) AppendUtf8(one, (unsigned)u);
        return one;
    }
    int UniOf(int code) const {
        std::map<int, std::string>::const_iterator it = tou.find(code);
        if (it != tou.end() && !it->second.empty()) return (int)FirstCodepoint(it->second);
        if (code >= 0 && code < 256) {
            if (enc[code]) return enc[code];
            if (code >= 0x20 && code < 0x7F) return code;
            if (code >= 0xA0) return code;
        }
        return 0;
    }
    float WidthOf(int code) const {
        if (twoByte) {
            std::map<int, float>::const_iterator it = wcid.find(code);
            return it == wcid.end() ? dw : it->second;
        }
        if (code >= 0 && code < 256 && w1k[code] > 0) return w1k[code];
        return missingW > 0 ? missingW : 500.f;
    }
    int GidOf(int code) const {
        if (twoByte) {
            std::map<int, int>::const_iterator it = cid2gid.find(code);
            if (it != cid2gid.end()) return it->second;
            return code;
        }
        return -1;
    }

    // Substitute-face glyph for a Unicode codepoint.
    void OutlineUni(unsigned int uni, const Mat& trm, std::vector<Canvas::Poly>& out) const
    {
        if (!uni) return;
        if (!subInit) {
            const EmbeddedFace& f = kFontFaces[subFam < kDocFamilyCount ? subFam : 3][subFace & 3];
            subData = f.data;
            if (!f.data || !stbtt_InitFont(&sub, f.data, stbtt_GetFontOffsetForIndex(f.data, 0))) subData = nullptr;
            subInit = true;
        }
        if (!subData) { if (getenv("PDFR_TRACE_GLYPH")) fprintf(stderr, "  uni=0x%x NO SUB DATA\n", uni); return; }
        int gid = stbtt_FindGlyphIndex(&sub, (int)uni);
        if (getenv("PDFR_TRACE_GLYPH") && uni < 0x100) fprintf(stderr, "  uni=0x%x gid=%d out0=%zu\n", uni, gid, out.size());
        if (gid <= 0) return;
        EmitGlyph(&sub, gid, trm, out);
    }

    // Append the outline (in em units, y up) transformed by `trm`.
    void Outline(int code, const Mat& trm, std::vector<Canvas::Poly>& out) const
    {
        if (hasProg) {
            int gid = -1;
            if (twoByte) gid = GidOf(code);
            else {
                int uni = UniOf(code);
                if (uni) gid = stbtt_FindGlyphIndex(&tt, uni);
            }
            if (gid > 0) { EmitGlyph(&tt, gid, trm, out); return; }
        }
        OutlineUni((unsigned)UniOf(code), trm, out);
    }

    // A single code can carry several codepoints (ToUnicode ligatures): lay
    // them out across the advance instead of dropping the glyphs.
    void OutlineAdvance(int code, const Mat& trm, std::vector<Canvas::Poly>& out) const
    {
        std::string txt = TextOf(code);
        if (hasProg || txt.size() <= 1) { Outline(code, trm, out); return; }
        std::vector<unsigned int> cps;
        for (size_t i = 0; i < txt.size();) {
            unsigned char c = (unsigned char)txt[i];
            size_t len = (c < 0x80) ? 1 : ((c & 0xE0) == 0xC0) ? 2 : ((c & 0xF0) == 0xE0) ? 3 : 4;
            if (i + len > txt.size()) break;
            cps.push_back(FirstCodepoint(txt.substr(i, len)));
            i += len;
        }
        if (cps.size() <= 1) { OutlineUni(cps.empty() ? 0u : cps[0], trm, out); return; }
        float w0 = WidthOf(code) / 1000.f;
        float step = w0 / (float)cps.size();
        for (size_t k = 0; k < cps.size(); ++k) {
            Mat off;
            off.e = step * (float)k;
            OutlineUni(cps[k], Concat(off, trm), out);
        }
    }

    void EmitGlyph(const stbtt_fontinfo* fi, int gid, const Mat& trm, std::vector<Canvas::Poly>& out) const
    {
        stbtt_vertex* verts = nullptr;
        int nv = stbtt_GetGlyphShape(fi, gid, &verts);
        if (nv <= 0) { if (verts) stbtt_FreeShape(fi, verts); return; }
        float uscale = stbtt_ScaleForMappingEmToPixels(fi, 1.0f);   // 1/unitsPerEm
        Canvas::Poly cur;
        bool open = false;
        Pt lastPt;
        for (int i = 0; i < nv; ++i) {
            const stbtt_vertex& v = verts[i];
            float gx = v.x * uscale, gy = v.y * uscale;
            if (v.type == STBTT_vmove) {
                if (open && cur.pts.size() > 1) out.push_back(cur);
                cur.pts.clear();
                cur.closed = true;
                float dx, dy; Apply(trm, gx, gy, dx, dy);
                cur.pts.push_back({ dx, dy });
                lastPt = { gx, gy };
                open = true;
            } else if (v.type == STBTT_vline) {
                float dx, dy; Apply(trm, gx, gy, dx, dy);
                cur.pts.push_back({ dx, dy });
                lastPt = { gx, gy };
            } else if (v.type == STBTT_vcurve) {
                float ex = v.cx * uscale, ey = v.cy * uscale;
                float c1x = lastPt.x + (2.f / 3.f) * (gx - lastPt.x);
                float c1y = lastPt.y + (2.f / 3.f) * (gy - lastPt.y);
                float c2x = ex + (2.f / 3.f) * (gx - ex);
                float c2y = ey + (2.f / 3.f) * (gy - ey);
                EmitCubic(trm, lastPt.x, lastPt.y, c1x, c1y, c2x, c2y, ex, ey, cur.pts);
                lastPt = { ex, ey };
            } else if (v.type == STBTT_vcubic) {
                float c1x = gx, c1y = gy;
                float c2x = v.cx * uscale, c2y = v.cy * uscale;
                float ex = v.cx1 * uscale, ey = v.cy1 * uscale;
                EmitCubic(trm, lastPt.x, lastPt.y, c1x, c1y, c2x, c2y, ex, ey, cur.pts);
                lastPt = { ex, ey };
            }
        }
        if (open && cur.pts.size() > 1) out.push_back(cur);
        stbtt_FreeShape(fi, verts);
    }

    static void EmitCubic(const Mat& m, float x0, float y0, float x1, float y1,
                          float x2, float y2, float x3, float y3, std::vector<Pt>& pts)
    {
        // adaptive-ish uniform subdivision in device space
        float dx0, dy0, dx1, dy1, dx2, dy2, dx3, dy3;
        Apply(m, x0, y0, dx0, dy0);
        Apply(m, x1, y1, dx1, dy1);
        Apply(m, x2, y2, dx2, dy2);
        Apply(m, x3, y3, dx3, dy3);
        float len = fabsf(dx1 - dx0) + fabsf(dy1 - dy0) + fabsf(dx2 - dx1) + fabsf(dy2 - dy1)
                  + fabsf(dx3 - dx2) + fabsf(dy3 - dy2);
        int steps = (int)(len * 0.5f);
        if (steps < 3) steps = 3;
        if (steps > 24) steps = 24;
        for (int i = 1; i <= steps; ++i) {
            float t = (float)i / steps, u = 1 - t;
            float x = u * u * u * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x3;
            float y = u * u * u * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y3;
            float px, py; Apply(m, x, y, px, py);
            pts.push_back({ px, py });
        }
    }
};

// ToUnicode CMap: bfchar / bfrange. Values may be multi-codepoint (ligatures
// such as "fi"), so destinations are kept as UTF-8 strings.
inline void ParseToUnicode(const std::vector<uint8_t>& d, std::map<int, std::string>& out)
{
    Lexer lx(d.data(), d.size(), 0);
    std::string mode;
    std::vector<Obj> toks;
    for (;;) {
        if (lx.Eof()) break;
        Obj o = lx.Parse();
        if (o.t == O_KEYWORD) {
            if (o.s == "beginbfchar") mode = "char";
            else if (o.s == "endbfchar") mode.clear();
            else if (o.s == "beginbfrange") mode = "range";
            else if (o.s == "endbfrange") mode.clear();
            else if (o.s == "begincodespacerange") mode = "space";
            else if (o.s == "endcodespacerange") mode.clear();
            toks.clear();
            continue;
        }
        if (mode == "char") {
            toks.push_back(o);
            if (toks.size() == 2) {
                int src = 0;
                if (toks[0].t == O_STR) for (size_t i = 0; i < toks[0].s.size(); ++i) src = src * 256 + (unsigned char)toks[0].s[i];
                else if (toks[0].t == O_NUM) src = (int)toks[0].num;
                std::string dst;
                if (toks[1].t == O_STR) dst = Utf16BeToUtf8(toks[1].s);
                else if (toks[1].t == O_NAME) { int u = GlyphNameToUni(toks[1].s); if (u) AppendUtf8(dst, u); }
                else if (toks[1].t == O_NUM) AppendUtf8(dst, (unsigned)toks[1].num);
                if (!dst.empty()) out[src] = dst;
                toks.clear();
            }
        } else if (mode == "range") {
            toks.push_back(o);
            if (toks.size() == 3) {
                int lo = 0, hi = 0;
                if (toks[0].t == O_STR) for (size_t i = 0; i < toks[0].s.size(); ++i) lo = lo * 256 + (unsigned char)toks[0].s[i];
                if (toks[1].t == O_STR) for (size_t i = 0; i < toks[1].s.size(); ++i) hi = hi * 256 + (unsigned char)toks[1].s[i];
                if (hi >= lo && hi - lo < 4096) {
                    if (toks[2].t == O_ARR) {
                        for (size_t k = 0; k < toks[2].arr.size() && lo + (int)k <= hi; ++k) {
                            const Obj& v = toks[2].arr[k];
                            if (v.t == O_STR) {
                                std::string dst = Utf16BeToUtf8(v.s);
                                if (!dst.empty()) out[lo + (int)k] = dst;
                            }
                        }
                    } else if (toks[2].t == O_STR && !toks[2].s.empty()) {
                        std::string base = Utf16BeToUtf8(toks[2].s);
                        unsigned int b = FirstCodepoint(base);
                        for (int c = lo; c <= hi; ++c) {
                            std::string one;
                            AppendUtf8(one, b + (unsigned)(c - lo));
                            out[c] = one;
                        }
                    }
                }
                toks.clear();
            }
        } else {
            toks.clear();
        }
    }
}

struct FontCache {
    const Doc* doc;
    std::map<int, Font> cache;             // keyed by font dict object number (0 = direct dict)
    std::list<Font> direct;                // stable addresses for direct (inline) font dicts

    Font* Get(const Obj* fdObj)
    {
        const Obj* fd = doc->Resolve(fdObj);
        if (!fd || !fd->IsDict()) return nullptr;
        int key = (fdObj && fdObj->t == O_REF) ? fdObj->ref.num : 0;
        if (key) {
            std::map<int, Font>::iterator it = cache.find(key);
            if (it != cache.end()) return &it->second;
        }
        Font f;
        Build(*fd, f);
        if (key) { cache[key] = f; return &cache[key]; }
        direct.push_back(f);
        return &direct.back();
    }

    void Build(const Obj& fd, Font& f)
    {
        const Obj* sub = doc->Get(&fd, "Subtype");
        f.twoByte = (sub && sub->t == O_NAME && sub->s == "Type0");
        const Obj* bn = doc->Get(&fd, "BaseFont");
        if (bn && bn->t == O_NAME) {
            f.base = bn->s;
            size_t plus = f.base.find('+');
            if (plus != std::string::npos && plus == 6) f.base = f.base.substr(7);
        }
        const Obj* desc = doc->Get(&fd, "FontDescriptor");
        const Obj* baseFontDict = &fd;
        if (f.twoByte) {
            const Obj* df = doc->Get(&fd, "DescendantFonts");
            if (df && df->t == O_ARR && !df->arr.empty()) {
                const Obj* d0 = doc->Resolve(&df->arr[0]);
                if (d0 && d0->IsDict()) {
                    baseFontDict = d0;
                    const Obj* dsub = doc->Get(d0, "Subtype");
                    if (dsub && dsub->t == O_NAME && dsub->s == "CIDFontType2") f.twoByte = true;
                    desc = doc->Get(d0, "FontDescriptor");
                    f.dw = (float)doc->NumOf(d0, "DW", 1000);
                    const Obj* w = doc->Get(d0, "W");
                    if (w && w->t == O_ARR) {
                        for (size_t i = 0; i < w->arr.size();) {
                            const Obj* a = doc->Resolve(&w->arr[i]);
                            if (!a || a->t != O_NUM) break;
                            int c0 = (int)a->num;
                            const Obj* b = (i + 1 < w->arr.size()) ? doc->Resolve(&w->arr[i + 1]) : nullptr;
                            if (b && b->t == O_ARR) {
                                for (size_t k = 0; k < b->arr.size(); ++k)
                                    f.wcid[c0 + (int)k] = (float)doc->Resolve(&b->arr[k])->Num(f.dw);
                                i += 2;
                            } else {
                                const Obj* c = (i + 2 < w->arr.size()) ? doc->Resolve(&w->arr[i + 2]) : nullptr;
                                if (!b || !c || b->t != O_NUM || c->t != O_NUM) break;
                                int c1 = (int)b->num;
                                float wv = (float)c->num;
                                if (c1 - c0 < 65536)
                                    for (int cc = c0; cc <= c1; ++cc) f.wcid[cc] = wv;
                                i += 3;
                            }
                        }
                    }
                    const Obj* c2g = doc->Get(d0, "CIDToGIDMap");
                    if (c2g && c2g->t == O_STREAM) {
                        std::vector<uint8_t> m = doc->StreamData(*c2g);
                        for (size_t k = 0; k + 1 < m.size(); k += 2)
                            f.cid2gid[(int)(k / 2)] = (m[k] << 8) | m[k + 1];
                    }
                }
            }
        } else {
            // simple font: widths + encoding
            int first = (int)doc->NumOf(&fd, "FirstChar", 0);
            const Obj* w = doc->Get(&fd, "Widths");
            if (w && w->t == O_ARR) {
                for (size_t i = 0; i < w->arr.size() && first + (int)i < 256; ++i) {
                    const Obj* wo = doc->Resolve(&w->arr[i]);
                    if (wo && wo->t == O_NUM) f.w1k[first + (int)i] = (float)wo->num;
                }
            }
            // encoding
            for (int c = 0x20; c < 0x7F; ++c) f.enc[c] = c;
            for (int c = 0x80; c < 0x100; ++c) f.enc[c] = kWinAnsiHigh[c - 0x80];
            const Obj* enc = doc->Get(&fd, "Encoding");
            if (enc) {
                if (enc->t == O_NAME) {
                    if (enc->s == "WinAnsiEncoding") { /* default */ }
                    else if (enc->s == "StandardEncoding" || enc->s == "MacRomanEncoding") {
                        for (int c = 0x80; c < 0x100; ++c) f.enc[c] = c;
                    }
                } else if (enc->IsDict()) {
                    const Obj* be = doc->Get(enc, "BaseEncoding");
                    if (be && be->t == O_NAME && (be->s == "StandardEncoding" || be->s == "MacRomanEncoding"))
                        for (int c = 0x80; c < 0x100; ++c) f.enc[c] = c;
                    const Obj* diff = doc->Get(enc, "Differences");
                    if (diff && diff->t == O_ARR) {
                        int code = 0;
                        for (size_t i = 0; i < diff->arr.size(); ++i) {
                            const Obj* e = doc->Resolve(&diff->arr[i]);
                            if (!e) continue;
                            if (e->t == O_NUM) code = (int)e->num;
                            else if (e->t == O_NAME) {
                                int u = GlyphNameToUni(e->s);
                                if (code >= 0 && code < 256) f.enc[code] = u;
                                ++code;
                            }
                        }
                    }
                }
            }
        }
        // descriptor: program + substitute face selection
        if (desc && desc->IsDict()) {
            f.missingW = (float)doc->NumOf(desc, "MissingWidth", 0);
            const Obj* ff2 = doc->Get(desc, "FontFile2");
            const Obj* ff3 = doc->Get(desc, "FontFile3");
            const Obj* ff1 = doc->Get(desc, "FontFile");
            const Obj* prog = ff2;
            if (!prog && ff3) {
                const Obj* st = doc->Get(ff3, "Subtype");
                if (st && st->t == O_NAME && st->s == "OpenType") prog = ff3;
            }
            if (prog && prog->t == O_STREAM) {
                f.prog = doc->StreamData(*prog);
                if (!f.prog.empty()) {
                    int off = stbtt_GetFontOffsetForIndex(f.prog.data(), 0);
                    if (stbtt_InitFont(&f.tt, f.prog.data(), off)) f.hasProg = true;
                }
            }
            (void)ff1;
        }
        // substitute family: mono / serif / sans, and weight/slant from the name
        std::string low;
        for (size_t i = 0; i < f.base.size(); ++i) low.push_back((char)tolower(f.base[i]));
        int fam = 3;
        if (low.find("courier") != std::string::npos || low.find("mono") != std::string::npos ||
            low.find("consol") != std::string::npos) fam = 5;
        else if (low.find("times") != std::string::npos || low.find("serif") != std::string::npos ||
                 low.find("roman") != std::string::npos || low.find("georgia") != std::string::npos ||
                 low.find("cambria") != std::string::npos || low.find("garamond") != std::string::npos)
            fam = 4;
        bool bold = low.find("bold") != std::string::npos || low.find("black") != std::string::npos ||
                    low.find("heavy") != std::string::npos;
        bool ital = low.find("italic") != std::string::npos || low.find("oblique") != std::string::npos;
        if (desc && desc->IsDict()) {
            float ang = (float)doc->NumOf(desc, "ItalicAngle", 0);
            float wt = (float)doc->NumOf(desc, "FontWeight", 0);
            if (fabsf(ang) > 0.5f) ital = true;
            if (wt >= 600.f) bold = true;
        }
        // Base-14 fonts carry no /Widths: take the advances from the AFM table the
        // PDF writer uses, or every glyph after the first lands at 500/1000 em.
        f.subFam = fam;
        if (!f.twoByte && !f.hasProg) {
            int afmFam = (fam == 5) ? 2 : (fam == 4) ? 1 : 0;
            int afmStyle = (bold ? 1 : 0) + (ital ? 2 : 0);
            if (afmStyle > 3) afmStyle = 0;
            for (int c = 32; c <= 126; ++c)
                if (f.w1k[c] <= 0.f) f.w1k[c] = (float)kPdfCharW[afmFam][afmStyle][c - 32];
        }
        f.subFace = (bold ? 1 : 0) + (ital ? 2 : 0);
        // ToUnicode
        const Obj* tu = doc->Get(&fd, "ToUnicode");
        if (tu && tu->t == O_STREAM) ParseToUnicode(doc->StreamData(*tu), f.tou);
        (void)baseFontDict;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Content stream interpretation
// ─────────────────────────────────────────────────────────────────────────────
struct GState {
    Mat ctm;
    float lw = 1.f;
    int   cap = 0, join = 0;
    std::vector<float> dash;
    float dashPhase = 0;
    float col[3] = { 0, 0, 0 };           // current fill colour
    int   ncomp = 1;                       // 1/3/4 component space for fill
    int   strokeNcomp = 1;
    float scol[3] = { 0, 0, 0 };
    // text
    Font* font = nullptr;
    float fs = 12.f, tc = 0, tw = 0, th = 1.f, tl = 0, ts = 0;
    int   tr = 0;
    Mat   tm, tlm;
    const Obj* res = nullptr;
    float clip[4] = { 0, 0, 1e9f, 1e9f };
    bool  hasClip = false;
};

struct Renderer {
    Doc* doc;
    Canvas* cv;
    FontCache fonts;
    std::string text;              // extracted text (verification hook)
    float lastLineY = -1e9f;
    int   depth = 0;

    Renderer(Doc* d, Canvas* c) : doc(d), cv(c) { fonts.doc = d; }

    void Page(const Page& pg, const Mat& base)
    {
        Mat m = Mat();
        m.e = -pg.mb[0];
        m.f = -pg.mb[1];
        float w = pg.mb[2] - pg.mb[0], h = pg.mb[3] - pg.mb[1];
        if (pg.rot == 90) {
            Mat r; r.a = 0; r.b = 1; r.c = -1; r.d = 0; r.e = h; r.f = 0;
            m = Concat(m, r);
        } else if (pg.rot == 180) {
            Mat r; r.a = -1; r.d = -1; r.e = w; r.f = h;
            m = Concat(m, r);
        } else if (pg.rot == 270) {
            Mat r; r.a = 0; r.b = -1; r.c = 1; r.d = 0; r.e = 0; r.f = w;
            m = Concat(m, r);
        }
        Mat full = Concat(m, base);

        GState gs;
        gs.ctm = full;
        gs.res = &pg.resources;

        // page content streams (array or single)
        const Obj* cont = doc->Resolve(pg.dict.Get("Contents"));
        if (!cont) return;
        if (cont->t == O_ARR) {
            for (size_t i = 0; i < cont->arr.size(); ++i) {
                const Obj* s = doc->Resolve(&cont->arr[i]);
                if (s && s->t == O_STREAM) {
                    std::vector<uint8_t> data = doc->StreamData(*s);
                    Run(data.data(), data.size(), gs);
                }
            }
        } else if (cont->t == O_STREAM) {
            std::vector<uint8_t> data = doc->StreamData(*cont);
            Run(data.data(), data.size(), gs);
        }
    }

    // ── path ────────────────────────────────────────────────────────────────
    struct Cmd { int op; float p[6]; };     // 0=line,1=cubic
    struct Sub { std::vector<Cmd> cmds; bool closed = false; };
    std::vector<Sub> path;
    Pt cur{ 0, 0 }, start{ 0, 0 };
    bool pendingClip = false;
    bool pendingClipEvenOdd = false;

    void PathReset() { path.clear(); pendingClip = false; pendingClipEvenOdd = false; }

    std::vector<Canvas::Poly> FlattenPath(const Mat& m)
    {
        std::vector<Canvas::Poly> out;
        for (size_t si = 0; si < path.size(); ++si) {
            const Sub& s = path[si];
            if (s.cmds.empty()) continue;
            Canvas::Poly poly;
            poly.closed = s.closed;
            Cmd first = s.cmds[0];
            float x, y;
            Apply(m, first.p[0], first.p[1], x, y);
            poly.pts.push_back({ x, y });
            for (size_t i = 1; i < s.cmds.size(); ++i) {
                // current point is last emitted point in device space
                Pt lastPt = poly.pts.back();
                const Cmd& c = s.cmds[i];
                if (c.op == 0) {
                    float px, py;
                    Apply(m, c.p[0], c.p[1], px, py);
                    if (fabsf(px - lastPt.x) + fabsf(py - lastPt.y) > 0.02f) poly.pts.push_back({ px, py });
                } else {
                    float x0 = lastPt.x, y0 = lastPt.y;
                    Mat inv;
                    float ux = 0, uy = 0;
                    if (Invert(m, inv)) Apply(inv, x0, y0, ux, uy);
                    // control points in device space
                    float c1x, c1y, c2x, c2y, ex, ey;
                    Apply(m, c.p[0], c.p[1], c1x, c1y);
                    Apply(m, c.p[2], c.p[3], c2x, c2y);
                    Apply(m, c.p[4], c.p[5], ex, ey);
                    float len = fabsf(c1x - x0) + fabsf(c1y - y0) + fabsf(c2x - c1x) + fabsf(c2y - c1y)
                              + fabsf(ex - c2x) + fabsf(ey - c2y);
                    int steps = (int)(len * 0.4f);
                    if (steps < 3) steps = 3;
                    if (steps > 64) steps = 64;
                    for (int k = 1; k <= steps; ++k) {
                        float t = (float)k / steps, u = 1 - t;
                        float px = u * u * u * x0 + 3 * u * u * t * c1x + 3 * u * t * t * c2x + t * t * t * ex;
                        float py = u * u * u * y0 + 3 * u * u * t * c1y + 3 * u * t * t * c2y + t * t * t * ey;
                        poly.pts.push_back({ px, py });
                    }
                    (void)ux; (void)uy;
                }
            }
            if (poly.pts.size() > 1) out.push_back(poly);
        }
        return out;
    }

    void FillPath(GState& gs, bool evenOdd)
    {
        std::vector<Canvas::Poly> polys = FlattenPath(gs.ctm);
        if (polys.empty()) return;
        if (pendingClip) {
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
            for (size_t i = 0; i < polys.size(); ++i)
                for (size_t k = 0; k < polys[i].pts.size(); ++k) {
                    x0 = std::min(x0, polys[i].pts[k].x); x1 = std::max(x1, polys[i].pts[k].x);
                    y0 = std::min(y0, polys[i].pts[k].y); y1 = std::max(y1, polys[i].pts[k].y);
                }
            cv->SetClip(x0, y0, x1 + 1.f, y1 + 1.f);
            pendingClip = false;
            return;
        }
        cv->Fill(polys, evenOdd, gs.col);
    }

    // Split a flattened polyline into the "on" runs of a dash pattern.
    void DashSplit(const Canvas::Poly& p, const std::vector<float>& dash, float phase,
                   float scl, std::vector<std::vector<Pt> >& out)
    {
        std::vector<float> d;
        float total = 0;
        for (size_t i = 0; i < dash.size(); ++i) {
            float v = fabsf(dash[i]) * scl;
            d.push_back(v);
            total += v;
        }
        if (d.empty() || total <= 0.01f) {
            out.push_back(p.pts);
            return;
        }
        if (d.size() % 2) { d.push_back(d.back()); }
        size_t idx = 0;
        float left = d[0];
        phase = fmodf(phase * scl, total);
        if (phase < 0) phase += total;
        while (phase > 0) {                       // advance into the pattern
            if (phase >= left) { phase -= left; idx = (idx + 1) % d.size(); left = d[idx]; }
            else { left -= phase; phase = 0; }
        }
        bool on = (idx % 2) == 0;
        std::vector<Pt> cur;
        if (on) cur.push_back(p.pts[0]);
        std::vector<Pt> pts = p.pts;
        if (p.closed && pts.size() > 1) pts.push_back(pts[0]);
        for (size_t i = 0; i + 1 < pts.size(); ++i) {
            Pt a = pts[i], b = pts[i + 1];
            float dx = b.x - a.x, dy = b.y - a.y;
            float len = sqrtf(dx * dx + dy * dy);
            if (len < 1e-6f) continue;
            float ux = dx / len, uy = dy / len;
            float pos = 0;
            while (pos < len) {
                float step = left;
                if (pos + step > len) { step = len - pos; }
                Pt n = { a.x + ux * (pos + step), a.y + uy * (pos + step) };
                if (on) cur.push_back(n);
                pos += step;
                left -= step;
                if (left <= 1e-5f) {
                    if (on && cur.size() > 1) out.push_back(cur);
                    cur.clear();
                    idx = (idx + 1) % d.size();
                    left = d[idx];
                    on = (idx % 2) == 0;
                    if (on) cur.push_back(n);
                }
            }
        }
        if (on && cur.size() > 1) out.push_back(cur);
    }

    void StrokePath(GState& gs)
    {
        std::vector<Canvas::Poly> polys = FlattenPath(gs.ctm);
        if (polys.empty()) return;
        float det = fabsf(gs.ctm.a * gs.ctm.d - gs.ctm.b * gs.ctm.c);
        float scl = sqrtf(det);
        float hw = std::max(0.35f, gs.lw * scl * 0.5f);
        std::vector<Canvas::Poly> out;
        for (size_t i = 0; i < polys.size(); ++i) {
            if (gs.dash.empty()) { StrokePoly(polys[i], hw, gs.cap, out); continue; }
            std::vector<std::vector<Pt> > runs;
            DashSplit(polys[i], gs.dash, gs.dashPhase, scl, runs);
            for (size_t k = 0; k < runs.size(); ++k) {
                Canvas::Poly open;
                open.closed = false;
                open.pts = runs[k];
                StrokePoly(open, hw, gs.cap, out);
            }
        }
        if (pendingClip) {
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
            for (size_t i = 0; i < polys.size(); ++i)
                for (size_t k = 0; k < polys[i].pts.size(); ++k) {
                    x0 = std::min(x0, polys[i].pts[k].x); x1 = std::max(x1, polys[i].pts[k].x);
                    y0 = std::min(y0, polys[i].pts[k].y); y1 = std::max(y1, polys[i].pts[k].y);
                }
            cv->SetClip(x0 - hw, y0 - hw, x1 + hw, y1 + hw);
            pendingClip = false;
            return;
        }
        cv->Fill(out, false, gs.scol, 1.f, true);
    }

    void StrokePoly(const Canvas::Poly& p, float hw, int cap, std::vector<Canvas::Poly>& out)
    {
        size_t n = p.pts.size();
        if (n < 2) return;
        for (size_t i = 0; i + 1 < n; ++i) {
            Pt a = p.pts[i], b = p.pts[i + 1];
            float dx = b.x - a.x, dy = b.y - a.y;
            float len = sqrtf(dx * dx + dy * dy);
            if (len < 1e-4f) continue;
            float nx = -dy / len * hw, ny = dx / len * hw;
            Canvas::Poly q;
            q.closed = true;
            q.pts.push_back({ a.x + nx, a.y + ny });
            q.pts.push_back({ b.x + nx, b.y + ny });
            q.pts.push_back({ b.x - nx, b.y - ny });
            q.pts.push_back({ a.x - nx, a.y - ny });
            out.push_back(q);
        }
        // round joins; round caps only when the line says so (butt caps keep
        // dash gaps open, which is what dotted lines rely on)
        for (size_t i = 0; i < n; ++i) {
            bool joint = (i > 0 && i + 1 < n);
            if (!joint && cap != 1) continue;
            Canvas::Poly c;
            c.closed = true;
            int seg = 10;
            for (int k = 0; k < seg; ++k) {
                float a = (float)k / seg * 6.2831853f;
                c.pts.push_back({ p.pts[i].x + cosf(a) * hw, p.pts[i].y + sinf(a) * hw });
            }
            out.push_back(c);
        }
    }

    // ── text ────────────────────────────────────────────────────────────────
    void ShowText(GState& gs, const std::string& bytes)
    {
        if (!gs.font) return;
        Font* f = gs.font;
        size_t i = 0;
        while (i < bytes.size()) {
            int code = 0;
            if (f->twoByte) {
                if (i + 1 >= bytes.size()) break;
                code = ((unsigned char)bytes[i] << 8) | (unsigned char)bytes[i + 1];
                i += 2;
            } else {
                code = (unsigned char)bytes[i];
                i += 1;
            }
            float w0 = f->WidthOf(code) / 1000.f;
            float tx = (w0 * gs.fs + gs.tc + ((!f->twoByte && code == 32) ? gs.tw : 0.f)) * gs.th;
            float ty = 0.f;
            if (gs.tr != 3 && gs.tr != 7) {
                Mat tp;
                tp.a = gs.fs * gs.th; tp.d = gs.fs; tp.f = gs.ts;
                Mat trm = Concat(tp, Concat(gs.tm, gs.ctm));
                std::vector<Canvas::Poly> polys;
                f->OutlineAdvance(code, trm, polys);
                if (!polys.empty()) {
                    if (gs.tr == 1) cv->Fill(polys, false, gs.scol, 1.f, false);
                    else if (gs.tr == 2) { cv->Fill(polys, false, gs.col, 1.f, false); }
                    else cv->Fill(polys, false, gs.col, 1.f, false);
                }
            }
            // text extraction hook
            text += f->TextOf(code);
            // advance
            Mat adv; adv.e = tx; adv.f = ty;
            gs.tm = Concat(adv, gs.tm);
        }
    }

    // ── operators ───────────────────────────────────────────────────────────
    void Run(const uint8_t* data, size_t len, GState& gsStarting)
    {
        if (depth > 8) return;
        ++depth;
        GState gs = gsStarting;
        std::vector<GState> stack;
        std::vector<Obj> ops;
        Lexer lx(data, len, 0);
        PathReset();
        for (;;) {
            if (lx.Eof()) break;
            Obj o = lx.Parse();
            if (o.t == O_KEYWORD) {
                if (o.s == "BI") InlineImage(lx, gs);
                else Op(o.s, ops, gs, stack);
            } else ops.push_back(o);
        }
        --depth;
        gsStarting.tm = gs.tm;
    }

    float Num(const std::vector<Obj>& ops, int idxFromEnd, float def = 0.f)
    {
        int i = (int)ops.size() - idxFromEnd;
        if (i < 0 || i >= (int)ops.size()) return def;
        const Obj& o = ops[i];
        return o.t == O_NUM ? (float)o.num : def;
    }

    void GetColor(const std::vector<Obj>& ops, int n, float c[3])
    {
        float v[4] = { 0, 0, 0, 0 };
        for (int i = 0; i < n; ++i) v[i] = Num(ops, n - i, 0.f);
        if (n == 1) { c[0] = c[1] = c[2] = v[0]; }
        else if (n == 3) { c[0] = v[0]; c[1] = v[1]; c[2] = v[2]; }
        else if (n == 4) {
            float k = v[3];
            c[0] = (1 - v[0]) * (1 - k);
            c[1] = (1 - v[1]) * (1 - k);
            c[2] = (1 - v[2]) * (1 - k);
        } else if (n > 0) { c[0] = v[0]; c[1] = v[0]; c[2] = v[0]; }
        for (int i = 0; i < 3; ++i) { c[i] = c[i] < 0 ? 0 : (c[i] > 1 ? 1 : c[i]); }
    }

    void Op(const std::string& op, std::vector<Obj>& ops, GState& gs, std::vector<GState>& stack)
    {
        if (op == "q") { stack.push_back(gs); ops.clear(); return; }
        if (op == "Q") {
            if (!stack.empty()) { gs = stack.back(); stack.pop_back(); }
            cv->ResetClip();
            if (gs.hasClip) cv->SetClip(gs.clip[0], gs.clip[1], gs.clip[2], gs.clip[3]);
            ops.clear(); return;
        }
        if (op == "cm") {
            Mat m;
            m.a = Num(ops, 6); m.b = Num(ops, 5); m.c = Num(ops, 4);
            m.d = Num(ops, 3); m.e = Num(ops, 2); m.f = Num(ops, 1);
            gs.ctm = Concat(m, gs.ctm);
            ops.clear(); return;
        }
        if (op == "w") { gs.lw = Num(ops, 1, 1.f); ops.clear(); return; }
        if (op == "J") { gs.cap = (int)Num(ops, 1); ops.clear(); return; }
        if (op == "j") { gs.join = (int)Num(ops, 1); ops.clear(); return; }
        if (op == "M") { ops.clear(); return; }
        if (op == "d") {
            gs.dash.clear();
            // "[a b] phase d" — the array is the second-to-last operand
            if (ops.size() >= 2 && ops[ops.size() - 2].t == O_ARR) {
                const Obj& a = ops[ops.size() - 2];
                for (size_t i = 0; i < a.arr.size(); ++i) gs.dash.push_back((float)a.arr[i].Num(0));
            } else if (!ops.empty() && ops.back().t == O_ARR) {
                const Obj& a = ops.back();
                for (size_t i = 0; i < a.arr.size(); ++i) gs.dash.push_back((float)a.arr[i].Num(0));
            }
            if (!ops.empty()) gs.dashPhase = Num(ops, 1);
            ops.clear(); return;
        }
        // ── colours ──
        if (op == "g" || op == "G") {
            float c[3]; GetColor(ops, 1, c);
            if (op == "g") { gs.col[0] = gs.col[1] = gs.col[2] = Num(ops, 1); gs.ncomp = 1; }
            else { gs.scol[0] = gs.scol[1] = gs.scol[2] = Num(ops, 1); gs.strokeNcomp = 1; }
            ops.clear(); return;
        }
        if (op == "rg" || op == "RG") {
            float c[3]; GetColor(ops, 3, c);
            if (op == "rg") { gs.col[0] = c[0]; gs.col[1] = c[1]; gs.col[2] = c[2]; gs.ncomp = 3; }
            else { gs.scol[0] = c[0]; gs.scol[1] = c[1]; gs.scol[2] = c[2]; gs.strokeNcomp = 3; }
            ops.clear(); return;
        }
        if (op == "k" || op == "K") {
            float c[3]; GetColor(ops, 4, c);
            if (op == "k") { gs.col[0] = c[0]; gs.col[1] = c[1]; gs.col[2] = c[2]; gs.ncomp = 4; }
            else { gs.scol[0] = c[0]; gs.scol[1] = c[1]; gs.scol[2] = c[2]; gs.strokeNcomp = 4; }
            ops.clear(); return;
        }
        if (op == "cs" || op == "CS") {
            int n = 1;
            if (!ops.empty() && ops.back().t == O_NAME) {
                const std::string& nm = ops.back().s;
                if (nm == "DeviceRGB" || nm == "CalRGB") n = 3;
                else if (nm == "DeviceCMYK") n = 4;
                else if (nm != "DeviceGray" && nm != "G") {
                    // named colour space from resources
                    const Obj* cso = nullptr;
                    if (gs.res) {
                        const Obj* csd = doc->Get(gs.res, "ColorSpace");
                        if (csd) cso = doc->Get(csd, nm.c_str());
                    }
                    if (cso) {
                        const Obj* r = doc->Resolve(cso);
                        if (r && r->t == O_ARR && !r->arr.empty()) {
                            const Obj* fam = doc->Resolve(&r->arr[0]);
                            if (fam && fam->t == O_NAME) {
                                if (fam->s == "DeviceRGB" || fam->s == "CalRGB") n = 3;
                                else if (fam->s == "DeviceCMYK") n = 4;
                                else if (fam->s == "ICCBased" && r->arr.size() > 1) {
                                    const Obj* st = doc->Resolve(&r->arr[1]);
                                    if (st) n = (int)doc->NumOf(st, "N", 3);
                                }
                            }
                        }
                    }
                }
            }
            if (op == "cs") gs.ncomp = n; else gs.strokeNcomp = n;
            ops.clear(); return;
        }
        if (op == "sc" || op == "scn" || op == "SC" || op == "SCN") {
            int n = (op == "sc" || op == "scn") ? gs.ncomp : gs.strokeNcomp;
            // last operand may be a pattern name: count only numbers
            int numCount = 0;
            for (size_t i = 0; i < ops.size(); ++i) if (ops[i].t == O_NUM) ++numCount;
            if (numCount > 0) {
                float c[3];
                GetColor(ops, std::min(numCount, 4), c);
                if (op == "sc" || op == "scn") { for (int i = 0; i < 3; ++i) gs.col[i] = c[i]; }
                else { for (int i = 0; i < 3; ++i) gs.scol[i] = c[i]; }
            } else if (n == 1) { gs.col[0] = gs.col[1] = gs.col[2] = 0; }
            ops.clear(); return;
        }
        // ── path construction ──
        if (op == "m" || op == "l") {
            float x = Num(ops, 2), y = Num(ops, 1);
            if (op == "m" || path.empty()) {
                Sub s;
                Cmd c; c.op = 0; c.p[0] = x; c.p[1] = y;
                s.cmds.push_back(c);
                path.push_back(s);
            } else {
                Cmd c; c.op = 0; c.p[0] = x; c.p[1] = y;
                path.back().cmds.push_back(c);
            }
            cur = { x, y };
            ops.clear(); return;
        }
        if (op == "c" || op == "v" || op == "y") {
            Cmd c; c.op = 1;
            if (op == "c") {
                c.p[0] = Num(ops, 6); c.p[1] = Num(ops, 5);
                c.p[2] = Num(ops, 4); c.p[3] = Num(ops, 3);
                c.p[4] = Num(ops, 2); c.p[5] = Num(ops, 1);
            } else if (op == "v") {
                c.p[0] = cur.x; c.p[1] = cur.y;
                c.p[2] = Num(ops, 4); c.p[3] = Num(ops, 3);
                c.p[4] = Num(ops, 2); c.p[5] = Num(ops, 1);
            } else {
                c.p[0] = Num(ops, 4); c.p[1] = Num(ops, 3);
                c.p[2] = Num(ops, 2); c.p[3] = Num(ops, 1);
                c.p[4] = Num(ops, 2); c.p[5] = Num(ops, 1);
            }
            if (path.empty()) { Sub s; path.push_back(s); Cmd m0; m0.op = 0; m0.p[0] = cur.x; m0.p[1] = cur.y; path.back().cmds.push_back(m0); }
            path.back().cmds.push_back(c);
            cur = { c.p[4], c.p[5] };
            ops.clear(); return;
        }
        if (op == "re") {
            float x = Num(ops, 4), y = Num(ops, 3), w = Num(ops, 2), h = Num(ops, 1);
            Sub s; s.closed = true;
            float pts[5][2] = { { x, y }, { x + w, y }, { x + w, y + h }, { x, y + h }, { x, y } };
            for (int i = 0; i < 5; ++i) { Cmd c; c.op = 0; c.p[0] = pts[i][0]; c.p[1] = pts[i][1]; s.cmds.push_back(c); }
            path.push_back(s);
            cur = { x, y };
            ops.clear(); return;
        }
        if (op == "h") { if (!path.empty()) path.back().closed = true; cur = start; ops.clear(); return; }
        if (op == "W" || op == "W*") { pendingClip = true; pendingClipEvenOdd = (op == "W*"); return; }
        if (op == "n") {
            if (pendingClip) {
                // clip-only path: bounding box of the path
                Mat save = gs.ctm;
                std::vector<Canvas::Poly> polys = FlattenPath(save);
                if (!polys.empty()) {
                    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
                    for (size_t i = 0; i < polys.size(); ++i)
                        for (size_t k = 0; k < polys[i].pts.size(); ++k) {
                            x0 = std::min(x0, polys[i].pts[k].x); x1 = std::max(x1, polys[i].pts[k].x);
                            y0 = std::min(y0, polys[i].pts[k].y); y1 = std::max(y1, polys[i].pts[k].y);
                        }
                    cv->SetClip(x0 - 1, y0 - 1, x1 + 1, y1 + 1);
                }
                pendingClip = false;
            }
            PathReset(); ops.clear(); return;
        }
        if (op == "f" || op == "F" || op == "f*") {
            FillPath(gs, op == "f*");
            PathReset(); ops.clear(); return;
        }
        if (op == "S" || op == "s") {
            if (op == "s" && !path.empty()) path.back().closed = true;
            StrokePath(gs);
            PathReset(); ops.clear(); return;
        }
        if (op == "B" || op == "B*" || op == "b" || op == "b*") {
            FillPath(gs, op[1] == '*');
            if (!path.empty()) { if (op == "b" || op == "b*") path.back().closed = true; }
            StrokePath(gs);
            PathReset(); ops.clear(); return;
        }
        // ── text state ──
        if (op == "BT") { gs.tm = Mat(); gs.tlm = Mat(); ops.clear(); return; }
        if (op == "ET") { ops.clear(); return; }
        if (op == "Tf") {
            if (ops.size() >= 2) {
                gs.fs = Num(ops, 1, 12.f);
                if (ops[ops.size() - 2].t == O_NAME && gs.res) {
                    const Obj* fontsDict = doc->Get(gs.res, "Font");
                    if (fontsDict) {
                        const Obj* fRef = fontsDict->Get(ops[ops.size() - 2].s.c_str());
                        if (fRef) gs.font = fonts.Get(fRef);
                    }
                }
            }
            ops.clear(); return;
        }
        if (op == "Tm") {
            Mat m;
            m.a = Num(ops, 6); m.b = Num(ops, 5); m.c = Num(ops, 4);
            m.d = Num(ops, 3); m.e = Num(ops, 2); m.f = Num(ops, 1);
            gs.tm = m; gs.tlm = m;
            ops.clear(); return;
        }
        if (op == "Td" || op == "TD") {
            float tx = Num(ops, 2), ty = Num(ops, 1);
            if (op == "TD") gs.tl = -ty;
            Mat m; m.e = tx; m.f = ty;
            gs.tlm = Concat(m, gs.tlm);
            gs.tm = gs.tlm;
            ops.clear(); return;
        }
        if (op == "T*") {
            Mat m; m.f = -gs.tl;
            gs.tlm = Concat(m, gs.tlm);
            gs.tm = gs.tlm;
            ops.clear(); return;
        }
        if (op == "TL") { gs.tl = Num(ops, 1); ops.clear(); return; }
        if (op == "Tc") { gs.tc = Num(ops, 1); ops.clear(); return; }
        if (op == "Tw") { gs.tw = Num(ops, 1); ops.clear(); return; }
        if (op == "Tz") { gs.th = Num(ops, 1, 100.f) / 100.f; ops.clear(); return; }
        if (op == "Ts") { gs.ts = Num(ops, 1); ops.clear(); return; }
        if (op == "Tr") { gs.tr = (int)Num(ops, 1); ops.clear(); return; }
        if (op == "Tj") {
            if (!ops.empty() && ops.back().t == O_STR) ShowText(gs, ops.back().s);
            ops.clear(); return;
        }
        if (op == "'" || op == "\"") {
            if (op == "\"") { gs.tw = Num(ops, 3); gs.tc = Num(ops, 2); }
            Mat m; m.f = -gs.tl;
            gs.tlm = Concat(m, gs.tlm);
            gs.tm = gs.tlm;
            if (!ops.empty() && ops.back().t == O_STR) ShowText(gs, ops.back().s);
            ops.clear(); return;
        }
        if (op == "TJ") {
            if (!ops.empty() && ops.back().t == O_ARR) {
                const Obj& a = ops.back();
                for (size_t i = 0; i < a.arr.size(); ++i) {
                    if (a.arr[i].t == O_STR) ShowText(gs, a.arr[i].s);
                    else if (a.arr[i].t == O_NUM) {
                        float adj = (float)a.arr[i].num / 1000.f * gs.fs * gs.th;
                        Mat adv; adv.e = -adj;
                        gs.tm = Concat(adv, gs.tm);
                    }
                }
            }
            ops.clear(); return;
        }
        // ── XObjects / images ──
        if (op == "Do") {
            if (!ops.empty() && ops.back().t == O_NAME && gs.res) {
                const Obj* xd = doc->Get(gs.res, "XObject");
                if (xd) {
                    const Obj* xr = xd->Get(ops.back().s.c_str());
                    const Obj* xo = doc->Resolve(xr);
                    if (xo && xo->t == O_STREAM) {
                        const Obj* st = doc->Get(xo, "Subtype");
                        if (st && st->t == O_NAME && st->s == "Form") DoForm(*xo, gs);
                        else if (st && st->t == O_NAME && st->s == "Image") DoImage(*xo, gs);
                    }
                }
            }
            ops.clear(); return;
        }
        if (op == "sh" || op == "gs" || op == "BDC" || op == "BMC" || op == "EMC" ||
            op == "ri" || op == "i" || op == "MP" || op == "DP" || op == "BX" || op == "EX") {
            ops.clear(); return;
        }
        ops.clear();
    }

    void DoForm(const Obj& xo, GState& gs)
    {
        if (depth > 10) return;
        const Obj* mat = doc->Get(&xo, "Matrix");
        Mat m;
        if (mat && mat->t == O_ARR && mat->arr.size() >= 6) {
            m.a = (float)mat->arr[0].Num(1); m.b = (float)mat->arr[1].Num(0);
            m.c = (float)mat->arr[2].Num(0); m.d = (float)mat->arr[3].Num(1);
            m.e = (float)mat->arr[4].Num(0); m.f = (float)mat->arr[5].Num(0);
        }
        GState inner = gs;
        inner.ctm = Concat(m, gs.ctm);
        const Obj* res = doc->Get(&xo, "Resources");
        if (res) inner.res = res;
        std::vector<uint8_t> data = doc->StreamData(xo);
        Run(data.data(), data.size(), inner);
    }

    // ── images ──────────────────────────────────────────────────────────────
    struct ImgPars {
        int  w = 0, h = 0, bpc = 8;
        bool mask = false;
        int  nc = 3;                     // components per sample
        bool indexed = false;
        int  baseN = 3;
        std::vector<uint8_t> palette;
    };

    void BuildImgPars(const Obj& d, const GState& gs, ImgPars& ip)
    {
        ip.w = (int)doc->NumOf(&d, "Width", 0);
        ip.h = (int)doc->NumOf(&d, "Height", 0);
        ip.bpc = (int)doc->NumOf(&d, "BitsPerComponent", 8);
        const Obj* im = doc->Get(&d, "ImageMask");
        if (im && im->t == O_BOOL && im->bl) { ip.mask = true; ip.bpc = 1; }

        const Obj* cso = doc->Resolve(doc->Get(&d, "ColorSpace"));
        ip.nc = 3;
        if (cso) {
            if (cso->t == O_NAME) {
                if (cso->s == "DeviceGray" || cso->s == "G") ip.nc = 1;
                else if (cso->s == "DeviceCMYK" || cso->s == "CMYK") ip.nc = 4;
                else if (cso->s == "DeviceRGB" || cso->s == "RGB") ip.nc = 3;
                else if (gs.res) {
                    const Obj* csd = doc->Get(gs.res, "ColorSpace");
                    if (csd) {
                        const Obj* named = doc->Get(csd, cso->s.c_str());
                        if (named) cso = doc->Resolve(named);
                    }
                }
            }
            if (cso && cso->t == O_ARR && !cso->arr.empty()) {
                const Obj* fam = doc->Resolve(&cso->arr[0]);
                if (fam && fam->t == O_NAME) {
                    if (fam->s == "Indexed" && cso->arr.size() >= 4) {
                        ip.indexed = true;
                        const Obj* base = doc->Resolve(&cso->arr[1]);
                        ip.baseN = 1;
                        if (base && base->t == O_NAME) {
                            if (base->s == "DeviceRGB" || base->s == "CalRGB") ip.baseN = 3;
                            else if (base->s == "DeviceCMYK") ip.baseN = 4;
                        }
                        const Obj* lut = doc->Resolve(&cso->arr[3]);
                        if (lut && lut->t == O_STREAM) ip.palette = doc->StreamData(*lut);
                        else if (lut && lut->t == O_STR) ip.palette.assign(lut->s.begin(), lut->s.end());
                        ip.nc = 1;
                    } else if (fam->s == "ICCBased" && cso->arr.size() >= 2) {
                        const Obj* st = doc->Resolve(&cso->arr[1]);
                        if (st) ip.nc = (int)doc->NumOf(st, "N", 3);
                    } else if (fam->s == "DeviceRGB" || fam->s == "CalRGB") ip.nc = 3;
                    else if (fam->s == "DeviceGray" || fam->s == "CalGray") ip.nc = 1;
                    else if (fam->s == "DeviceCMYK") ip.nc = 4;
                }
            }
        }
        if (ip.mask) ip.nc = 1;
    }

    void DrawImageRaw(const ImgPars& ip, const std::vector<uint8_t>& raw, GState& gs)
    {
        if (ip.w <= 0 || ip.h <= 0 || raw.empty()) return;
        int iw = ip.w, ih = ip.h, bpc = ip.bpc;
        int samples = ip.mask ? 1 : ip.nc;
        if (samples <= 0) return;
        std::vector<uint8_t> img((size_t)iw * ih * samples, 0);
        for (int y = 0; y < ih; ++y) {
            for (int x = 0; x < iw; ++x) {
                for (int c = 0; c < samples; ++c) {
                    int idx = (y * iw + x) * samples + c;
                    uint8_t v = 0;
                    if (bpc == 8) {
                        size_t o = (size_t)idx;
                        if (o < raw.size()) v = raw[o];
                    } else if (bpc == 1) {
                        size_t bit = (size_t)(y * iw + x) * samples + c;
                        size_t o = bit >> 3;
                        if (o < raw.size()) v = ((raw[o] >> (7 - (bit & 7))) & 1) ? 255 : 0;
                    } else if (bpc == 4) {
                        size_t bit = (size_t)(y * iw + x) * samples + c;
                        size_t o = bit >> 1;
                        if (o < raw.size()) {
                            bool hi = (bit & 1) == 0;
                            v = hi ? (uint8_t)((raw[o] >> 4) * 17) : (uint8_t)((raw[o] & 0xF) * 17);
                        }
                    } else if (bpc == 2) {
                        size_t bit = (size_t)(y * iw + x) * samples + c;
                        size_t o = bit >> 2;
                        if (o < raw.size()) v = (uint8_t)(((raw[o] >> (6 - 2 * (bit & 3))) & 3) * 85);
                    } else if (bpc == 16) {
                        size_t o = ((size_t)(y * iw + x) * samples + c) * 2;
                        if (o < raw.size()) v = raw[o];
                    }
                    img[idx] = v;
                }
            }
        }
        if (ip.indexed && !ip.palette.empty()) {
            std::vector<uint8_t> conv((size_t)iw * ih * ip.baseN, 255);
            for (int i = 0; i < iw * ih; ++i) {
                int e = img[i];
                for (int c = 0; c < ip.baseN; ++c) {
                    size_t o = (size_t)e * ip.baseN + c;
                    conv[(size_t)i * ip.baseN + c] = o < ip.palette.size() ? ip.palette[o] : 0;
                }
            }
            img.swap(conv);
            samples = ip.baseN;
        }
        Mat dev = gs.ctm;                     // unit square → user space → device
        cv->DrawImage(img, iw, ih, samples, dev, gs.col, ip.mask);
        ++images_drawn;
    }

    void DoImage(const Obj& xo, GState& gs)
    {
        ImgPars ip;
        BuildImgPars(xo, gs, ip);
        if (ip.w <= 0 || ip.h <= 0) return;
        std::vector<uint8_t> raw;
        int w = ip.w, h = ip.h, nc = ip.nc;
        if (!doc->DecodeImage(xo, raw, w, h, nc)) return;
        ip.w = w;
        ip.h = h;
        if (!ip.mask && nc > 0) ip.nc = nc;
        DrawImageRaw(ip, raw, gs);
    }
    int images_drawn = 0;

    // Inline image: BI /W .. /ID <binary> EI. Handled here because the payload
    // is raw bytes that the operator lexer must not try to tokenise.
    void InlineImage(Lexer& lx, GState& gs)
    {
        Obj d;
        d.t = O_DICT;
        for (;;) {
            if (lx.Eof()) return;
            Obj k = lx.Parse();
            if (k.t == O_KEYWORD) {
                if (k.s == "ID") break;
                if (k.s == "EI") return;
                continue;
            }
            if (k.t != O_NAME) continue;
            Obj v = lx.Parse();
            std::string key = k.s;
            if (key == "F" || key == "Filter") key = "Filter";     // /F abbreviation
            else if (key == "W") key = "Width";
            else if (key == "H") key = "Height";
            else if (key == "BPC") key = "BitsPerComponent";
            else if (key == "CS") key = "ColorSpace";
            else if (key == "IM") key = "ImageMask";
            else if (key == "D" || key == "DP") continue;
            d.d.push_back(std::make_pair(key, v));
        }
        if (lx.i < lx.n) ++lx.i;                                  // single WS after ID
        // payload length: computed when the image is unfiltered, else scan for EI
        ImgPars probe;
        BuildImgPars(d, gs, probe);
        size_t start = lx.i;
        size_t len = 0;
        bool hasFilter = d.Get("Filter") != nullptr;
        if (!hasFilter && probe.w > 0 && probe.h > 0) {
            int samples = probe.mask ? 1 : probe.nc;
            size_t bits = (size_t)probe.w * probe.h * samples * probe.bpc;
            len = (bits + 7) / 8;
            if (start + len > lx.n) len = lx.n - start;
        } else {
            size_t e = start;
            while (e + 2 <= lx.n) {
                if (lx.p[e] == 'E' && lx.p[e + 1] == 'I' &&
                    (e == 0 || IsWS(lx.p[e - 1])) &&
                    (e + 2 >= lx.n || IsWS(lx.p[e + 2]) || IsDelim(lx.p[e + 2]))) break;
                ++e;
            }
            len = e - start;
            if (len > 0 && (lx.p[start + len - 1] == '\n' || lx.p[start + len - 1] == '\r')) --len;
        }
        Obj payload;
        payload.t = O_STR;
        payload.s.assign((const char*)lx.p + start, len);
        payload.d = d.d;
        std::vector<uint8_t> raw = doc->StreamData(payload);
        DrawImageRaw(probe, raw, gs);
        lx.i = start + len;
        // consume "EI"
        std::string tail = lx.Word();
        if (tail == "EI") return;
        while (!lx.Eof()) {
            std::string w = lx.Word();
            if (w == "EI") break;
            if (w.empty()) { ++lx.i; }
        }
    }
};

static inline bool RenderPage(Doc* doc, int idx, float scale,
                              std::vector<uint8_t>& rgba, int& outW, int& outH, std::string* err)
{
    if (!doc || idx < 0 || idx >= doc->PageCount()) return false;
    const Page& pg = doc->pages[idx];
    float pw = pg.mb[2] - pg.mb[0], ph = pg.mb[3] - pg.mb[1];
    if (pg.rot == 90 || pg.rot == 270) std::swap(pw, ph);
    int W = (int)ceilf(pw * scale), H = (int)ceilf(ph * scale);
    if (W <= 0 || H <= 0) return false;
    long long px = (long long)W * H;
    if (px > 24LL * 1024 * 1024) { if (err) *err = "page too large at this zoom"; return false; }

    Canvas cv;
    cv.Init(W, H);
    Mat base;
    base.a = scale;
    base.d = -scale;
    base.e = 0;
    base.f = (float)H;
    Renderer r(doc, &cv);
    r.Page(pg, base);
    if (err) *err = r.text;
    rgba.swap(cv.px);
    outW = W;
    outH = H;
    return true;
}

} // namespace pdfr
