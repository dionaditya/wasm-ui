// Native smoke test for the PDF document layer (no wasm, no rendering).
//   g++ -O2 -std=c++17 -I pdf tools/pdfcheck/doc_probe.cpp pdf/pdfdoc.cpp -lz -o /tmp/doc_probe
#include "../../pdf/pdfdoc.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>

using namespace pdf;

static std::string slurp(const char* p) {
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss; ss << f.rdbuf(); return ss.str();
}

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: doc_probe file.pdf\n"); return 2; }
    std::string data = slurp(argv[1]);
    Document doc;
    if (!doc.load((const uint8_t*)data.data(), data.size())) {
        printf("LOAD FAILED: %s\n", doc.error.c_str());
        return 1;
    }
    printf("pages=%d warnings=%d\n", doc.pageCount(), (int)doc.warnings.size());
    for (auto& w : doc.warnings) printf("  warn: %s\n", w.c_str());
    for (int i = 0; i < doc.pageCount(); i++) {
        double w, h; int rot;
        doc.pageSize(i, w, h, rot);
        printf("page %d: %.1f x %.1f rot=%d\n", i, w, h, rot);
    }
    // fonts on page 0 + resolve a few strings
    ObjP res = doc.resolve(doc.pages[0]->get("Resources"));
    int nf = 0;
    if (res) {
        ObjP fonts = doc.resolve(res->get("Font"));
        if (fonts) {
            for (auto& kv : fonts->dict) {
                Font* f = doc.font(kv.second);
                if (!f) continue;
                nf++;
                if (nf <= 6)
                    printf("  font /%s base=%s cls=%d bold=%d ital=%d first=%d widths=%d\n",
                           kv.first.c_str(), f->base.c_str(), f->faceClass(), f->faceBold(),
                           f->faceItalic(), f->firstChar, (int)f->widths.size());
            }
        }
    }
    printf("fonts on page0: %d\n", nf);

    // decode content stream and count operators
    ObjP contents = doc.resolve(doc.pages[0]->get("Contents"));
    std::string cs;
    if (contents && contents->isStream) doc.decodeStream(contents, cs);
    else if (contents && contents->t == O_ARR) {
        for (auto& e : contents->arr) { ObjP c = doc.resolve(e); std::string part; if (c && doc.decodeStream(c, part)) cs += part + "\n"; }
    }
    printf("content bytes=%d\n", (int)cs.size());
    ContentLexer lx(cs.data(), cs.size());
    Tok t; int ops = 0, nums = 0, strs = 0;
    std::string first;
    while (lx.next(t), t.type != T_EOF) {
        if (t.type == T_OP) { ops++; if (ops <= 12) { first += t.s + " "; } }
        else if (t.type == T_NUM) nums++;
        else if (t.type == T_STR) strs++;
        if (ops > 400000) break;
    }
    printf("tokens: ops=%d nums=%d strs=%d  first:%s\n", ops, nums, strs, first.c_str());
    return 0;
}
