// Host-side harness: parse a PDF with pdfread.hpp, dump page sizes, text and
// PPM renders. Used to validate the wasm build's reader without a browser.
#include "pdfread.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static bool ReadFile(const char* path, std::vector<uint8_t>& out)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize((size_t)n);
    size_t got = fread(out.data(), 1, out.size(), f);
    fclose(f);
    out.resize(got);
    return true;
}

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: pdfdump file.pdf [page] [scale] [outprefix]\n"); return 2; }
    std::vector<uint8_t> data;
    if (!ReadFile(argv[1], data)) { fprintf(stderr, "cannot read %s\n", argv[1]); return 2; }

    pdfr::Doc doc;
    if (!doc.Load(data.data(), data.size())) {
        printf("ERR %s\n", doc.err.empty() ? "parse failed" : doc.err.c_str());
        return 1;
    }
    printf("pages %d\n", doc.PageCount());
    for (int i = 0; i < doc.PageCount(); ++i) {
        float w, h;
        doc.PageSize(i, w, h);
        printf("page %d %.2f x %.2f\n", i, w, h);
    }
    int page = (argc > 2) ? atoi(argv[2]) : 0;
    float scale = (argc > 3) ? (float)atof(argv[3]) : 1.5f;
    std::string prefix = (argc > 4) ? argv[4] : "/tmp/pdfdump";

    std::vector<uint8_t> rgba;
    int W = 0, H = 0;
    std::string text;
    if (!pdfr::RenderPage(&doc, page, scale, rgba, W, H, &text)) { printf("render failed\n"); return 1; }
    printf("render %dx%d\n", W, H);

    std::string ppmPath = prefix + ".ppm";
    FILE* f = fopen(ppmPath.c_str(), "wb");
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; ++i) fwrite(&rgba[(size_t)i * 4], 1, 3, f);
    fclose(f);
    printf("ppm %s\n", ppmPath.c_str());

    std::string txtPath = prefix + ".txt";
    f = fopen(txtPath.c_str(), "wb");
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
    printf("text %zu bytes\n", text.size());
    return 0;
}
