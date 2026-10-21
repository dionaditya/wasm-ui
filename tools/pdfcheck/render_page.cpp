// Native render harness: page -> PPM (P6), so the C++ rasterizer can be diffed
// against a reference renderer without a browser.
//   g++ -O2 -std=c++17 -I pdf tools/pdfcheck/render_page.cpp pdf/pdfdoc.cpp pdf/pdfraster.cpp -lz -o /tmp/pdfrender
//   /tmp/pdfrender file.pdf 1.5 out/ [page]
#include "../../pdf/pdfdoc.hpp"
#include "../../pdf/pdfraster.hpp"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <cmath>

using namespace pdf;

static std::string slurp(const char* p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf(); return ss.str();
}

int main(int argc, char** argv) {
    if (argc < 4) { printf("usage: pdfrender file.pdf scale outprefix [page]\n"); return 2; }
    std::string data = slurp(argv[1]);
    double scale = atof(argv[2]);
    std::string prefix = argv[3];
    Document doc;
    auto t0 = std::chrono::steady_clock::now();
    if (!doc.load((const uint8_t*)data.data(), data.size())) {
        printf("LOAD FAILED: %s\n", doc.error.c_str());
        return 1;
    }
    auto t1 = std::chrono::steady_clock::now();
    printf("loaded: %d pages in %lld ms\n", doc.pageCount(),
           (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    int first = 0, last = doc.pageCount() - 1;
    if (argc >= 5) first = last = atoi(argv[4]);

    Renderer r(&doc);
    int totalGlyphs = 0, totalNotes = 0;
    for (int i = first; i <= last; i++) {
        Image img;
        RenderStats st;
        auto a = std::chrono::steady_clock::now();
        if (!r.renderPage(i, scale, img, &st)) { printf("page %d: render failed\n", i); continue; }
        auto b = std::chrono::steady_clock::now();
        char path[512];
        snprintf(path, sizeof(path), "%s%02d.ppm", prefix.c_str(), i);
        FILE* f = fopen(path, "wb");
        if (!f) { printf("cannot write %s\n", path); return 1; }
        fprintf(f, "P6\n%d %d\n255\n", img.w, img.h);
        for (size_t k = 0; k < img.rgba.size(); k += 4) fwrite(&img.rgba[k], 1, 3, f);
        fclose(f);
        // ink coverage: fraction of non-white pixels
        long ink = 0;
        for (size_t k = 0; k < img.rgba.size(); k += 4)
            if (img.rgba[k] < 240 || img.rgba[k + 1] < 240 || img.rgba[k + 2] < 240) ink++;
        if (getenv("PDF_OVERLAP")) {
            // glyph boxes that collide on the same line: the numeric gate for
            // advance/kerning bugs (0 expected)
            const auto& it2 = r.textItems();
            int pairs = 0;
            float worst = 0;
            for (size_t k = 1; k < it2.size(); k++) {
                const auto& a = it2[k - 1];
                const auto& b = it2[k];
                if (a.text.empty() || b.text.empty()) continue;
                if (std::fabs(a.y0 - b.y0) > 2.f) continue;
                float ov = a.x1 - b.x0;
                if (ov > 0.3f) { pairs++; if (ov > worst) worst = ov; }
            }
            printf("OVERLAP[%d]: %d colliding pairs (worst %.2f pt)\n", i, pairs, worst);
        }
        if (getenv("PDF_ITEMS")) {
            const auto& ti = r.textItems();
            for (size_t k = 0; k < ti.size() && k < 24; k++)
                printf("  [%2d] '%s' ox=%.2f oy=%.2f adv=%.2f fs=%.1f box=(%.1f,%.1f)-(%.1f,%.1f)\n",
                       (int)k, ti[k].text.c_str(), ti[k].ox, ti[k].oy, ti[k].adv, ti[k].fs,
                       ti[k].x0, ti[k].y0, ti[k].x1, ti[k].y1);
        }
        if (getenv("PDF_TEXT")) {
            std::string all;
            for (auto& it : r.textItems()) {
                if (!all.empty()) {
                    auto& prev = r.textItems()[0];
                    (void)prev;
                }
                all += it.text;
            }
            printf("TEXT[%d] (%d items): %s\n", i, (int)r.textItems().size(), all.substr(0, 700).c_str());
        }
        printf("page %2d: %dx%d %3lld ms ops=%d fill=%d stroke=%d texts=%d glyphs=%d imgs=%d "
               "ink=%.2f%% unsupported=%d %s\n",
               i, img.w, img.h,
               (long long)std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count(),
               st.ops, st.fills, st.strokes, st.texts, st.glyphs, st.images,
               100.0 * ink / (double)(img.w * img.h), st.unsupported, st.notes.c_str());
        totalGlyphs += st.glyphs;
        totalNotes += st.unsupported;
    }
    printf("total glyphs=%d unsupported-notes=%d\n", totalGlyphs, totalNotes);
    return 0;
}
