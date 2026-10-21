// jpegdec.hpp — JPEG decoder (baseline + progressive) for PDF /DCTDecode images.
//
// Everything a page raster needs: huffman decoding, dequantise, float IDCT,
// chroma upsampling, YCbCr → RGB, restart markers. Baseline scans are IDCT'd
// as they are decoded (streaming, low memory); progressive scans keep the
// coefficients and reconstruct at the end. Arithmetic coding, 12-bit precision
// and lossless JPEG are rejected — the caller then leaves the image out rather
// than drawing garbage.
#pragma once

#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>

#ifndef PDFR_JPEG_DC_ADD
#define PDFR_JPEG_DC_ADD 0
#endif
#ifndef PDFR_JPEG_EOB_SUB
#define PDFR_JPEG_EOB_SUB 1
#endif
#ifndef PDFR_JPEG_GUARD
#define PDFR_JPEG_GUARD 1
#endif

namespace pdfr {

struct JpegImage {
    int w = 0, h = 0, nc = 0;          // nc: 1 gray, 3 RGB, 4 CMYK
    std::vector<uint8_t> px;
};

struct JpegHuff {
    int  count[17];
    int  minCode[17];
    int  maxCode[18];
    int  valPtr[17];
    unsigned char vals[256];
    bool ok = false;
};

struct JpegComp {
    int id = 0, h = 1, v = 1, tq = 0, td = 0, ta = 0;
    int pw = 0, ph = 0, bw = 0, bh = 0;     // padded plane size, block counts
    int sw = 0, sh = 0;                     // block counts for single-component scans
    std::vector<uint8_t> plane;
    std::vector<int16_t> coef;              // progressive only
};

class JpegDecoder {
public:
    JpegImage Decode(const uint8_t* data, size_t len)
    {
        p = data;
        n = len;
        i = 0;
        if (n < 4 || p[0] != 0xFF || p[1] != 0xD8) return out;
        i = 2;
        while (i + 1 < n) {
            if (p[i] != 0xFF) { ++i; continue; }
            while (i < n && p[i] == 0xFF) ++i;
            if (i >= n) break;
            uint8_t m = p[i++];
            if (m == 0xD9) break;
            if (m == 0x01 || (m >= 0xD0 && m <= 0xD7)) continue;
            if (i + 1 >= n) break;
            int segLen = (p[i] << 8) | p[i + 1];
            if (segLen < 2) break;
            size_t segStart = i + 2;
            if (segStart + (size_t)segLen - 2 > n) break;
            size_t segEnd = segStart + segLen - 2;
            if (m == 0xC0 || m == 0xC1) Frame(segStart, segEnd, false);
            else if (m == 0xC2) Frame(segStart, segEnd, true);
            else if (m == 0xC3 || m == 0xC5 || m == 0xC6 || m == 0xC7 || m == 0xC9 || m == 0xCA ||
                     m == 0xCB || m == 0xCD || m == 0xCE || m == 0xCF) return out;   // unsupported
            else if (m == 0xC4) Dht(segStart, segEnd);
            else if (m == 0xDB) Dqt(segStart, segEnd);
            else if (m == 0xDD) { if (segEnd - segStart >= 2) restartInterval = (p[segStart] << 8) | p[segStart + 1]; }
            else if (m == 0xDA) {
                if (!Sos(segStart, segEnd)) return out;
                i = segEnd;
                if (progressive) { ProgScan(); continue; }   // i now sits at the next marker
                BaseScan();
                break;                                        // baseline: one scan is all
            }
            i = segEnd;
        }
        if (!pixels) Reconstruct();
        if (!pixels || !width || !height || !ncomp) return out;
        out.w = width;
        out.h = height;
        out.nc = (ncomp == 1) ? 1 : (ncomp == 4 ? 4 : 3);
        out.px.assign((size_t)width * height * out.nc, 0);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                uint8_t s[4] = { 0, 0, 0, 0 };
                for (int c = 0; c < ncomp; ++c) {
                    JpegComp& cc = comp[c];
                    float fx = (float)x * cc.h / (float)maxH;
                    float fy = (float)y * cc.v / (float)maxV;
                    int x0 = (int)fx, y0 = (int)fy;
                    if (x0 >= cc.pw) x0 = cc.pw - 1;
                    if (y0 >= cc.ph) y0 = cc.ph - 1;
                    int x1 = std::min(x0 + 1, cc.pw - 1), y1 = std::min(y0 + 1, cc.ph - 1);
                    float tx = fx - (float)x0, ty = fy - (float)y0;
                    if (cc.h == maxH && cc.v == maxV) {              // full resolution: no filtering
                        s[c] = cc.plane[(size_t)y0 * cc.pw + x0];
                    } else {                                          // linear chroma upsample
                        float a = cc.plane[(size_t)y0 * cc.pw + x0];
                        float b = cc.plane[(size_t)y0 * cc.pw + x1];
                        float d = cc.plane[(size_t)y1 * cc.pw + x0];
                        float e = cc.plane[(size_t)y1 * cc.pw + x1];
                        float v = (a * (1 - tx) + b * tx) * (1 - ty) + (d * (1 - tx) + e * tx) * ty;
                        s[c] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5f);
                    }
                }
                uint8_t* o = &out.px[((size_t)y * width + x) * out.nc];
                if (ncomp == 1) o[0] = s[0];
                else if (ncomp == 3) {
                    float Y = s[0], Cb = s[1] - 128.f, Cr = s[2] - 128.f;
                    int r = (int)(Y + 1.402f * Cr);
                    int g = (int)(Y - 0.344136f * Cb - 0.714136f * Cr);
                    int b = (int)(Y + 1.772f * Cb);
                    o[0] = (uint8_t)(r < 0 ? 0 : r > 255 ? 255 : r);
                    o[1] = (uint8_t)(g < 0 ? 0 : g > 255 ? 255 : g);
                    o[2] = (uint8_t)(b < 0 ? 0 : b > 255 ? 255 : b);
                } else { o[0] = s[0]; o[1] = s[1]; o[2] = s[2]; o[3] = s[3]; }
            }
        }
        return out;
    }

private:
    const uint8_t* p = nullptr;
    size_t n = 0, i = 0;
    int width = 0, height = 0, ncomp = 0, precision = 8;
    int maxH = 1, maxV = 1, restartInterval = 0;
    bool progressive = false;
    bool pixels = false;                    // planes are filled
    JpegComp comp[4];
    JpegHuff dc[4], ac[4];
    int qt[4][64];
    int scanComp[4] = { 0, 0, 0, 0 };
    int scanCount = 0, ss = 0, se = 63, ah = 0, al = 0;
    bool markerHit = false;
    JpegImage out;
    uint32_t bitBuf = 0;
    int bitCnt = 0;

    static const int kZigZag[64];

    // ── bit reader (stops cleanly at a marker) ──────────────────────────────
    int NextByte()
    {
        if (i >= n) { markerHit = true; return -1; }
        uint8_t b = p[i++];
        if (b != 0xFF) return b;
        size_t j = i;
        while (j < n && p[j] == 0xFF) ++j;
        if (j >= n) { markerHit = true; return -1; }
        if (p[j] == 0x00) { i = j + 1; return 0xFF; }
        markerHit = true;
        i = (j > 0) ? j - 1 : 0;
        return -1;
    }
    int Bit()
    {
        if (bitCnt == 0) {
            int b = NextByte();
            if (b < 0) return 0;
            bitBuf = (uint32_t)b;
            bitCnt = 8;
        }
        --bitCnt;
        return (int)((bitBuf >> bitCnt) & 1);
    }
    int Bits(int k)
    {
        int v = 0;
        for (int j = 0; j < k; ++j) v = (v << 1) | Bit();
        return v;
    }
    void AlignBits() { bitCnt = 0; }

    int DecodeHuff(const JpegHuff& h)
    {
        int code = 0;
        for (int len = 1; len <= 16; ++len) {
            code = (code << 1) | Bit();
            if (markerHit) return 0;
            if (h.maxCode[len] >= 0 && code <= h.maxCode[len]) {
                int idx = h.valPtr[len] + code - h.minCode[len];
                if (idx < 0 || idx > 255) return 0;
                return h.vals[idx];
            }
        }
        return 0;
    }
    static int Extend(int v, int t)
    {
        return (t == 0) ? 0 : ((v < (1 << (t - 1))) ? v - (1 << t) + 1 : v);
    }

    // ── segment parsers ─────────────────────────────────────────────────────
    void Frame(size_t s, size_t e, bool prog)
    {
        if (e - s < 6) return;
        progressive = prog;
        precision = p[s];
        height = (p[s + 1] << 8) | p[s + 2];
        width  = (p[s + 3] << 8) | p[s + 4];
        ncomp  = p[s + 5];
        if (precision != 8 || ncomp <= 0 || ncomp > 4 || e - s < 6 + (size_t)ncomp * 3) { ncomp = 0; return; }
        maxH = maxV = 1;
        for (int c = 0; c < ncomp; ++c) {
            size_t o = s + 6 + c * 3;
            comp[c].id = p[o];
            comp[c].h = p[o + 1] >> 4;
            comp[c].v = p[o + 1] & 15;
            comp[c].tq = p[o + 2];
            if (comp[c].h < 1) comp[c].h = 1;
            if (comp[c].v < 1) comp[c].v = 1;
            if (comp[c].h > 4) comp[c].h = 4;
            if (comp[c].v > 4) comp[c].v = 4;
            if (comp[c].h > maxH) maxH = comp[c].h;
            if (comp[c].v > maxV) maxV = comp[c].v;
        }
        int mcuCols = (width + maxH * 8 - 1) / (maxH * 8);
        int mcuRows = (height + maxV * 8 - 1) / (maxV * 8);
        for (int c = 0; c < ncomp; ++c) {
            JpegComp& cc = comp[c];
            // Interleaved scans emit a whole MCU of blocks, including padding
            // past the right/bottom edge; those blocks still consume bits, so
            // the plane must be sized on MCU boundaries or the decoder desyncs.
            cc.pw = mcuCols * cc.h * 8;
            cc.ph = mcuRows * cc.v * 8;
            cc.bw = cc.pw / 8;
            cc.bh = cc.ph / 8;
            cc.sw = ((width * cc.h + maxH - 1) / maxH + 7) / 8;
            cc.sh = ((height * cc.v + maxV - 1) / maxV + 7) / 8;
            cc.plane.assign((size_t)cc.pw * cc.ph, 128);
            if (progressive) {
                size_t blocks = (size_t)cc.bw * cc.bh;
                if (blocks * 64 > 8u * 1024 * 1024) { ncomp = 0; return; }   // cap coefficient memory
                cc.coef.assign(blocks * 64, 0);
            }
        }
    }

    void Dqt(size_t s, size_t e)
    {
        while (s < e) {
            int pq = p[s] >> 4, tq = p[s] & 15;
            ++s;
            if (tq > 3) return;
            for (int k = 0; k < 64; ++k) {
                int v;
                if (pq == 0) { if (s >= e) return; v = p[s++]; }
                else { if (s + 1 >= e) return; v = (p[s] << 8) | p[s + 1]; s += 2; }
                qt[tq][kZigZag[k]] = v;
            }
        }
    }

    void Dht(size_t s, size_t e)
    {
        while (s + 17 <= e) {
            int tc = p[s] >> 4, th = p[s] & 15;
            ++s;
            if (th > 3 || tc > 1) return;
            JpegHuff& h = (tc == 0) ? dc[th] : ac[th];
            memset(h.count, 0, sizeof h.count);
            int total = 0;
            for (int L = 1; L <= 16; ++L) { h.count[L] = p[s + L - 1]; total += h.count[L]; }
            s += 16;
            if (total > 256 || s + (size_t)total > e) return;
            for (int k = 0; k < total; ++k) h.vals[k] = p[s + k];
            s += total;
            int code = 0, k = 0;
            for (int L = 1; L <= 16; ++L) {
                h.valPtr[L] = k;
                h.minCode[L] = code;
                code += h.count[L];
                k += h.count[L];
                h.maxCode[L] = h.count[L] ? code - 1 : -1;
                if (code > 65535) return;
                code <<= 1;
            }
            h.maxCode[17] = 0x7FFFFFFF;
            h.ok = true;
        }
    }

    bool Sos(size_t s, size_t e)
    {
        if (!width || !height || !ncomp || s >= e) return false;
        int ns = p[s++];
        if (ns <= 0 || ns > ncomp || s + (size_t)ns * 2 + 3 > e) return false;
        scanCount = ns;
        for (int k = 0; k < ns; ++k) {
            int id = p[s];
            int td = p[s + 1] >> 4, ta = p[s + 1] & 15;
            s += 2;
            int ci = -1;
            for (int c = 0; c < ncomp; ++c) if (comp[c].id == id) ci = c;
            if (ci < 0) return false;
            comp[ci].td = td;
            comp[ci].ta = ta;
            scanComp[k] = ci;
        }
        ss = p[s];
        se = p[s + 1];
        ah = p[s + 2] >> 4;
        al = p[s + 2] & 15;
        if (!progressive) { ss = 0; se = 63; ah = 0; al = 0; }
        bitCnt = 0;
        markerHit = false;
        return true;
    }

    // ── IDCT ────────────────────────────────────────────────────────────────
    void IdctBlock(const int16_t* coef, const int* q, uint8_t* dst, int stride)
    {
        static const float kC[8][8] = {
            { 0.3535533906f, 0.3535533906f, 0.3535533906f, 0.3535533906f, 0.3535533906f, 0.3535533906f, 0.3535533906f, 0.3535533906f },
            { 0.4903926402f, 0.4157348062f, 0.2777851165f, 0.0975451610f, -0.0975451610f, -0.2777851165f, -0.4157348062f, -0.4903926402f },
            { 0.4619397663f, 0.1913417162f, -0.1913417162f, -0.4619397663f, -0.4619397663f, -0.1913417162f, 0.1913417162f, 0.4619397663f },
            { 0.4157348062f, -0.0975451610f, -0.4903926402f, -0.2777851165f, 0.2777851165f, 0.4903926402f, 0.0975451610f, -0.4157348062f },
            { 0.3535533906f, -0.3535533906f, -0.3535533906f, 0.3535533906f, 0.3535533906f, -0.3535533906f, -0.3535533906f, 0.3535533906f },
            { 0.2777851165f, -0.4903926402f, 0.0975451610f, 0.4157348062f, -0.4157348062f, -0.0975451610f, 0.4903926402f, -0.2777851165f },
            { 0.1913417162f, -0.4619397663f, 0.4619397663f, -0.1913417162f, -0.1913417162f, 0.4619397663f, -0.4619397663f, 0.1913417162f },
            { 0.0975451610f, -0.2777851165f, 0.4157348062f, -0.4903926402f, 0.4903926402f, -0.4157348062f, 0.2777851165f, -0.0975451610f },
        };
        float blk[64], tmp[64];
        for (int k = 0; k < 64; ++k) blk[k] = (float)coef[k] * (float)q[k];
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                float s = 0;
                for (int u = 0; u < 8; ++u) s += blk[y * 8 + u] * kC[u][x];
                tmp[y * 8 + x] = s;
            }
        for (int x = 0; x < 8; ++x)
            for (int y = 0; y < 8; ++y) {
                float s = 0;
                for (int v = 0; v < 8; ++v) s += tmp[v * 8 + x] * kC[v][y];
                int val = (int)floorf(s + 128.5f);
                if (val < 0) val = 0;
                if (val > 255) val = 255;
                dst[(size_t)y * stride + x] = (uint8_t)val;
            }
    }

    void SkipRestart()
    {
        while (i + 1 < n) {
            if (p[i] == 0xFF) {
                if (p[i + 1] >= 0xD0 && p[i + 1] <= 0xD7) { i += 2; markerHit = false; return; }
                return;
            }
            ++i;
        }
    }

    // ── baseline ────────────────────────────────────────────────────────────
    void BaseScan()
    {
        int mcuW = maxH * 8, mcuH = maxV * 8;
        int mx = (width + mcuW - 1) / mcuW;
        int my = (height + mcuH - 1) / mcuH;
        int pred[4] = { 0, 0, 0, 0 };
        int16_t coef[64];
        int total = 0;
        for (int yy = 0; yy < my && !markerHit; ++yy) {
            for (int xx = 0; xx < mx && !markerHit; ++xx) {
                for (int k = 0; k < scanCount && !markerHit; ++k) {
                    int ci = scanComp[k];
                    JpegComp& cc = comp[ci];
                    for (int by = 0; by < cc.v && !markerHit; ++by) {
                        for (int bx = 0; bx < cc.h && !markerHit; ++bx) {
                            memset(coef, 0, sizeof coef);
                            int t = DecodeHuff(dc[cc.td]);
                            if (t) pred[ci] += Extend(Bits(t), t);
                            coef[0] = (int16_t)pred[ci];
                            for (int z = 1; z < 64; ++z) {
                                int rs = DecodeHuff(ac[cc.ta]);
                                if (markerHit) break;
                                int r = rs >> 4, sz = rs & 15;
                                if (sz == 0) { if (r == 15) { z += 15; continue; } break; }
                                z += r;
                                if (z > 63) break;
                                coef[kZigZag[z]] = (int16_t)Extend(Bits(sz), sz);
                            }
                            int px = (xx * cc.h + bx) * 8;
                            int py = (yy * cc.v + by) * 8;
                            if (px + 8 <= cc.pw && py + 8 <= cc.ph)
                                IdctBlock(coef, qt[cc.tq], &cc.plane[(size_t)py * cc.pw + px], cc.pw);
                        }
                    }
                }
                ++total;
                if (restartInterval > 0 && total % restartInterval == 0) {
                    AlignBits();
                    SkipRestart();
                    pred[0] = pred[1] = pred[2] = pred[3] = 0;
                }
            }
        }
        if (ncomp) pixels = true;
    }

    // ── progressive ─────────────────────────────────────────────────────────
    // Spectral selection + successive approximation, following the reference
    // decoding order (EOB runs are counted per block, and a refinement bit is
    // only applied when that bit is not already set).
    void ProgScan()
    {
        int pred[4] = { 0, 0, 0, 0 };
        int eobRun = 0;
        int mcuX, mcuY;
        if (scanCount == 1) { mcuX = comp[scanComp[0]].sw; mcuY = comp[scanComp[0]].sh; }
        else {
            mcuX = (width + maxH * 8 - 1) / (maxH * 8);
            mcuY = (height + maxV * 8 - 1) / (maxV * 8);
        }
        int total = 0;
        for (int yy = 0; yy < mcuY && !markerHit; ++yy) {
            for (int xx = 0; xx < mcuX && !markerHit; ++xx) {
                for (int k = 0; k < scanCount && !markerHit; ++k) {
                    int ci = scanComp[k];
                    JpegComp& cc = comp[ci];
                    int bwx = (scanCount == 1) ? 1 : cc.h;
                    int bhy = (scanCount == 1) ? 1 : cc.v;
                    for (int by = 0; by < bhy && !markerHit; ++by) {
                        for (int bx = 0; bx < bwx && !markerHit; ++bx) {
                            int bcx = xx * bwx + bx, bcy = yy * bhy + by;
                            if (bcx >= cc.bw || bcy >= cc.bh) continue;
                            int16_t* blk = &cc.coef[((size_t)bcy * cc.bw + bcx) * 64];

                            if (ss == 0) {                                  // DC scan
                                if (ah == 0) {
                                    int t = DecodeHuff(dc[cc.td]);
                                    if (t) pred[ci] += Extend(Bits(t), t);
                                    blk[0] = (int16_t)(pred[ci] << al);
                                } else if (Bit()) {
#if PDFR_JPEG_DC_ADD
                                    blk[0] = (int16_t)(blk[0] + (blk[0] >= 0 ? (1 << al) : -(1 << al)));
#else
                                    blk[0] = (int16_t)(blk[0] | (1 << al));
#endif
                                }
                                continue;
                            }
                            if (ah == 0) {                                   // first AC scan
                                if (eobRun > 0) { --eobRun; continue; }
                                int z = ss;
                                for (;;) {
                                    int rs = DecodeHuff(ac[cc.ta]);
                                    if (markerHit) break;
                                    int r = rs >> 4, sz = rs & 15;
                                    if (sz == 0) {
                                        if (r < 15) {
                                            eobRun = (1 << r) - PDFR_JPEG_EOB_SUB;
                                            if (r) eobRun += Bits(r);
                                            break;
                                        }
                                        z += 16;                             // ZRL
                                    } else {
                                        z += r;
                                        if (z > 63) break;
                                        blk[kZigZag[z]] = (int16_t)(Extend(Bits(sz), sz) << al);
                                        ++z;
                                    }
                                    if (z > se) break;
                                }
                                continue;
                            }
                            // successive-approximation refinement
                            int bit = 1 << al;
                            if (eobRun > 0) {
                                --eobRun;
                                for (int z = ss; z <= se; ++z) {
                                    int16_t& v = blk[kZigZag[z]];
                                    if (v && Bit() && (!PDFR_JPEG_GUARD || (v & bit) == 0)) v = (int16_t)(v + (v > 0 ? bit : -bit));
                                }
                                continue;
                            }
                            int z = ss;
                            for (;;) {
                                int rs = DecodeHuff(ac[cc.ta]);
                                if (markerHit) break;
                                int r = rs >> 4, sz = rs & 15;
                                int newv = 0;
                                if (sz == 0 && r < 15) {
                                    // end of band: no further bits for this block
                                    eobRun = (1 << r) - PDFR_JPEG_EOB_SUB;
                                    if (r) eobRun += Bits(r);
                                    break;
                                }
                                if (sz) newv = Bit() ? bit : -bit;
                                bool stored = false;
                                while (z <= se) {
                                    int16_t& v = blk[kZigZag[z]];
                                    if (v) {
                                        if (Bit() && (!PDFR_JPEG_GUARD || (v & bit) == 0)) v = (int16_t)(v + (v > 0 ? bit : -bit));
                                    } else {
                                        if (r == 0) {
                                            if (newv) v = (int16_t)newv;
                                            ++z;
                                            stored = true;
                                            break;
                                        }
                                        --r;
                                    }
                                    ++z;
                                }
                                if (stored) { if (z > se) break; continue; }
                                break;
                            }
                        }
                    }
                }
                ++total;
                if (restartInterval > 0 && total % restartInterval == 0) {
                    AlignBits();
                    SkipRestart();
                    pred[0] = pred[1] = pred[2] = pred[3] = 0;
                    eobRun = 0;
                }
            }
        }
    }

    void Reconstruct()
    {
        if (!progressive || !ncomp || !width) return;
        for (int c = 0; c < ncomp; ++c) {
            JpegComp& cc = comp[c];
            if (cc.coef.empty()) continue;
            for (int by = 0; by < cc.bh; ++by)
                for (int bx = 0; bx < cc.bw; ++bx)
                    IdctBlock(&cc.coef[((size_t)by * cc.bw + bx) * 64], qt[cc.tq],
                              &cc.plane[(size_t)by * 8 * cc.pw + bx * 8], cc.pw);
        }
        pixels = true;
    }
};

const int JpegDecoder::kZigZag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63,
};

inline bool DecodeJpeg(const uint8_t* data, size_t len, JpegImage& out)
{
    JpegDecoder d;
    out = d.Decode(data, len);
    return out.w > 0 && out.h > 0 && !out.px.empty();
}

} // namespace pdfr
