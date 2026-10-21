#include "imgui/imgui.h"
#include "fonts_data.h"   // embedded document faces + family table
#include "pdf_widths.h"
#include "pdf/pdfdoc.hpp"    // native PDF parser (no MuPDF, no JS)
#include "pdf/pdfraster.hpp" // native PDF rasterizer -> RGBA -> GL texture
#include <chrono>
#include "imgui/backends/imgui_impl_sdl2.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include <SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/fetch.h>
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif
#include <cstdio>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>
#include <deque>
#include <map>
#include <sstream>
#include <algorithm>
#include <sstream>
#include <cstdlib>
#include "duckx/duckx.hpp"
#include "duckx/zip.h"
#include "imgui/imgui_internal.h"
#include "doctext.hpp"        // document text layout engine (lines, caret, hit-test, rich text)
#include "fonts_data.h"      // embedded DejaVu Sans: regular / bold / oblique / bold-oblique

// ─── Tailwind-inspired palette ────────────────────────────────────────────────
namespace Col {
    static const ImVec4 bg        = {0.059f, 0.090f, 0.165f, 1.f};
    static const ImVec4 surface   = {0.118f, 0.161f, 0.235f, 1.f};
    static const ImVec4 border    = {0.200f, 0.255f, 0.333f, 1.f};
    static const ImVec4 primary   = {0.231f, 0.510f, 0.965f, 1.f};
    static const ImVec4 primaryHov= {0.376f, 0.647f, 0.980f, 1.f};
    static const ImVec4 primaryAct= {0.149f, 0.392f, 0.922f, 1.f};
    static const ImVec4 success   = {0.133f, 0.773f, 0.369f, 1.f};
    static const ImVec4 warning   = {0.984f, 0.729f, 0.000f, 1.f};
    static const ImVec4 danger    = {0.937f, 0.267f, 0.267f, 1.f};
    static const ImVec4 text      = {0.945f, 0.961f, 0.980f, 1.f};
    static const ImVec4 muted     = {0.580f, 0.639f, 0.718f, 1.f};
}

static ImU32 ToU32(ImVec4 v, float a = 1.f) {
    return IM_COL32((int)(v.x*255),(int)(v.y*255),(int)(v.z*255),(int)(a*255));
}

static void SetupModernStyle()
{
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowPadding      = {16, 16};
    s.FramePadding       = {10,  7};
    s.CellPadding        = {8,   5};
    s.ItemSpacing        = {8,   8};
    s.ItemInnerSpacing   = {6,   4};
    s.ScrollbarSize      = 10;
    s.GrabMinSize        = 12;
    s.IndentSpacing      = 18;
    s.WindowRounding     = 10;
    s.ChildRounding      = 8;
    s.FrameRounding      = 6;
    s.PopupRounding      = 8;
    s.ScrollbarRounding  = 6;
    s.GrabRounding       = 6;
    s.TabRounding        = 6;
    s.WindowBorderSize   = 0;
    s.ChildBorderSize    = 1;
    s.FrameBorderSize    = 1;
    s.PopupBorderSize    = 1;
    s.TabBorderSize      = 0;
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]             = Col::bg;
    c[ImGuiCol_ChildBg]              = Col::surface;
    c[ImGuiCol_PopupBg]              = {Col::surface.x, Col::surface.y, Col::surface.z, 0.98f};
    c[ImGuiCol_Border]               = Col::border;
    c[ImGuiCol_BorderShadow]         = {0,0,0,0};
    c[ImGuiCol_FrameBg]              = Col::surface;
    c[ImGuiCol_FrameBgHovered]       = {0.176f,0.235f,0.329f,1.f};
    c[ImGuiCol_FrameBgActive]        = {0.145f,0.200f,0.290f,1.f};
    c[ImGuiCol_TitleBg]              = Col::bg;
    c[ImGuiCol_TitleBgActive]        = {0.090f,0.133f,0.231f,1.f};
    c[ImGuiCol_TitleBgCollapsed]     = {Col::bg.x,Col::bg.y,Col::bg.z,0.8f};
    c[ImGuiCol_MenuBarBg]            = Col::bg;
    c[ImGuiCol_ScrollbarBg]          = {Col::bg.x,Col::bg.y,Col::bg.z,0.5f};
    c[ImGuiCol_ScrollbarGrab]        = Col::border;
    c[ImGuiCol_ScrollbarGrabHovered] = {0.231f,0.290f,0.373f,1.f};
    c[ImGuiCol_ScrollbarGrabActive]  = Col::primary;
    c[ImGuiCol_CheckMark]            = Col::primary;
    c[ImGuiCol_SliderGrab]           = Col::primary;
    c[ImGuiCol_SliderGrabActive]     = Col::primaryAct;
    c[ImGuiCol_Button]               = Col::primary;
    c[ImGuiCol_ButtonHovered]        = Col::primaryHov;
    c[ImGuiCol_ButtonActive]         = Col::primaryAct;
    c[ImGuiCol_Header]               = {Col::primary.x,Col::primary.y,Col::primary.z,0.3f};
    c[ImGuiCol_HeaderHovered]        = {Col::primary.x,Col::primary.y,Col::primary.z,0.5f};
    c[ImGuiCol_HeaderActive]         = {Col::primary.x,Col::primary.y,Col::primary.z,0.7f};
    c[ImGuiCol_Separator]            = Col::border;
    c[ImGuiCol_SeparatorHovered]     = {Col::primary.x,Col::primary.y,Col::primary.z,0.8f};
    c[ImGuiCol_SeparatorActive]      = Col::primary;
    c[ImGuiCol_ResizeGrip]           = {Col::primary.x,Col::primary.y,Col::primary.z,0.2f};
    c[ImGuiCol_ResizeGripHovered]    = {Col::primary.x,Col::primary.y,Col::primary.z,0.5f};
    c[ImGuiCol_ResizeGripActive]     = Col::primary;
    c[ImGuiCol_Tab]                  = {Col::bg.x,Col::bg.y,Col::bg.z,0.f};
    c[ImGuiCol_TabHovered]           = {Col::primary.x,Col::primary.y,Col::primary.z,0.3f};
    c[ImGuiCol_TabSelected]          = Col::primary;
    c[ImGuiCol_TabSelectedOverline]  = Col::primaryHov;
    c[ImGuiCol_TabDimmed]            = Col::bg;
    c[ImGuiCol_TabDimmedSelected]    = {0.118f,0.200f,0.400f,1.f};
    c[ImGuiCol_Text]                 = Col::text;
    c[ImGuiCol_TextDisabled]         = Col::muted;
    c[ImGuiCol_PlotLines]            = Col::primary;
    c[ImGuiCol_PlotLinesHovered]     = Col::primaryHov;
    c[ImGuiCol_PlotHistogram]        = Col::primary;
    c[ImGuiCol_PlotHistogramHovered] = Col::primaryHov;
    c[ImGuiCol_TableHeaderBg]        = Col::bg;
    c[ImGuiCol_TableBorderStrong]    = Col::border;
    c[ImGuiCol_TableBorderLight]     = {0.145f,0.200f,0.290f,1.f};
    c[ImGuiCol_TableRowBg]           = {0,0,0,0};
    c[ImGuiCol_TableRowBgAlt]        = {1,1,1,0.03f};
    c[ImGuiCol_TextSelectedBg]       = {Col::primary.x,Col::primary.y,Col::primary.z,0.3f};
    c[ImGuiCol_NavCursor]            = Col::primary;
    c[ImGuiCol_ModalWindowDimBg]     = {Col::bg.x,Col::bg.y,Col::bg.z,0.7f};
}

// ─── Reusable widgets ─────────────────────────────────────────────────────────
static void CardBegin(const char* id, ImVec2 size = {0,0})
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Col::surface);
    ImGui::BeginChild(id, size, ImGuiChildFlags_None);
}
static void CardEnd() { ImGui::EndChild(); ImGui::PopStyleColor(); }

static void MetricCard(const char* id, const char* label,
                        const char* value, const char* sub,
                        ImVec4 accent, float w, float h)
{
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, Col::surface);
    ImGui::BeginChild(id, {w, h}, ImGuiChildFlags_None);
    ImGui::GetWindowDrawList()->AddRectFilled(
        pos, {pos.x + 4, pos.y + h},
        ToU32(accent), 4);
    ImGui::SetCursorPosX(14);
    ImGui::SetCursorPosY(14);
    ImGui::PushStyleColor(ImGuiCol_Text, Col::muted);
    ImGui::TextUnformatted(label);
    ImGui::PopStyleColor();
    ImGui::SetCursorPosX(14);
    ImGui::SetWindowFontScale(1.9f);
    ImGui::PushStyleColor(ImGuiCol_Text, Col::text);
    ImGui::TextUnformatted(value);
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.0f);
    ImGui::SetCursorPosX(14);
    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::TextUnformatted(sub);
    ImGui::PopStyleColor();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

static void Badge(const char* text, ImVec4 bg, ImVec4 fg = Col::text)
{
    ImVec2 p   = ImGui::GetCursorScreenPos();
    ImVec2 tsz = ImGui::CalcTextSize(text);
    float  pad = 6, rnd = 4;
    ImGui::GetWindowDrawList()->AddRectFilled(
        {p.x - pad, p.y - 2}, {p.x + tsz.x + pad, p.y + tsz.y + 2},
        ToU32(bg), rnd);
    ImGui::PushStyleColor(ImGuiCol_Text, fg);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
}

static bool PrimaryButton(const char* label, ImVec2 size = {0,0})
{ return ImGui::Button(label, size); }

static bool DangerButton(const char* label, ImVec2 size = {0,0})
{
    ImGui::PushStyleColor(ImGuiCol_Button,        Col::danger);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {1.f, 0.4f, 0.4f, 1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.8f, 0.1f, 0.1f, 1.f});
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return r;
}

static bool SuccessButton(const char* label, ImVec2 size = {0,0})
{
    ImGui::PushStyleColor(ImGuiCol_Button,        Col::success);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.2f, 0.9f, 0.5f, 1.f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  {0.1f, 0.6f, 0.3f, 1.f});
    bool r = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return r;
}

// ─── App state ────────────────────────────────────────────────────────────────
struct AppState {
    SDL_Window*   window     = nullptr;
    SDL_GLContext gl_context = nullptr;
    int   clicks     = 0;
    float progress   = 0.62f;
    float perf_data[80];
    int   perf_idx   = 0;
    char  username[64]  = "ada_lovelace";
    char  email[128]    = "ada@example.com";
    char  bio[256]      = "First programmer. Fan of the Analytical Engine.";
    bool  notif_email   = true;
    bool  notif_push    = false;
    bool  dark_mode     = true;
    float vol      = 0.7f;
    float bright   = 0.85f;
    float accent[3]= {0.231f, 0.510f, 0.965f};
    int   lang_idx = 0;
    bool  autosave = true;
    bool  telemetry= false;
    int  active_tab = 0;
    bool open_deploy_modal  = false;
    bool open_delete_modal  = false;
    bool open_saved_modal   = false;
    char deploy_env[32]     = "production";
    std::string status_msg;
    bool delete_confirmed   = false;
    char delete_type_buf[32]= "";
    void init() {
        for (int i = 0; i < 80; i++)
            perf_data[i] = 40 + 30*sinf(i * 0.3f) + 10*sinf(i * 0.7f + 1.f);
    }
};
static AppState g;

// ─── RSS feed state ───────────────────────────────────────────────────────────
struct RssItem {
    std::string title;
    std::string link;
    std::string date;
    std::string desc;
};
struct FeedState {
    std::vector<RssItem> items;
    bool loading = false;
    bool fetched = false;
    bool error   = false;
    char error_msg[128] = "";
};
static FeedState gFeed;

static std::string rss_str(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":\"";
    size_t p = json.find(needle);
    if (p == std::string::npos) return "";
    p += needle.size();
    std::string out;
    while (p < json.size() && json[p] != '"') {
        if (json[p] == '\\' && p + 1 < json.size()) {
            ++p;
            if      (json[p] == 'n') out += '\n';
            else if (json[p] == 'r') {}
            else                     out += json[p];
        } else { out += json[p]; }
        ++p;
    }
    return out;
}
static std::string strip_html(const std::string& s) {
    std::string out; bool tag = false;
    for (char c : s) {
        if (c == '<') tag = true;
        else if (c == '>') tag = false;
        else if (!tag) out += c;
    }
    if (out.size() > 160) { out.resize(157); out += "..."; }
    return out;
}
static void on_fetch_ok(emscripten_fetch_t* f) {
    std::string body(f->data, f->numBytes);
    gFeed.items.clear();
    size_t arr = body.find("\"items\":[");
    if (arr == std::string::npos) {
        gFeed.error = true;
        strncpy(gFeed.error_msg, "Unexpected response format", sizeof(gFeed.error_msg)-1);
        gFeed.loading = false;
        emscripten_fetch_close(f);
        return;
    }
    size_t pos = arr + 9;
    while (gFeed.items.size() < 20) {
        size_t s = body.find('{', pos);
        if (s == std::string::npos) break;
        int depth = 1; size_t e = s + 1;
        while (e < body.size() && depth > 0) {
            if (body[e] == '{') depth++;
            else if (body[e] == '}') depth--;
            e++;
        }
        std::string obj = body.substr(s, e - s);
        RssItem item;
        item.title = rss_str(obj, "title");
        item.link  = rss_str(obj, "link");
        item.date  = rss_str(obj, "pubDate");
        item.desc  = strip_html(rss_str(obj, "description"));
        if (!item.title.empty()) gFeed.items.push_back(std::move(item));
        pos = e;
    }
    gFeed.loading = false;
    gFeed.fetched = true;
    gFeed.error   = false;
    emscripten_fetch_close(f);
}
static void on_fetch_err(emscripten_fetch_t* f) {
    gFeed.loading = false;
    gFeed.error   = true;
    snprintf(gFeed.error_msg, sizeof(gFeed.error_msg), "HTTP %d", (int)f->status);
    emscripten_fetch_close(f);
}
static void start_rss_fetch() {
    if (gFeed.loading) return;
    gFeed.loading = true; gFeed.error = false;
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.onsuccess  = on_fetch_ok;
    attr.onerror    = on_fetch_err;
    emscripten_fetch(&attr,
        "https://api.rss2json.com/v1/api.json"
        "?rss_url=https%3A%2F%2Fhnrss.org%2Ffrontpage");
}

extern "C" {
    EMSCRIPTEN_KEEPALIVE int imgui_want_text_input() {
        return ImGui::GetIO().WantTextInput ? 1 : 0;
    }
    // debug counters (exposed through doc_debug_state for automated tests)
    static int gDbgKeyCode = 0, gDbgKeyDown = 0, gDbgKeyCount = 0;
    static int gDbgCtrl = 0, gDbgShift = 0;
    EMSCRIPTEN_KEEPALIVE void imgui_add_char(int c) {
        ImGui::GetIO().AddInputCharacter((unsigned int)c);
    }
    // Legacy JS keycodes → ImGui keys.  The JS bridge sends -1 for "release
    // every key" (window blur), and plain ASCII codes 48..57 / 65..90 for
    // digits / letters so that Ctrl+C / Ctrl+V / Ctrl+Z / Ctrl+B ... reach ImGui
    // (they never produce a 'char' event, so they used to be invisible).
    EMSCRIPTEN_KEEPALIVE void imgui_add_key(int keycode, int down) {
        ImGuiIO& io = ImGui::GetIO();
        gDbgKeyCode = keycode; gDbgKeyDown = down; ++gDbgKeyCount;
        if (keycode == -1) {
            ImGuiKey mods[] = { ImGuiKey_LeftShift, ImGuiKey_RightShift,
                                ImGuiKey_LeftCtrl,  ImGuiKey_RightCtrl,
                                ImGuiKey_LeftAlt,   ImGuiKey_RightAlt,
                                ImGuiKey_LeftSuper, ImGuiKey_RightSuper };
            for (ImGuiKey k : mods) io.AddKeyEvent(k, false);
            return;
        }
        ImGuiKey k = ImGuiKey_None;
        switch (keycode) {
            case 8:  k = ImGuiKey_Backspace;    break;
            case 13: k = ImGuiKey_Enter;        break;
            case 9:  k = ImGuiKey_Tab;          break;
            case 27: k = ImGuiKey_Escape;       break;
            case 37: k = ImGuiKey_LeftArrow;    break;
            case 39: k = ImGuiKey_RightArrow;   break;
            case 38: k = ImGuiKey_UpArrow;      break;
            case 40: k = ImGuiKey_DownArrow;    break;
            case 46: k = ImGuiKey_Delete;       break;
            case 36: k = ImGuiKey_Home;         break;
            case 35: k = ImGuiKey_End;          break;
            case 33: k = ImGuiKey_PageUp;       break;
            case 34: k = ImGuiKey_PageDown;     break;
            case 188: k = ImGuiKey_Comma;       break;
            case 190: k = ImGuiKey_Period;      break;
            case 189: k = ImGuiKey_Minus;       break;
            case 187: k = ImGuiKey_Equal;       break;
            // ── Modifier keys — required for Shift+Click / Shift+Arrow /
            //    Ctrl+C / Ctrl+A / Ctrl+Z etc.
            case 16: k = ImGuiKey_LeftShift;    break;
            case 17: k = ImGuiKey_LeftCtrl;     break;
            case 18: k = ImGuiKey_LeftAlt;      break;
            case 91: case 93: k = ImGuiKey_LeftSuper; break;
            default:
                if (keycode >= '0' && keycode <= '9')
                    k = (ImGuiKey)(ImGuiKey_0 + (keycode - '0'));
                else if (keycode >= 'A' && keycode <= 'Z')
                    k = (ImGuiKey)(ImGuiKey_A + (keycode - 'A'));
                else if (keycode >= 'a' && keycode <= 'z')
                    k = (ImGuiKey)(ImGuiKey_A + (keycode - 'a'));
                break;
        }
        if (k != ImGuiKey_None) io.AddKeyEvent(k, down != 0);
    }
}

// ─── PDF Editor state ─────────────────────────────────────────────────────────
static struct PdfState {
    int   page_count  = 0;
    int   cur_page    = 0;
    float zoom        = 1.0f;
    struct PageTex {
        GLuint id = 0;
        int    w  = 0, h = 0;
    };
    std::vector<PageTex> pages;
    struct Annot {
        int   kind    = 0;
        float x=0,y=0,w=0,h=0;
        int   page    = 0;
        std::string text;
        ImU32 color   = IM_COL32(255,220,0,150);
        std::vector<std::pair<float,float>> stroke;
        bool  selected = false;
    };
    std::vector<Annot> annots;
    int  sel_annot    = -1;
    enum Tool { Select, TextTool, RectTool, HighlightTool, FreehandTool } tool = Select;
    bool placing = false;
    bool drawing = false;
    ImVec2 drag_start{};
    char  text_buf[512] = {};
    int   text_annot_idx = -1;
    std::string status;
    bool loading = false;

    // native renderer state
    pdf::Document      doc;
    std::vector<pdf::RenderStats> stats;
    // text layer per page (page space, points, y up) + current selection
    std::vector<std::vector<pdf::TextItem>> page_text;
    int   sel_a = -1, sel_b = -1;   // selected item range on the current page
    bool  sel_dragging = false;
    int   sel_page = -1;
    int   render_ms   = 0;
    int   page_w_pt   = 612, page_h_pt = 792;
    bool  tried_autoload = false;
    std::string doc_name;
} gPdf;

// Scale the page is rasterized at; the zoom slider rescales the texture in the
// shader-free path (ImGui draws a textured quad, i.e. OpenGL).
static const double kPdfRenderScale = 1.5;

static void PdfUploadTexture(int idx, const pdf::Image& img)
{
    auto& p = gPdf.pages[idx];
    if (!p.id) glGenTextures(1, &p.id);
    glBindTexture(GL_TEXTURE_2D, p.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.w, img.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.rgba.data());
    p.w = img.w;
    p.h = img.h;
}

static bool PdfRenderPageNative(int idx, double scale)
{
    if (!gPdf.doc.loaded || idx < 0 || idx >= gPdf.page_count) return false;
    auto t0 = std::chrono::steady_clock::now();
    pdf::Image img;
    pdf::RenderStats st;
    pdf::Renderer r(&gPdf.doc);
    if (!r.renderPage(idx, scale, img, &st)) {
        gPdf.status = "render failed (page " + std::to_string(idx + 1) + ")";
        return false;
    }
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0).count();
    PdfUploadTexture(idx, img);
    if ((int)gPdf.stats.size() <= idx) gPdf.stats.resize(idx + 1);
    gPdf.stats[idx] = st;
    if ((int)gPdf.page_text.size() <= idx) gPdf.page_text.resize(idx + 1);
    gPdf.page_text[idx] = r.textItems();
    gPdf.render_ms = (int)ms;
    return true;
}

static bool PdfLoadBytes(const uint8_t* data, int len, const char* name)
{
    for (auto& p : gPdf.pages) if (p.id) glDeleteTextures(1, &p.id);
    gPdf.pages.clear();
    gPdf.annots.clear();
    gPdf.sel_annot = -1;
    gPdf.cur_page  = 0;
    gPdf.page_count = 0;
    gPdf.stats.clear();
    gPdf.page_text.clear();
    gPdf.sel_a = gPdf.sel_b = -1;
    gPdf.sel_dragging = false;
    gPdf.doc = pdf::Document();
    auto t0 = std::chrono::steady_clock::now();
    if (!gPdf.doc.load(data, (size_t)len)) {
        gPdf.status = std::string("Cannot read PDF: ") + gPdf.doc.error;
        return false;
    }
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0).count();
    gPdf.page_count = gPdf.doc.pageCount();
    gPdf.pages.assign(gPdf.page_count, PdfState::PageTex{});
    gPdf.stats.assign(gPdf.page_count, pdf::RenderStats());
    gPdf.doc_name = name && *name ? name : "document";
    double w = 612, h = 792; int rot = 0;
    gPdf.doc.pageSize(0, w, h, rot);
    gPdf.page_w_pt = (int)lround(w);
    gPdf.page_h_pt = (int)lround(h);
    bool ok = PdfRenderPageNative(0, kPdfRenderScale);
    gPdf.status = gPdf.doc_name + ": " + std::to_string(gPdf.page_count) + " pages, parsed in " +
                  std::to_string(ms) + " ms" + (ok ? ", page 1 rendered in " +
                  std::to_string(gPdf.render_ms) + " ms" : std::string(", render failed"));
    return true;
}

static bool PdfLoadFileNative(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return false; }
    std::vector<uint8_t> buf((size_t)n);
    size_t got = fread(buf.data(), 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) return false;
    const char* base = strrchr(path, '/');
    return PdfLoadBytes(buf.data(), (int)buf.size(), base ? base + 1 : path);
}

// ── text selection -----------------------------------------------------------
// Annotation coords live in texture pixels; the text layer lives in page points.
static inline float PdfPxPerPt() { return (float)kPdfRenderScale; }

// item index under a page-space point, or -1
static int PdfTextHit(const std::vector<pdf::TextItem>& items, float pxpt, float pypt)
{
    for (int i = (int)items.size() - 1; i >= 0; i--) {
        const auto& it = items[i];
        if (it.text.empty()) continue;
        // a little vertical slack: glyph boxes are tight around the baseline
        if (pxpt >= it.x0 - 0.6f && pxpt <= it.x1 + 0.6f &&
            pypt >= it.y0 - 1.0f && pypt <= it.y1 + 1.0f)
            return i;
    }
    // fall back to the nearest item on the same visual line
    int best = -1;
    float bestD = 4.0f;
    for (int i = 0; i < (int)items.size(); i++) {
        const auto& it = items[i];
        if (it.text.empty()) continue;
        if (pypt < it.y0 - 1.0f || pypt > it.y1 + 1.0f) continue;
        float dx = pxpt < it.x0 ? it.x0 - pxpt : (pxpt > it.x1 ? pxpt - it.x1 : 0.f);
        if (dx < bestD) { bestD = dx; best = i; }
    }
    return best;
}

static std::string PdfSelectedText()
{
    if (gPdf.sel_page != gPdf.cur_page) return std::string();
    if (gPdf.cur_page >= (int)gPdf.page_text.size()) return std::string();
    const auto& items = gPdf.page_text[gPdf.cur_page];
    int a = gPdf.sel_a, b = gPdf.sel_b;
    if (a < 0 || b < 0 || items.empty()) return std::string();
    if (a > b) std::swap(a, b);
    a = std::max(0, a); b = std::min((int)items.size() - 1, b);
    std::string out;
    const pdf::TextItem* prev = nullptr;
    for (int i = a; i <= b; i++) {
        const auto& it = items[i];
        if (prev) {
            float fs = prev->fs > 0.1f ? prev->fs : 10.f;
            bool newLine = std::fabs(it.oy - prev->oy) > fs * 0.5f;
            if (newLine) {
                out += '\n';
            } else {
                // a real space shows up as a gap beyond the previous advance
                float gap = it.ox - (prev->ox + prev->adv);
                if (gap > fs * 0.14f && !out.empty() && out.back() != '\n') out += ' ';
            }
        }
        out += it.text;
        prev = &it;
    }
    return out;
}

static void PdfCopySelection()
{
    std::string s = PdfSelectedText();
    if (s.empty()) return;
    EM_ASM({
        var t = UTF8ToString($0);
        if (navigator.clipboard && navigator.clipboard.writeText) {
            navigator.clipboard.writeText(t);
        } else {
            var ta = document.createElement('textarea');
            ta.value = t; document.body.appendChild(ta); ta.select();
            document.execCommand('copy'); document.body.removeChild(ta);
        }
    }, s.c_str());
    gPdf.status = "copied " + std::to_string(s.size()) + " characters";
}

extern "C" {
    // Parse + rasterize a PDF entirely in C++. Returns the page count, or < 0.
    EMSCRIPTEN_KEEPALIVE int pdf_open_document(const uint8_t* data, int len) {
        if (!PdfLoadBytes(data, len, "document.pdf")) return -1;
        return gPdf.page_count;
    }
    EMSCRIPTEN_KEEPALIVE int pdf_open_file(const char* path) {
        if (!PdfLoadFileNative(path)) return -1;
        return gPdf.page_count;
    }
    // Rasterize one page into its OpenGL texture. Returns 1 on success.
    EMSCRIPTEN_KEEPALIVE int pdf_render_page(int idx, float scale) {
        return PdfRenderPageNative(idx, scale > 0.1f ? (double)scale : kPdfRenderScale) ? 1 : 0;
    }
    EMSCRIPTEN_KEEPALIVE int pdf_page_count() { return gPdf.page_count; }
    EMSCRIPTEN_KEEPALIVE void pdf_open_picker() {
        EM_ASM({ var el = document.getElementById('pdf-file-input'); if (el) el.click(); });
    }
    EMSCRIPTEN_KEEPALIVE const char* pdf_selected_debug() {
        static std::string out;
        out = PdfSelectedText();
        return out.c_str();
    }
    EMSCRIPTEN_KEEPALIVE int pdf_text_item_count() {
        if (gPdf.cur_page >= (int)gPdf.page_text.size()) return 0;
        return (int)gPdf.page_text[gPdf.cur_page].size();
    }
    EMSCRIPTEN_KEEPALIVE const char* pdf_doc_debug() {
        static std::string js;
        int unsupported = 0, glyphs = 0;
        std::string notes;
        for (auto& st : gPdf.stats) {
            unsupported += st.unsupported;
            glyphs += st.glyphs;
            if (notes.size() < 160 && !st.notes.empty()) { notes += st.notes.substr(0, 160); notes += " "; }
        }
        js = "{\"loaded\":" + std::string(gPdf.doc.loaded ? "true" : "false") +
             ",\"pages\":" + std::to_string(gPdf.page_count) +
             ",\"page\":" + std::to_string(gPdf.cur_page + 1) +
             ",\"tex_w\":" + std::to_string(gPdf.pages.empty() ? 0 : gPdf.pages[0].w) +
             ",\"tex_h\":" + std::to_string(gPdf.pages.empty() ? 0 : gPdf.pages[0].h) +
             ",\"render_ms\":" + std::to_string(gPdf.render_ms) +
             ",\"glyphs\":" + std::to_string(glyphs) +
             ",\"unsupported\":" + std::to_string(unsupported) +
             ",\"notes\":\"" + notes + "\""
             ",\"status\":\"" + gPdf.status + "\"}";
        return js.c_str();
    }
}

// ─── Tabs (Dashboard / Profile / Settings / Spreadsheet / Feed / PDF) ─────────
// These were stubs ("same as original file") — the real widgets are below.

static void TabDashboard()
{
    float W = ImGui::GetContentRegionAvail().x;
    float cardW = std::max(150.f, (W - 3 * 12.f) / 4.f);

    char v[32];
    snprintf(v, sizeof(v), "%d", g.clicks);
    MetricCard("##m1", "Total Clicks", v, "+12% this week",  Col::primary, cardW, 96.f);
    ImGui::SameLine(0, 12);
    snprintf(v, sizeof(v), "%.0f%%", g.progress * 100.f);
    MetricCard("##m2", "Build Progress", v, "1.2 MB wasm",   Col::success, cardW, 96.f);
    ImGui::SameLine(0, 12);
    snprintf(v, sizeof(v), "%.0f", 60.f + 0.25f * (float)g.clicks);
    MetricCard("##m3", "FPS", v, "requestAnimationFrame",     Col::warning, cardW, 96.f);
    ImGui::SameLine(0, 12);
    MetricCard("##m4", "Memory", "18.4 MB", "linear mem, growable", Col::danger, cardW, 96.f);

    ImGui::Spacing();
    ImGui::BeginGroup();
    CardBegin("##perfcard", {W * 0.62f - 6.f, 210.f});
    ImGui::TextUnformatted("Performance");
    ImGui::SameLine(0, 14);
    Badge("live", {0.133f, 0.773f, 0.369f, 0.22f}, Col::text);
    ImGui::PushStyleColor(ImGuiCol_Text, Col::muted);
    ImGui::TextUnformatted("frame time, last 80 samples");
    ImGui::PopStyleColor();
    // animate the ring buffer
    g.perf_data[g.perf_idx] = 40.f + 30.f * sinf((float)ImGui::GetTime() * 1.4f)
                            + 10.f * sinf((float)ImGui::GetTime() * 3.7f + 1.f);
    g.perf_idx = (g.perf_idx + 1) % 80;
    ImGui::PushStyleColor(ImGuiCol_PlotLines, Col::primary);
    ImGui::PlotLines("##perf", g.perf_data, 80, g.perf_idx, nullptr, 0.f, 100.f, {-1, 120.f});
    ImGui::PopStyleColor();
    CardEnd();

    ImGui::SameLine(0, 12);
    CardBegin("##progcard", {W * 0.38f - 6.f, 210.f});
    ImGui::TextUnformatted("Deployment");
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, Col::muted);
    ImGui::TextUnformatted("progress");
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, Col::primary);
    ImGui::ProgressBar(g.progress, {-1, 18.f});
    ImGui::PopStyleColor();
    ImGui::Spacing();
    { float pct = g.progress * 100.f;
      if (ImGui::SliderFloat("##prog", &pct, 0.f, 100.f, "%.0f%%"))
          g.progress = pct / 100.f; }
    ImGui::Spacing();
    if (PrimaryButton("Deploy", {90, 30})) g.open_deploy_modal = true;
    ImGui::SameLine();
    if (DangerButton("Rollback", {90, 30})) g.open_delete_modal = true;
    ImGui::SameLine();
    if (ImGui::Button("Click me", {90, 30})) g.clicks++;
    CardEnd();
    ImGui::EndGroup();
}

static void TabProfile()
{
    float W = ImGui::GetContentRegionAvail().x;
    CardBegin("##profile", {std::min(W, 620.f), 0.f});
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled({p.x + 34, p.y + 34}, 32.f, ToU32(Col::primary));
    const char* initials = "AL";
    ImVec2 ts = ImGui::CalcTextSize(initials);
    dl->AddText({p.x + 34 - ts.x * 0.5f, p.y + 34 - ts.y * 0.5f},
                IM_COL32(255, 255, 255, 255), initials);
    ImGui::Dummy({72.f, 72.f});
    ImGui::SameLine(0, 12);
    ImGui::BeginGroup();
    ImGui::PushStyleColor(ImGuiCol_Text, Col::text);
    ImGui::TextUnformatted(g.username);
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_Text, Col::muted);
    ImGui::TextUnformatted(g.email);
    ImGui::PopStyleColor();
    Badge("admin", {0.231f, 0.510f, 0.965f, 0.30f}, Col::text);
    ImGui::EndGroup();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushItemWidth(-1);
    ImGui::TextUnformatted("Username");
    ImGui::InputText("##user", g.username, sizeof(g.username));
    ImGui::Spacing();
    ImGui::TextUnformatted("Email");
    ImGui::InputText("##email", g.email, sizeof(g.email));
    ImGui::Spacing();
    ImGui::TextUnformatted("Bio");
    ImGui::InputTextMultiline("##bio", g.bio, sizeof(g.bio), {-1, 60.f});
    ImGui::PopItemWidth();
    ImGui::Spacing();
    if (PrimaryButton("Save profile", {120, 30})) g.open_saved_modal = true;
    CardEnd();
}

static void TabSettings()
{
    float W = ImGui::GetContentRegionAvail().x;
    CardBegin("##settings", {std::min(W, 620.f), 0.f});
    ImGui::PushItemWidth(-150.f);
    ImGui::TextUnformatted("Notifications");
    ImGui::Separator();
    ImGui::Checkbox("Email notifications", &g.notif_email);
    ImGui::Checkbox("Push notifications",  &g.notif_push);
    ImGui::Spacing();
    ImGui::TextUnformatted("Appearance");
    ImGui::Separator();
    ImGui::Checkbox("Dark mode", &g.dark_mode);
    ImGui::ColorEdit3("Accent", g.accent);
    ImGui::SliderFloat("Brightness", &g.bright, 0.2f, 1.2f, "%.2f");
    ImGui::SliderFloat("Volume", &g.vol, 0.f, 1.f, "%.2f");
    ImGui::Spacing();
    ImGui::TextUnformatted("General");
    ImGui::Separator();
    const char* langs[] = {"English", "Bahasa Indonesia", "Chinese", "German"};
    ImGui::Combo("Language", &g.lang_idx, langs, 4);
    ImGui::Checkbox("Autosave", &g.autosave);
    ImGui::Checkbox("Send anonymous telemetry", &g.telemetry);
    ImGui::Spacing();
    ImGui::PopItemWidth();
    if (ImGui::Button("Reset to defaults", {150, 30})) {
        g.notif_email = true; g.notif_push = false; g.dark_mode = true;
        g.vol = 0.7f; g.bright = 0.85f; g.accent[0] = 0.231f;
        g.accent[1] = 0.510f; g.accent[2] = 0.965f;
        g.lang_idx = 0; g.autosave = true; g.telemetry = false;
    }
    CardEnd();
}

// ── Spreadsheet ──────────────────────────────────────────────────────────────
static char  gSheet[12][6][48];
static bool  gSheetInit = false;

static float SheetColW(int c) { return c == 0 ? 44.f : 120.f; }

static void TabSpreadsheet()
{
    if (!gSheetInit) {
        for (int r = 0; r < 12; r++)
            for (int c = 0; c < 6; c++)
                snprintf(gSheet[r][c], sizeof(gSheet[r][c]), "%s", "");
        const char* seed[4][4] = {
            {"Region",  "Q1",    "Q2",    "Q3"},
            {"Jakarta", "1200",  "1450",  "1610"},
            {"Bandung", "820",   "910",   "1020"},
            {"Surabaya","640",   "700",   "780"},
        };
        for (int r = 0; r < 4; r++)
            for (int c = 0; c < 4; c++)
                snprintf(gSheet[r][c], sizeof(gSheet[r][c]), "%s", seed[r][c]);
        gSheetInit = true;
    }

    const char* headers[6] = {"", "A", "B", "C", "D", "E"};
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {6, 4});
    if (ImGui::BeginTable("##sheet", 6,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY,
            {0.f, 320.f})) {
        for (int c = 0; c < 6; c++)
            ImGui::TableSetupColumn(headers[c], ImGuiTableColumnFlags_WidthFixed, SheetColW(c));
        ImGui::TableHeadersRow();
        for (int r = 0; r < 12; r++) {
            ImGui::TableNextRow();
            for (int c = 0; c < 6; c++) {
                ImGui::TableSetColumnIndex(c);
                ImGui::PushID(r * 16 + c);
                if (c == 0) {
                    char rn[8]; snprintf(rn, sizeof(rn), "%d", r + 1);
                    ImGui::PushStyleColor(ImGuiCol_Text, Col::muted);
                    ImGui::TextUnformatted(rn);
                    ImGui::PopStyleColor();
                } else {
                    ImGui::SetNextItemWidth(-1);
                    ImGui::InputText("##cell", gSheet[r][c], sizeof(gSheet[r][c]));
                }
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }
    ImGui::PopStyleVar();

    ImGui::Spacing();
    // live sum of column B..E for numeric cells
    double sum = 0.0;
    for (int r = 1; r < 12; r++)
        for (int c = 1; c < 5; c++) {
            char* end = nullptr;
            double v = strtod(gSheet[r][c], &end);
            if (end && end != gSheet[r][c]) sum += v;
        }
    ImGui::Text("Sum of numeric cells: %.0f", sum);
    ImGui::SameLine(0, 16);
    if (ImGui::Button("Clear sheet", {110, 26})) {
        for (int r = 0; r < 12; r++)
            for (int c = 0; c < 6; c++) gSheet[r][c][0] = 0;
    }
}

static void TabFeed()
{
    float W = ImGui::GetContentRegionAvail().x;
    if (ImGui::Button("Refresh feed", {120, 28})) start_rss_fetch();
    ImGui::SameLine();
    if (gFeed.loading) {
        ImGui::PushStyleColor(ImGuiCol_Text, Col::muted);
        ImGui::TextUnformatted("loading...");
        ImGui::PopStyleColor();
    } else if (gFeed.error) {
        ImGui::PushStyleColor(ImGuiCol_Text, Col::danger);
        ImGui::TextUnformatted(gFeed.error_msg);
        ImGui::PopStyleColor();
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, Col::muted);
        ImGui::Text("%d items", (int)gFeed.items.size());
        ImGui::PopStyleColor();
    }
    ImGui::Separator();

    ImGui::BeginChild("##feedlist", {W, 0}, ImGuiChildFlags_None);
    for (size_t i = 0; i < gFeed.items.size(); i++) {
        const RssItem& it = gFeed.items[i];
        ImGui::PushID((int)i);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, Col::surface);
        ImGui::BeginChild("##item", {0, 104.f}, ImGuiChildFlags_None);
        ImGui::PushStyleColor(ImGuiCol_Text, Col::text);
        ImGui::TextWrapped("%s", it.title.c_str());
        ImGui::PopStyleColor();
        if (!it.date.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, Col::muted);
            ImGui::TextUnformatted(it.date.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::PushStyleColor(ImGuiCol_Text, Col::muted);
        ImGui::TextWrapped("%s", it.desc.c_str());
        ImGui::PopStyleColor();
        if (!it.link.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("open")) EM_ASM({ window.open(UTF8ToString($0), '_blank'); }, it.link.c_str());
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
    if (gFeed.items.empty() && !gFeed.loading && !gFeed.error) {
        ImGui::PushStyleColor(ImGuiCol_Text, Col::muted);
        ImGui::TextUnformatted("No items yet — press Refresh feed.");
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
}

// ── PDF editor ───────────────────────────────────────────────────────────────
static void TabPdfEditor()
{
    float W = ImGui::GetContentRegionAvail().x;
    float H = ImGui::GetContentRegionAvail().y;

    // The reference paper ships with the module: open it on first draw so the
    // tab is never empty.
    if (gPdf.sel_a >= 0 && gPdf.sel_page == gPdf.cur_page && ImGui::GetIO().KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_C, false))
        PdfCopySelection();

    if (!gPdf.tried_autoload && !gPdf.doc.loaded) {
        gPdf.tried_autoload = true;
        PdfLoadFileNative("/compressed.tracemonkey-pldi-09.pdf");
    }

    // toolbar
    ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.10f, 0.14f, 0.22f, 1.f});
    ImGui::BeginChild("##pdftb", {W, 40.f}, ImGuiChildFlags_None);
    ImGui::SetCursorPosY(6.f);
    if (ImGui::Button("Open PDF", {90, 26})) pdf_open_picker();
    ImGui::SameLine(0, 10);
    const char* tools[] = {"Select", "Text", "Rect", "Highlight", "Draw"};
    for (int t = 0; t < 5; t++) {
        bool on = (gPdf.tool == (PdfState::Tool)t);
        ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4{0.231f,0.510f,0.965f,1.f} : Col::surface);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.376f,0.647f,0.98f,1.f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.149f,0.392f,0.922f,1.f});
        if (ImGui::Button(tools[t], {72, 26})) { gPdf.tool = (PdfState::Tool)t; gPdf.placing = false; }
        ImGui::PopStyleColor(3);
        ImGui::SameLine(0, 4);
    }
    ImGui::SameLine(0, 12);
    ImGui::SetNextItemWidth(120.f);
    ImGui::SliderFloat("##zoom", &gPdf.zoom, 0.5f, 3.f, "zoom %.1fx");
    ImGui::SameLine(0, 12);
    if (gPdf.page_count > 0) {
        if (ImGui::Button("<", {26, 26}) && gPdf.cur_page > 0) {
            gPdf.cur_page--;
            gPdf.sel_a = gPdf.sel_b = -1; gPdf.sel_dragging = false;
            if (!gPdf.pages[gPdf.cur_page].id) PdfRenderPageNative(gPdf.cur_page, kPdfRenderScale);
        }
        ImGui::SameLine();
        ImGui::Text("%d / %d", gPdf.cur_page + 1, gPdf.page_count);
        ImGui::SameLine();
        if (ImGui::Button(">", {26, 26}) && gPdf.cur_page + 1 < gPdf.page_count) {
            gPdf.cur_page++;
            gPdf.sel_a = gPdf.sel_b = -1; gPdf.sel_dragging = false;
            if (!gPdf.pages[gPdf.cur_page].id) PdfRenderPageNative(gPdf.cur_page, kPdfRenderScale);
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, Col::muted);
        ImGui::TextUnformatted("no document");
        ImGui::PopStyleColor();
    }
    ImGui::SameLine(0, 16);
    if (gPdf.sel_a >= 0 && gPdf.sel_page == gPdf.cur_page) {
        if (ImGui::Button("Copy text", {86, 26})) PdfCopySelection();
    } else {
        ImGui::BeginDisabled();
        ImGui::Button("Copy text", {86, 26});
        ImGui::EndDisabled();
    }
    ImGui::SameLine(0, 16);
    ImGui::Text("%d annotations", (int)gPdf.annots.size());
    if (!gPdf.status.empty()) {
        ImGui::SameLine(0, 16);
        ImGui::PushStyleColor(ImGuiCol_Text, Col::muted);
        ImGui::TextUnformatted(gPdf.status.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // canvas
    float chH = std::max(120.f, H - 50.f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.13f, 0.14f, 0.17f, 1.f});
    ImGui::BeginChild("##pdfcanvas", {W, chH}, ImGuiChildFlags_None);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 win = ImGui::GetWindowPos();
    float scrollY = ImGui::GetScrollY();

    float pageW = 0.f, pageH = 0.f;
    ImTextureID tex = 0;
    if (gPdf.page_count > 0 && gPdf.cur_page < (int)gPdf.pages.size() &&
        gPdf.pages[gPdf.cur_page].id) {
        pageW = gPdf.pages[gPdf.cur_page].w * gPdf.zoom;
        pageH = gPdf.pages[gPdf.cur_page].h * gPdf.zoom;
        tex   = (ImTextureID)(uintptr_t)gPdf.pages[gPdf.cur_page].id;
    }
    if (tex) {
        float px = win.x + (W - pageW) * 0.5f;
        float py = win.y + 16.f - scrollY;
        ImGui::Dummy({1.f, pageH + 32.f});
        dl->AddRectFilled({px + 3, py + 3}, {px + pageW + 3, py + pageH + 3}, IM_COL32(0,0,0,60));
        dl->AddImage(tex, {px, py}, {px + pageW, py + pageH});

        // ── selected text layer ────────────────────────────────────────────
        const std::vector<pdf::TextItem>* curItems = nullptr;
        if (gPdf.sel_page == gPdf.cur_page && gPdf.cur_page < (int)gPdf.page_text.size())
            curItems = &gPdf.page_text[gPdf.cur_page];
        if (curItems && gPdf.sel_a >= 0 && gPdf.sel_b >= 0 && !curItems->empty()) {
            int a = std::min(gPdf.sel_a, gPdf.sel_b), b = std::max(gPdf.sel_a, gPdf.sel_b);
            a = std::max(0, a); b = std::min((int)curItems->size() - 1, b);
            const ImU32 selCol = IM_COL32(64, 132, 255, 90);
            for (int i = a; i <= b; i++) {
                const auto& it = (*curItems)[i];
                if (it.text.empty()) continue;
                float sx0 = px + it.x0 * PdfPxPerPt() * gPdf.zoom;
                float sx1 = px + it.x1 * PdfPxPerPt() * gPdf.zoom;
                float sy0 = py + (gPdf.page_h_pt - it.y1) * PdfPxPerPt() * gPdf.zoom;
                float sy1 = py + (gPdf.page_h_pt - it.y0) * PdfPxPerPt() * gPdf.zoom;
                dl->AddRectFilled({sx0 - 1.f, sy0 - 1.f}, {sx1 + 1.f, sy1 + 1.f}, selCol);
            }
        }

        ImGui::SetCursorPos({(W - pageW) * 0.5f, 16.f});
        ImGui::InvisibleButton("##pdfsurface", {pageW, pageH});
        ImVec2 mp = ImGui::GetIO().MousePos;
        bool hovered = ImGui::IsItemHovered();

        // annotations of this page
        for (size_t i = 0; i < gPdf.annots.size(); i++) {
            PdfState::Annot& a = gPdf.annots[i];
            if (a.page != gPdf.cur_page) continue;
            ImVec2 p0{px + a.x * gPdf.zoom, py + a.y * gPdf.zoom};
            ImVec2 p1{px + (a.x + a.w) * gPdf.zoom, py + (a.y + a.h) * gPdf.zoom};
            if (a.kind == 0) {                     // text
                dl->AddText({p0.x, p0.y}, ToU32(Col::danger), a.text.c_str());
            } else if (a.kind == 1) {              // rect
                dl->AddRect(p0, p1, a.color, 0.f, 0, 2.f);
            } else if (a.kind == 2) {              // highlight
                dl->AddRectFilled(p0, p1, a.color);
            } else if (a.kind == 3) {              // freehand
                for (size_t k = 1; k < a.stroke.size(); k++)
                    dl->AddLine({px + a.stroke[k-1].first * gPdf.zoom, py + a.stroke[k-1].second * gPdf.zoom},
                                {px + a.stroke[k].first * gPdf.zoom,   py + a.stroke[k].second * gPdf.zoom},
                                a.color, 2.f);
            }
            if ((int)i == gPdf.sel_annot)
                dl->AddRect({p0.x - 2, p0.y - 2}, {p1.x + 2, p1.y + 2}, IM_COL32(80,140,255,200));
        }

        if (hovered && ImGui::IsMouseClicked(0)) {
            float lx = (mp.x - px) / gPdf.zoom;
            float ly = (mp.y - py) / gPdf.zoom;
            if (gPdf.tool == PdfState::Select) {
                gPdf.sel_annot = -1;
                // text first: selecting words is what this tool is for
                int hit = -1;
                if (gPdf.cur_page < (int)gPdf.page_text.size()) {
                    const auto& items = gPdf.page_text[gPdf.cur_page];
                    float pptx = lx / PdfPxPerPt();
                    float ppty = gPdf.page_h_pt - ly / PdfPxPerPt();
                    hit = PdfTextHit(items, pptx, ppty);
                }
                if (hit >= 0) {
                    gPdf.sel_page = gPdf.cur_page;
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        // whole word: extend while items stay on one line with no gap
                        const auto& items = gPdf.page_text[gPdf.cur_page];
                        int a = hit, b = hit;
                        auto lineOf = [&](int i) { return items[i].y0; };
                        while (a > 0 && std::fabs(lineOf(a - 1) - lineOf(hit)) < 2.f &&
                               items[a].x0 - items[a - 1].x1 < 3.5f) a--;
                        while (b + 1 < (int)items.size() && std::fabs(lineOf(b + 1) - lineOf(hit)) < 2.f &&
                               items[b + 1].x0 - items[b].x1 < 3.5f) b++;
                        gPdf.sel_a = a; gPdf.sel_b = b;
                    } else {
                        gPdf.sel_a = gPdf.sel_b = hit;
                        gPdf.sel_dragging = true;
                    }
                } else {
                    gPdf.sel_a = gPdf.sel_b = -1;
                    gPdf.sel_dragging = false;
                    for (int i = (int)gPdf.annots.size() - 1; i >= 0; i--) {
                        PdfState::Annot& a = gPdf.annots[i];
                        if (a.page != gPdf.cur_page) continue;
                        if (lx >= a.x && lx <= a.x + a.w && ly >= a.y && ly <= a.y + a.h) {
                            gPdf.sel_annot = i; break;
                        }
                    }
                }
            } else if (gPdf.tool == PdfState::TextTool) {
                gPdf.placing = true;
                gPdf.drag_start = {lx, ly};
            } else {
                gPdf.drawing = true;
                gPdf.drag_start = {lx, ly};
                PdfState::Annot a;
                a.page = gPdf.cur_page;
                a.x = lx; a.y = ly; a.w = 0; a.h = 0;
                a.kind = (gPdf.tool == PdfState::RectTool) ? 1
                       : (gPdf.tool == PdfState::HighlightTool) ? 2 : 3;
                if (a.kind == 3) {
                    a.color = IM_COL32(240, 60, 60, 220);
                    a.stroke.push_back({lx, ly});
                } else if (a.kind == 2) {
                    a.color = IM_COL32(255, 220, 0, 140);
                } else {
                    a.color = IM_COL32(240, 60, 60, 220);
                }
                gPdf.annots.push_back(a);
                gPdf.sel_annot = (int)gPdf.annots.size() - 1;
            }
        }
        if (gPdf.drawing && ImGui::IsMouseDown(0) && gPdf.sel_annot >= 0 &&
            gPdf.sel_annot < (int)gPdf.annots.size()) {
            float lx = (mp.x - px) / gPdf.zoom;
            float ly = (mp.y - py) / gPdf.zoom;
            PdfState::Annot& a = gPdf.annots[gPdf.sel_annot];
            if (a.kind == 3) a.stroke.push_back({lx, ly});
            else {
                a.x = std::min(a.x, lx);
                a.y = std::min(a.y, ly);
                a.w = fabsf(lx - gPdf.drag_start.x);
                a.h = fabsf(ly - gPdf.drag_start.y);
            }
        }
        if (gPdf.sel_dragging && ImGui::IsMouseDown(0) && gPdf.sel_page == gPdf.cur_page &&
            gPdf.cur_page < (int)gPdf.page_text.size()) {
            const auto& items = gPdf.page_text[gPdf.cur_page];
            float lx = (mp.x - px) / gPdf.zoom / PdfPxPerPt();
            float ly = (mp.y - py) / gPdf.zoom / PdfPxPerPt();
            int hit = PdfTextHit(items, lx, gPdf.page_h_pt - ly);
            if (hit >= 0) gPdf.sel_b = hit;
        }
        if (!ImGui::IsMouseDown(0)) { gPdf.drawing = false; gPdf.sel_dragging = false; }
        if (hovered) ImGui::SetMouseCursor(gPdf.tool == PdfState::Select
                                            ? ImGuiMouseCursor_Arrow : ImGuiMouseCursor_Hand);
    } else {
        ImGui::Dummy({1.f, chH - 40.f});
        ImGui::PushStyleColor(ImGuiCol_Text, Col::muted);
        ImGui::SetCursorPos({W * 0.5f - 140.f, chH * 0.4f});
        ImGui::TextUnformatted("Open a PDF to annotate it (rendered natively in C++).");
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    if (gPdf.placing) {
        ImGui::Spacing();
        ImGui::SetNextItemWidth(320.f);
        ImGui::InputText("Annotation text", gPdf.text_buf, sizeof(gPdf.text_buf));
        ImGui::SameLine();
        if (ImGui::Button("Add", {60, 24})) {
            PdfState::Annot a;
            a.kind = 0; a.page = gPdf.cur_page;
            a.x = gPdf.drag_start.x; a.y = gPdf.drag_start.y;
            a.w = 200.f; a.h = 14.f;
            a.text = gPdf.text_buf;
            gPdf.annots.push_back(a);
            gPdf.text_buf[0] = 0;
            gPdf.placing = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {70, 24})) gPdf.placing = false;
    }
    if (gPdf.sel_annot >= 0 && gPdf.sel_annot < (int)gPdf.annots.size()) {
        ImGui::SameLine();
        if (DangerButton("Delete annotation", {150, 24})) {
            gPdf.annots.erase(gPdf.annots.begin() + gPdf.sel_annot);
            gPdf.sel_annot = -1;
        }
    }
}

// ─── Modals ───────────────────────────────────────────────────────────────────
static void DrawModals()
{
    if (g.open_deploy_modal) {
        ImGui::OpenPopup("Deploy##modal");
        g.open_deploy_modal = false;
    }
    if (g.open_delete_modal) {
        ImGui::OpenPopup("Delete##modal");
        g.open_delete_modal = false;
    }
    if (g.open_saved_modal) {
        ImGui::OpenPopup("Saved##modal");
        g.open_saved_modal = false;
    }

    ImGui::SetNextWindowSize({380, 190}, ImGuiCond_Always);
    if (ImGui::BeginPopupModal("Deploy##modal", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextUnformatted("Deploy to which environment?");
        ImGui::Spacing();
        const char* envs[] = {"production", "staging", "development"};
        int cur = 0;
        for (int i = 0; i < 3; i++) if (strcmp(g.deploy_env, envs[i]) == 0) cur = i;
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##env", &cur, envs, 3))
            snprintf(g.deploy_env, sizeof(g.deploy_env), "%s", envs[cur]);
        ImGui::Spacing();
        if (SuccessButton("Deploy", {100, 30})) {
            g.progress = 1.f;
            g.status_msg = std::string("Deployed to ") + g.deploy_env;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {100, 30})) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize({400, 220}, ImGuiCond_Always);
    if (ImGui::BeginPopupModal("Delete##modal", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextUnformatted("Type DELETE to confirm.");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##del", g.delete_type_buf, sizeof(g.delete_type_buf));
        g.delete_confirmed = strcmp(g.delete_type_buf, "DELETE") == 0;
        ImGui::Spacing();
        if (!g.delete_confirmed) ImGui::BeginDisabled();
        if (DangerButton("Delete", {100, 30})) {
            g.progress = 0.f;
            g.status_msg = "Rolled back";
            g.delete_type_buf[0] = 0;
            g.delete_confirmed = false;
            ImGui::CloseCurrentPopup();
        }
        if (!g.delete_confirmed) ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {100, 30})) {
            g.delete_type_buf[0] = 0;
            g.delete_confirmed = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize({340, 140}, ImGuiCond_Always);
    if (ImGui::BeginPopupModal("Saved##modal", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextUnformatted("Profile saved.");
        ImGui::Spacing();
        if (PrimaryButton("OK", {80, 28})) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// ─── Docx/OOXML rich-text editor ──────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

enum DocStyle { DS_Normal=0, DS_H1, DS_H2, DS_H3, DS_Quote, DS_Code };
enum DocAlign       { DA_Left=0, DA_Center, DA_Right, DA_Justify };
enum DocImageLayout { IL_Inline=0, IL_Behind, IL_Front };
enum DocShapeType   { SH_Rect=0, SH_Oval, SH_Line, SH_Triangle };

struct DocShape {
    DocShapeType type  = SH_Rect;
    int   page  = 0;
    float x=0,y=0,w=120,h=60;
    ImVec4 fill   = {0.70f,0.85f,1.00f,0.70f};
    ImVec4 stroke = {0.20f,0.40f,0.85f,0.90f};
    float  thick  = 2.f;
};

// ─── Run-level character formatting ──────────────────────────────────────────
// Formatting belongs to *ranges of text*, not to whole paragraphs: selecting a
// word and pressing B must bold only that word, and typing right after it must
// continue in bold.  A paragraph therefore stores its text plus a list of runs,
// each covering `len` bytes with one attribute set.
struct DocAttr {
    bool    bold      = false;
    bool    italic    = false;
    bool    underline = false;
    bool    strike    = false;
    int     vert      = 0;                       // +1 super, -1 sub
    int     family    = 0;                       // index into kDocFamilyName[]
    bool    hasFamily = false;                   // run carried an explicit w:rFonts
    float   fsPt      = 0.f;                     // 0 = inherit the paragraph/heading size
    bool    hasColor  = false;
    ImVec4  color     = {0.f, 0.f, 0.f, 1.f};
    bool    hasHL     = false;
    ImVec4  hl        = {0.f, 0.f, 0.f, 0.f};

    bool operator==(const DocAttr& o) const {
        return bold == o.bold && italic == o.italic && underline == o.underline &&
               strike == o.strike && vert == o.vert &&
               family == o.family && fsPt == o.fsPt &&
               hasColor == o.hasColor && hasHL == o.hasHL &&
               (!hasColor || (color.x == o.color.x && color.y == o.color.y && color.z == o.color.z)) &&
               (!hasHL    || (hl.x == o.hl.x && hl.y == o.hl.y && hl.z == o.hl.z && hl.w == o.hl.w));
    }
    bool operator!=(const DocAttr& o) const { return !(*this == o); }
};

struct DocRun {
    int     len = 0;                             // bytes covered
    DocAttr a;
};

// A simple table: rows x cols of plain strings, laid out in the document flow.
// One side of a table border. Word stores each side separately and "none" means
// the line is absent -- a Word table is very often borderless, so a single
// borderW float cannot express it.
struct DocBorder {
    bool   on  = false;
    float  w   = 0.75f;                                  // pt
    ImVec4 col = {0.f, 0.f, 0.f, 1.f};
};

struct DocTable {
    int rows = 0, cols = 0;
    bool header = true;
    float borderW = 1.f;                                  // app-created tables: uniform grid
    ImVec4 borderCol = {0.59f, 0.63f, 0.71f, 1.f};
    ImVec4 headerFill = {0.93f, 0.94f, 0.97f, 1.f};
    ImVec4 bodyFill   = {1.f, 1.f, 1.f, 1.f};
    std::vector<std::string> cells;

    // Styling imported from w:tblPr / w:tcPr (empty = app defaults).
    std::vector<float> colW;                              // pt per column
    float indent = 0.f;                                   // pt (w:tblInd)
    bool  styled = false;                                 // came from a .docx
    DocBorder bTop, bBottom, bLeft, bRight, bInsideH, bInsideV;
    std::vector<ImVec4> cellFill;                         // rows*cols, w=0 -> none
    std::vector<int>    cellAlign;                        // rows*cols, -1 default

    std::string&       at(int r, int c)       { return cells[(size_t)r * cols + c]; }
    const std::string& at(int r, int c) const { return cells[(size_t)r * cols + c]; }
    float ColW(int c, float avail) const
    {
        if (colW.size() != (size_t)cols) return avail / (float)std::max(1, cols);
        float sum = 0.f;
        for (float w : colW) sum += w;
        if (sum <= 0.f) return avail / (float)std::max(1, cols);
        return colW[(size_t)c] * (avail / sum);           // fit to the text column
    }
};
static std::vector<DocTable> gDocTables;
static int  gDocTableEditPara = -1;
static int  gDocTableEditR    = -1;
static int  gDocTableEditC    = -1;
static int  gDocTableFocus    = 0;      // frames left to keep requesting focus
static bool gDocTableHeader   = true;
static inline float DocTableRowH() { return 30.f; }
static inline float DocTableHeight(const DocTable& t) { return (float)t.rows * DocTableRowH() + 2.f; }

struct DocPara {
    char     text[2048] = {};
    DocStyle style      = DS_Normal;
    DocAlign align      = DA_Left;
    // Legacy paragraph-level flags: they describe the paragraph's *default*
    // attribute (used for the demo text, headings, and for text that has no
    // explicit run yet).  `runs` overrides them per character range.
    bool     bold       = false;
    bool     italic     = false;
    bool     underline  = false;
    bool     strike     = false;
    int      vertAlign  = 0;      // 0 = baseline, +1 = superscript, -1 = subscript
    int      list       = 0;      // 0 = none, 1 = bulleted, 2 = numbered
    std::vector<DocRun> runs;     // covers [0, strlen(text)) exactly when non-empty
    int            imageIdx      = -1;
    int            tableIdx      = -1;     // >= 0 → this paragraph is a table
    char           caption[192]  = {};     // caption shown under images / tables
    int            divider       = 0;      // 1 = horizontal rule, 2 = dot leader
    int            indexEntry    = 0;      // 1 TOC / 2 list of figures / 3 list of tables
    float          imageWidthPct = 100.f;
    char           imgRel[64]     = {};    // r:embed id from the imported .docx
    float          imgWpt         = 0.f;   // requested size in points
    float          imgHpt         = 0.f;
    float          firstIndent    = 0.f;   // pt, first-line indent (negative = hanging)
    // Typography inherited from the file's styles (0 / -1 = use the editor's own
    // defaults). Without these the canvas drew every imported document at 15 pt
    // DejaVu, which is why the preview never matched Word.
    float          baseFs         = 0.f;   // pt
    int            baseFamily     = 0;     // index into kDocFamilyName[]
    int            section        = 0;     // index into the imported section table
    std::string    mathTex;               // display equation, rendered as an image
    DocImageLayout imageLayout   = IL_Inline;

    // Paragraph spacing.  Negative before/after = "use the style default";
    // lineSpacing 0 = the font's natural line height (LayoutParagraph 1.0x).
    float          spaceBefore   = -1.f;   // pt
    float          spaceAfter    = -1.f;   // pt
    float          lineSpacing   = 0.f;    // multiplier, 0 = default (1.0)

    ImVec4         textClr       = {0.f,0.f,0.f,1.f};
    bool           hasCustomTextColor = false;
    ImVec4         hlClr         = {0.f,0.f,0.f,0.f};
};

struct DocImage {
    unsigned int         texID=0;
    int                  srcW=0, srcH=0;
    std::vector<uint8_t> pixels;
};

struct DocPageSetup {
    bool  landscape = false;
    float ml = 62.f, mr = 62.f, mt = 72.f, mb = 72.f;
    // Distance from the paper edge to the header / footer block.  Office keeps
    // these independent of the top/bottom margin, so a bigger margin does not
    // drag the header down into the page.
    float hd = 36.f, fd = 36.f;
    bool  header  = true;              // draw the document header on this page
    bool  footer  = true;
    int   cols   = 1;                  // 1..3 newspaper columns
    float colGap = 16.f;               // gap between columns, points
};
static std::vector<DocPageSetup> gDocPageSetups;    // sparse: missing = default
static std::vector<float>        gDocPageTops;      // top of each page, document points
static DocPageSetup              gDocSetupDraft;    // pending edits in the page-setup panel
static bool                      gDocSetupLoaded = false;
static DocPageSetup              gDocDefaultSetup;  // document-wide fallback (Apply to all pages)
static bool                      gDocDefaultSetupSet = false;

// ── Tab stops ────────────────────────────────────────────────────────────────
// `x` is document points measured from the left margin (0 = margin edge), so a
// stop stays put when the page is resized.  `leader` fills the gap the tab
// leaves: 0 none, 1 dots (...), 2 dashes (---), 3 line (___).
struct DocTabStop { float x = 0.f; int leader = 0; };
static std::vector<DocTabStop> gDocTabs;
static int                     gDocTabNewLeader = 1;   // leader for stops added from the ruler
static const float             kDocDefTab = 36.f;      // Word's 0.5in default grid

struct DocSnapshot {
    std::vector<DocPara> paras;
    std::vector<DocPageSetup> setups;   // per-page layout must undo with the text
    std::vector<DocTabStop>   tabs;     // tab stops undo too
    DocPageSetup defSetup;              // document-wide fallback
    bool         defSetupSet = false;
    char header[1024]={};
    char footer[1024]={};
};

// ══════════════════════════════════════════════════════════════════════════════
// ─── Document model: positions, runs, editing operations ─────────────────────
// ══════════════════════════════════════════════════════════════════════════════

// A document position is (paragraph index, byte offset inside that paragraph).
struct DocPos { int para = 0; int off = 0; };
static inline bool DocPosLess(const DocPos& a, const DocPos& b)
{ return a.para < b.para || (a.para == b.para && a.off < b.off); }
static inline bool DocPosEq(const DocPos& a, const DocPos& b)
{ return a.para == b.para && a.off == b.off; }

static std::vector<DocPara>    gDocParas;
static int                     gDocSel       = -1;     // caret paragraph (toolbar/export)
static bool                    gDocFocus     = false;  // canvas owns the keyboard
static DocPos                  gDocCaret;
static DocPos                  gDocAnchor;             // selection anchor
static bool                    gDocSelActive = false;
static bool                    gDocMouseSel  = false;
static double                  gDocBlinkT    = 0.0;
static float                   gDocWantX     = -1.f;
static int                     gDocClickCnt  = 0;
static double                  gDocLastClickT = -1.0;
static DocPos                  gDocLastClickPos;
static std::vector<DocPara>    gDocClipParas;          // rich clipboard
static std::string             gDocClipText;
static DocAttr                 gDocTypingAttr;         // attribute for newly typed text
static bool                    gDocTypingAttrSet = false;
static std::string             gDocStatus;
static std::vector<DocImage>   gDocImages;
static char                    gDocHeader[1024] = "Document Header";
static char                    gDocFooter[1024] = "Page {page}";
static std::deque<DocSnapshot> gDocHistory;
static int                     gDocHistIdx  = -1;
static bool                    gDocHFOpen   = false;
static bool                    gDocDiagramOpen = false;
static int                     gDocDiagramKind = 0;    // 0 mermaid, 1 graphviz
static char                    gDocDiagramSrc[4096] = "flowchart TD\n  A[Customer] --> B{Order valid?}\n  B -->|yes| C[Ship it]\n  B -->|no| D[Cancel]\n";
static char                    gDocDiagramStatus[128] = "";
static bool                    gDocMathOpen = false;
static char                    gDocMathSrc[1024] = "E = mc^2";
static bool                    gDocTableOpen = false;
static int                     gDocTableRows = 3, gDocTableCols = 3;
static int                     gDocHFEdit   = 0;      // 1 = header, 2 = footer (in-place editor)
static int                     gDocHFFrames = 0;      // frames since the editor opened
static int                     gDocHFPage   = 0;
static int                     gDocHFFocus  = 0;      // frames left to keep requesting focus
static bool                    gDocTabPending = false;  // "?tab=docx" deep link

// ── attribute helpers ────────────────────────────────────────────────────────
static DocAttr DocBaseAttr(const DocPara& p)
{
    DocAttr a;
    a.family    = p.baseFamily;
    a.hasFamily = true;
    a.bold      = p.bold   || (p.style == DS_H1 || p.style == DS_H2 || p.style == DS_H3);
    a.italic    = p.italic || p.style == DS_Quote;
    a.underline = p.underline;
    a.strike    = p.strike;
    a.vert      = p.vertAlign;
    a.hasColor  = p.hasCustomTextColor;
    a.color     = p.textClr;
    a.hasHL     = (p.hlClr.x > 0.01f || p.hlClr.y > 0.01f || p.hlClr.z > 0.01f);
    a.hl        = p.hlClr;
    return a;
}

// Keep `runs` contiguous and exactly as long as the text.
static void DocRunsNorm(std::vector<DocRun>& r, int len, const DocAttr& dflt)
{
    std::vector<DocRun> out;
    int acc = 0;
    for (size_t i = 0; i < r.size(); ++i) {
        const DocRun& x = r[i];
        if (x.len <= 0 || acc >= len) continue;
        DocRun y = x;
        if (acc + y.len > len) y.len = len - acc;
        if (y.len <= 0) continue;
        if (!out.empty() && out.back().a == y.a) out.back().len += y.len;
        else out.push_back(y);
        acc += y.len;
    }
    if (acc < len) {
        DocRun y; y.len = len - acc; y.a = dflt;
        if (!out.empty() && out.back().a == y.a) out.back().len += y.len;
        else out.push_back(y);
    }
    r.swap(out);
}

// Index of the run that starts at byte `off` (splitting a run if needed).
static int DocRunsSplit(std::vector<DocRun>& r, int off)
{
    int acc = 0;
    for (int i = 0; i < (int)r.size(); ++i) {
        if (acc == off) return i;
        if (off > acc && off < acc + r[i].len) {
            DocRun tail = r[i];
            tail.len = acc + r[i].len - off;
            r[i].len = off - acc;
            r.insert(r.begin() + i + 1, tail);
            return i + 1;
        }
        acc += r[i].len;
    }
    return (int)r.size();
}

static DocAttr DocAttrAt(const DocPara& p, int off)
{
    int len = (int)strlen(p.text);
    if (p.runs.empty()) return DocBaseAttr(p);
    off = std::max(0, std::min(off, len));
    int acc = 0;
    for (size_t i = 0; i < p.runs.size(); ++i) {
        if (off < acc + p.runs[i].len) return p.runs[i].a;
        acc += p.runs[i].len;
    }
    return p.runs.back().a;
}

// Assign `at` to bytes [a,b) of the paragraph.
static void DocSetAttrRange(DocPara& p, int a, int b, const DocAttr& at)
{
    int len = (int)strlen(p.text);
    a = std::max(0, std::min(a, len));
    b = std::max(0, std::min(b, len));
    if (b <= a) return;
    DocAttr base = DocBaseAttr(p);
    DocRunsNorm(p.runs, len, base);
    int ia = DocRunsSplit(p.runs, a);
    int ib = DocRunsSplit(p.runs, b);
    for (int i = ia; i < ib && i < (int)p.runs.size(); ++i) p.runs[i].a = at;
    DocRunsNorm(p.runs, len, base);
}

// True if every byte of [a,b) already has the property selected by `op`.
static bool DocRangeAllHas(const DocPara& p, int a, int b, int op, const ImVec4& v)
{
    int len = (int)strlen(p.text);
    a = std::max(0, std::min(a, len));
    b = std::max(0, std::min(b, len));
    if (b <= a) return false;
    int acc = a;
    while (acc < b) {
        DocAttr at = DocAttrAt(p, acc);
        bool has = false;
        switch (op) {
            case 0: has = at.bold; break;
            case 1: has = at.italic; break;
            case 2: has = at.underline; break;
            case 3: has = at.strike; break;
            case 4: has = (at.vert > 0); break;
            case 5: has = (at.vert < 0); break;
            case 6: has = at.hasColor && fabsf(at.color.x-v.x)<0.004f && fabsf(at.color.y-v.y)<0.004f && fabsf(at.color.z-v.z)<0.004f; break;
            case 7: has = at.hasHL && fabsf(at.hl.x-v.x)<0.004f && fabsf(at.hl.y-v.y)<0.004f && fabsf(at.hl.z-v.z)<0.004f; break;
            case 8: has = (at.family == (int)v.x); break;
            case 9: has = (fabsf(at.fsPt - v.x) < 0.01f); break;
        }
        if (!has) return false;
        // step to the end of the current run
        int step = 1;
        if (!p.runs.empty()) {
            int acc2 = 0;
            for (size_t i = 0; i < p.runs.size(); ++i) {
                if (acc < acc2 + p.runs[i].len) { step = acc2 + p.runs[i].len - acc; break; }
                acc2 += p.runs[i].len;
            }
        }
        acc += std::max(1, step);
    }
    return true;
}

static void DocAttrApplyOp(DocAttr& at, int op, bool on, const ImVec4& v)
{
    switch (op) {
        case 0: at.bold      = on; break;
        case 1: at.italic    = on; break;
        case 2: at.underline = on; break;
        case 3: at.strike    = on; break;
        case 4: at.vert      = on ? 1 : 0; break;
        case 5: at.vert      = on ? -1 : 0; break;
        case 6: at.hasColor  = on; at.color = ImVec4(v.x, v.y, v.z, 1.f); break;
        case 7: at.hasHL     = on; at.hl = ImVec4(v.x, v.y, v.z, v.w > 0.01f ? v.w : 0.5f); break;
        case 8: at.family    = (int)v.x; break;
        case 9: at.fsPt      = v.x; break;
    }
}


// ── page geometry & document styles ─────────────────────────────────────────
static std::vector<DocShape>   gDocShapes;
static int                     gDocSelShape   = -1;
static bool                    gDocShapeMode  = false;
static DocShapeType            gDocShapeType  = SH_Rect;
static bool                    gShapeDragging = false;
static float                   gShapeDragX0=0,gShapeDragY0=0;
static int                     gShapeDragPage =-1;

static const char* kStyleName[] = { "Normal","H1","H2","H3","Quote","Code" };

static bool  gDocLayoutDirty  = true;

static void DocPushSnap() {
    while ((int)gDocHistory.size() > gDocHistIdx+1) gDocHistory.pop_back();
    DocSnapshot s; s.paras=gDocParas; s.setups=gDocPageSetups; s.tabs=gDocTabs;
    s.defSetup = gDocDefaultSetup; s.defSetupSet = gDocDefaultSetupSet;
    memcpy(s.header,gDocHeader,sizeof(gDocHeader));
    memcpy(s.footer,gDocFooter,sizeof(gDocFooter));
    gDocHistory.push_back(std::move(s));
    if ((int)gDocHistory.size()>50) gDocHistory.pop_front(); else ++gDocHistIdx;
}
static bool DocUndo() {
    if (gDocHistIdx<=0) return false;
    --gDocHistIdx;
    gDocParas=gDocHistory[gDocHistIdx].paras;
    gDocPageSetups=gDocHistory[gDocHistIdx].setups;
    gDocTabs=gDocHistory[gDocHistIdx].tabs;
    gDocDefaultSetup=gDocHistory[gDocHistIdx].defSetup;
    gDocDefaultSetupSet=gDocHistory[gDocHistIdx].defSetupSet;
    memcpy(gDocHeader,gDocHistory[gDocHistIdx].header,sizeof(gDocHeader));
    memcpy(gDocFooter,gDocHistory[gDocHistIdx].footer,sizeof(gDocFooter));
    if (gDocSel>=(int)gDocParas.size()) gDocSel=(int)gDocParas.size()-1;
    gDocLayoutDirty = true;
    return true;
}
static bool DocRedo() {
    if (gDocHistIdx>=(int)gDocHistory.size()-1) return false;
    ++gDocHistIdx;
    gDocParas=gDocHistory[gDocHistIdx].paras;
    gDocPageSetups=gDocHistory[gDocHistIdx].setups;
    gDocTabs=gDocHistory[gDocHistIdx].tabs;
    gDocDefaultSetup=gDocHistory[gDocHistIdx].defSetup;
    gDocDefaultSetupSet=gDocHistory[gDocHistIdx].defSetupSet;
    memcpy(gDocHeader,gDocHistory[gDocHistIdx].header,sizeof(gDocHeader));
    memcpy(gDocFooter,gDocHistory[gDocHistIdx].footer,sizeof(gDocFooter));
    if (gDocSel>=(int)gDocParas.size()) gDocSel=(int)gDocParas.size()-1;
    gDocLayoutDirty = true;
    return true;
}

static const float kDocPageW  = 595.f;
static const float kDocPageH  = 842.f;
static       float kDocML     = 62.f;
static       float kDocMR     = 62.f;
static       float kDocMT     = 72.f;
static       float kDocMB     = 72.f;

// ─── Per-page layout ────────────────────────────────────────────────────────
// Word calls this a section break: one page can be landscape with its own
// margins and without a header/footer while the rest stays portrait.

static DocPageSetup DocDefaultPageSetup()
{
    if (gDocDefaultSetupSet) return gDocDefaultSetup;
    DocPageSetup s;
    s.ml = kDocML; s.mr = kDocMR; s.mt = kDocMT; s.mb = kDocMB;
    return s;
}
static DocPageSetup DocPageSetupAt(int pg)
{
    if (pg >= 0 && pg < (int)gDocPageSetups.size()) return gDocPageSetups[pg];
    // Pages past the last explicit entry inherit the last one, not the factory
    // default: otherwise the PDF export (which can paginate a page further than
    // the canvas) silently loses the margins the user applied.
    if (!gDocPageSetups.empty()) return gDocPageSetups.back();
    return DocDefaultPageSetup();
}
static void DocSetPageSetup(int pg, const DocPageSetup& s)
{
    if (pg < 0) return;
    if ((int)gDocPageSetups.size() <= pg)
        gDocPageSetups.resize((size_t)pg + 1, DocDefaultPageSetup());
    gDocPageSetups[(size_t)pg] = s;
}
static ImVec2 DocPageSizeOf(const DocPageSetup& s)
{
    return s.landscape ? ImVec2(842.f, 595.f) : ImVec2(595.f, 842.f);
}
static float DocPageTopPx(int pg, float docScale)
{
    if (pg >= 0 && pg < (int)gDocPageTops.size()) return gDocPageTops[(size_t)pg] * docScale;
    float acc = 0.f;
    for (int i = 0; i < pg; ++i) acc += DocPageSizeOf(DocPageSetupAt(i)).y + 20.f;   // 20pt page gap
    return acc * docScale;
}
static const float kDocGap    = 20.f;

// Screen-window-relative geometry of one sheet: every page can differ in size
// and margins, so nothing may assume the portrait constants.
static float DocPageCanvasTop(int pg, float docScale)      // y inside the canvas child
{ return kDocGap * docScale + DocPageTopPx(pg, docScale); }
static float DocPageLeftIn(float canvasW, int pg, float docScale)
{ return (canvasW - DocPageSizeOf(DocPageSetupAt(pg)).x * docScale) * 0.5f; }
static float DocColumnW(const DocPageSetup& s)
{
    int   n  = std::max(1, s.cols);
    float tw = DocPageSizeOf(s).x - s.ml - s.mr;
    return (tw - s.colGap * (n - 1)) / (float)n;
}
static float DocContentWAt(int pg, float docScale)
{
    return DocColumnW(DocPageSetupAt(pg)) * docScale;
}
static float DocMaxPageWPt(int numPages)
{
    float w = kDocPageW;
    for (int pg = 0; pg < std::max(1, numPages); ++pg)
        w = std::max(w, DocPageSizeOf(DocPageSetupAt(pg)).x);
    return w;
}

// ── tab stops ────────────────────────────────────────────────────────────────
static void DocTabsSort()
{
    std::sort(gDocTabs.begin(), gDocTabs.end(),
              [](const DocTabStop& a, const DocTabStop& b) { return a.x < b.x; });
}
static void DocTabsPx(std::vector<float>& out, float scale)
{
    out.clear();
    out.reserve(gDocTabs.size());
    for (const DocTabStop& t : gDocTabs) out.push_back(t.x * scale);
    std::sort(out.begin(), out.end());
}
// Split header/footer text into lines, substituting {page} with `pageNo`.
static void DocHFTextLines(const char* src, int pageNo, std::vector<std::string>& out)
{
    out.clear();
    if (!src) return;
    std::string cur;
    for (const char* p = src; *p; ++p) {
        if (*p == '\n') { out.push_back(cur); cur.clear(); continue; }
        if (p[0] == '{' && strncmp(p, "{page}", 6) == 0) {
            if (pageNo > 0) cur += std::to_string(pageNo);
            else            cur += "{page}";     // pageNo 0 = keep the placeholder
            p += 5;
            continue;
        }
        cur.push_back(*p);
    }
    out.push_back(cur);
}

// Leader style of the stop a rendered gap ends on (0 = no stop found).
static int DocLeaderForPx(float gapEndPx, float scale)
{
    if (scale <= 0.f) return 0;
    float xPt = gapEndPx / scale;
    for (const DocTabStop& t : gDocTabs)
        if (fabsf(t.x - xPt) < 1.5f) return t.leader;
    return 0;
}

// Paint the gap a '\t' opened: nothing, dots, dashes or a ruled line.
static void DocDrawTabLeaders(ImDrawList* dl, const dt::ParaLayout& pl, ImVec2 origin,
                              float scale, ImU32 col, int align = 0, float wrapW = 0.f)
{
    for (const dt::Line& ln : pl.lines) {
        const float off = dt::LineOffX(ln, align, wrapW);
        for (const dt::Piece& pc : ln.pieces) {
            if (!pc.tab || pc.w < 2.f) continue;
            int leader = DocLeaderForPx(pc.x + pc.w, scale);
            if (leader == 0) continue;
            float fs = 15.f;   // kDocBaseFS, kept literal: this helper is above it
            if (pc.span >= 0 && pc.span < (int)pl.spans.size()) fs = dt::SpanFs(pl.spans[pc.span]);
            float y0 = origin.y + ln.y + fs * 0.80f;
            float x0 = origin.x + off + pc.x + 2.f;
            float x1 = origin.x + off + pc.x + pc.w - 2.f;
            if (x1 <= x0) continue;
            float th = std::max(1.f, fs * 0.07f);
            if (leader == 3) {
                dl->AddLine({x0, y0}, {x1, y0}, col, th);
            } else if (leader == 1) {
                float step = std::max(3.5f, fs * 0.34f);
                float r    = std::max(0.9f, fs * 0.055f);
                for (float x = x0; x <= x1; x += step)
                    dl->AddCircleFilled({x, y0}, r, col);
            } else {
                float step = std::max(7.f, fs * 0.70f);
                float dash = step * 0.55f;
                for (float x = x0; x <= x1; x += step)
                    dl->AddLine({x, y0}, {std::min(x + dash, x1), y0}, col, th);
            }
        }
    }
}

static float DocFontScale(DocStyle s) {
    if (s==DS_H1) return 2.0f;
    if (s==DS_H2) return 1.55f;
    if (s==DS_H3) return 1.25f;
    return 1.0f;
}
static float DocSpaceBefore(DocStyle s, float baseFS) {
    if (s==DS_H1) return baseFS*2.0f;
    if (s==DS_H2) return baseFS*1.2f;
    if (s==DS_H3) return baseFS*0.8f;
    return baseFS*0.35f;
}

struct DocParaLayout { int page; float yStart, yEnd; float indent = 0.f; int col = 0; float xOff = 0.f; };
static std::vector<DocParaLayout>  gDocLayout;
static std::vector<dt::ParaLayout> gDocLines;     // visual lines per paragraph (layout cache)
// Family table (names, Word aliases, PDF class) lives in fonts_data.h, generated
// by tools/fonts/build_fonts.py together with the subset TTFs.
static dt::FontSet                 gDocFonts[kDocFamilyCount];

// Name -> family: Word files name faces we may not embed (Times New Roman,
// Calibri, ...), so match the metric-compatible replacement first and then fall
// back on the shape of the name.
static int DocFamilyFromName(const std::string& raw)
{
    std::string n; n.reserve(raw.size());
    for (char c : raw) n.push_back((char)tolower((unsigned char)c));
    for (int i = 0; i < kDocFamilyCount; ++i) {
        std::string dn = kDocFamilyName[i];
        for (char& c : dn) c = (char)tolower((unsigned char)c);
        if (n == dn) return i;
        if (kDocFamilyAlias[i][0]) {
            std::string an = kDocFamilyAlias[i];
            for (char& c : an) c = (char)tolower((unsigned char)c);
            if (n == an) return i;
        }
    }
    if (n.find("courier") != std::string::npos || n.find("mono") != std::string::npos ||
        n.find("consol")  != std::string::npos || n.find("code")  != std::string::npos)
        return 2;
    if (n.find("times") != std::string::npos || n.find("serif") != std::string::npos ||
        n.find("cambria")!= std::string::npos || n.find("georgia")!= std::string::npos ||
        n.find("garamond")!=std::string::npos || n.find("book")  != std::string::npos ||
        n.find("roman")  != std::string::npos)
        return 1;
    return 0;
}

static int DocFamilyClamp(int fam)
{
    return (fam >= 0 && fam < kDocFamilyCount) ? fam : 0;
}

// The .docx writer prefers the Word name of a metric-compatible face ("Times New
// Roman" for Liberation Serif) so exchanged files keep their layout in Word.
static const char* DocxFontName(int fam)
{
    fam = DocFamilyClamp(fam);
    return kDocFamilyAlias[fam][0] ? kDocFamilyAlias[fam] : kDocFamilyName[fam];
}
static std::vector<int>            gDocListNum;   // 1-based number for each numbered paragraph
static const float                 kDocListIndentPx = 26.f;
static const float                 kDocBaseFS = 15.f;
static float gDocPrevDocScale = 0.f;
static float gDocZoom = 1.f;            // View tab: 1.0 = fit page width

// ── color / span resolution for the layout engine ────────────────────────────
static ImU32 DocSpanColor(const DocPara& p, const DocAttr& a)
{
    if (a.hasColor)
        return ImGui::ColorConvertFloat4ToU32(ImVec4(a.color.x, a.color.y, a.color.z, 1.f));
    ImVec4 c = {0.07f, 0.07f, 0.12f, 1.f};
    if (p.style == DS_Quote) c = {0.30f, 0.32f, 0.44f, 1.f};
    else if (p.style == DS_Code) c = {0.08f, 0.42f, 0.22f, 1.f};
    return ImGui::ColorConvertFloat4ToU32(c);
}

static std::vector<dt::StyleSpan> DocParaSpans(const DocPara& p)
{
    std::vector<dt::StyleSpan> out;
    const DocAttr base = DocBaseAttr(p);
    const int len = (int)strlen(p.text);
    const float baseFs = (p.baseFs > 0.f) ? p.baseFs : kDocBaseFS * DocFontScale(p.style);

    auto push = [&](int b0, int b1, const DocAttr& a) {
        if (b1 <= b0) return;
        dt::StyleSpan s;
        s.b0 = b0; s.b1 = b1;
        int fam = DocFamilyClamp(a.family);
        s.st.fs = (a.fsPt > 0.f) ? a.fsPt : baseFs;
        s.st.font = gDocFonts[fam].Pick(a.bold, a.italic);
        if (!s.st.font) s.st.font = ImGui::GetFont();
        s.st.underline = a.underline;
        s.st.strike    = a.strike;
        s.st.vert      = a.vert;
        s.color = DocSpanColor(p, a);
        s.hasHL = a.hasHL;
        s.hl    = a.hl;
        out.push_back(s);
    };

    if (len <= 0) return out;
    if (p.runs.empty()) {
        push(0, len, base);
        return out;
    }
    int acc = 0;
    for (size_t i = 0; i < p.runs.size() && acc < len; ++i) {
        int b0 = acc;
        int b1 = std::min(len, acc + p.runs[i].len);
        push(b0, b1, p.runs[i].a);
        acc += p.runs[i].len;
    }
    if (acc < len) push(acc, len, base);
    return out;
}



// Word sections: a paragraph's w:pPr/w:sectPr closes the section it belongs to,
// so the sequence of sectPr elements defines the section list.
struct DocxSection { int cols = 1; float gap = 16.f; bool continuous = false; };
static std::vector<DocxSection> gDocxSections;
static DocxSection gDocxSectionCur;


// Apply an imported Word section's column settings to a page.
static void DocApplySection(int page, int section)
{
    if (section < 0 || section >= (int)gDocxSections.size()) return;
    const DocxSection& sec = gDocxSections[(size_t)section];
    if ((int)gDocPageSetups.size() <= page)
        gDocPageSetups.resize((size_t)page + 1, DocDefaultPageSetup());
    DocPageSetup st = gDocPageSetups[(size_t)page];
    st.cols   = std::max(1, sec.cols);
    st.colGap = sec.gap > 0.f ? sec.gap : 16.f;
    gDocPageSetups[(size_t)page] = st;
}

static void ComputeDocLayout(float docScale)
{
    gDocLayout.clear();
    gDocLayout.resize(gDocParas.size());
    gDocLines.clear();
    gDocLines.resize(gDocParas.size());
    gDocListNum.assign(gDocParas.size(), 0);
    gDocLayoutDirty  = false;
    gDocPrevDocScale = docScale;
    int   curPage = 0;
    int   listRun = 0;
    auto  widthOf = [&](int pg) { return DocColumnW(DocPageSetupAt(pg)) * docScale; };
    auto  topOf   = [&](int pg) { return DocPageSetupAt(pg).mt * docScale; };
    auto  botOf   = [&](int pg) {
        DocPageSetup st = DocPageSetupAt(pg);
        return (DocPageSizeOf(st).y - st.mb) * docScale;
    };
    int   curCol  = 0;
    int   curCols = std::max(1, DocPageSetupAt(0).cols);
    float curGap  = DocPageSetupAt(0).colGap;
    float cW      = widthOf(0);
    float curY    = topOf(0);
    float secTop  = curY;              // top of the current section's column band
    auto  xOffOf  = [&](int col) { return (cW + curGap) * (float)col * docScale; };
    auto  advance = [&]() {
        // Columns of one section start at the same y (the section's band top);
        // a page break re-anchors that band to the new page's top margin.
        if (curCol + 1 < curCols) { curCol++; }
        else { curPage++; curCol = 0; secTop = topOf(curPage); }
        curY = secTop;
    };
    std::vector<float> tabPx;
    DocTabsPx(tabPx, docScale);
    int curSec = -1;
    for (int pi=0; pi<(int)gDocParas.size(); pi++) {
        const DocPara& p = gDocParas[pi];
        DocParaLayout& lo = gDocLayout[pi];

        // A section carries its own column count and type. "continuous" keeps the
        // new section on the same page (abstract above a two-column body); any
        // other type starts a page. This is the same rule the PDF writer uses.
        if (p.section != curSec) {
            DocxSection sd;
            const bool haveSec = (p.section >= 0 && p.section < (int)gDocxSections.size());
            if (haveSec) sd = gDocxSections[(size_t)p.section];
            const bool cont = haveSec && sd.continuous && curSec >= 0;
            if (curSec >= 0 && !cont) {
                curPage++; curCol = 0;
                if ((int)gDocPageTops.size() <= curPage) gDocPageTops.resize((size_t)curPage + 1, 0.f);
                secTop = topOf(curPage);
            }
            curSec = p.section;
            DocPageSetup st = DocPageSetupAt(curPage);
            const float tw = DocPageSizeOf(st).x - st.ml - st.mr;
            if (haveSec) { curCols = std::max(1, sd.cols); curGap = sd.gap > 0.f ? sd.gap : 16.f; }
            else         { curCols = std::max(1, st.cols); curGap = st.colGap; }
            cW = (tw - curGap * (float)(curCols - 1)) / (float)curCols;
            if (cont) { curCol = 0; secTop = curY; }
            curY = secTop;
            DocApplySection(curPage, curSec);
        }
        float sb = (p.spaceBefore >= 0.f ? p.spaceBefore
                                         : DocSpaceBefore(p.style, p.baseFs > 0.f ? p.baseFs : kDocBaseFS))
                   * docScale;
        curY += sb;
        if (curY >= botOf(curPage)) advance();
        lo.page=curPage; lo.yStart=curY;
        lo.col = curCol;
        lo.xOff = xOffOf(curCol);
        lo.indent = p.list ? kDocListIndentPx * docScale : 0.f;
        if (p.list == 2) listRun++; else listRun = 0;
        gDocListNum[pi] = (p.list == 1) ? 0 : listRun;
        float capH = 0.f;
        {
            bool obj = (p.tableIdx >= 0 && p.tableIdx < (int)gDocTables.size()) ||
                       (p.imageIdx >= 0 && p.imageIdx < (int)gDocImages.size());
            if (obj && p.caption[0]) capH = kDocBaseFS * 0.85f * 1.9f + 3.f;
        }
        float paraH;
        if (p.divider) {
            paraH = kDocBaseFS * 1.45f;
        } else if (p.tableIdx >= 0 && p.tableIdx < (int)gDocTables.size()) {
            paraH = DocTableHeight(gDocTables[p.tableIdx]) + capH;
        } else if (p.imageIdx>=0 && p.imageIdx<(int)gDocImages.size()) {
            if (p.imageLayout != IL_Inline) {
                paraH = 0.f;
            } else {
                const DocImage& img = gDocImages[p.imageIdx];
                float dW = std::min(cW*(p.imageWidthPct/100.f),(float)img.srcW*docScale);
                float dH = img.srcH>0 ? dW*(float)img.srcH/(float)img.srcW : dW*0.5f;
                paraH = dH+8.f+capH;
            }
        } else {
            // Real line layout: wraps exactly like the renderer draws it, so
            // caret/hit-testing and page breaks agree with the pixels.
            dt::ParaLayout& pl = gDocLines[pi];
            const float lmul = (p.lineSpacing > 0.05f) ? p.lineSpacing : 1.f;
            pl = dt::LayoutParagraph(p.text, DocParaSpans(p), std::max(20.f, cW - lo.indent),
                                     &tabPx, kDocDefTab * docScale, lmul, (int)p.align);
            paraH = pl.h;
        }
        if (curY + paraH > botOf(curPage) && curY > topOf(curPage)) {
            advance();
            lo.page = curPage; lo.yStart = curY;
            lo.col  = curCol;  lo.xOff = xOffOf(curCol);
        }
        curY += paraH;
        if (p.spaceAfter > 0.f) curY += p.spaceAfter * docScale;   // explicit only
        lo.yEnd = curY;
    }

    int pages = 1;
    for (const auto& lo : gDocLayout) pages = std::max(pages, lo.page + 1);
    gDocPageTops.assign((size_t)pages, 0.f);
    float acc = 0.f;
    for (int pg = 0; pg < pages; ++pg) {
        gDocPageTops[(size_t)pg] = acc;
        acc += DocPageSizeOf(DocPageSetupAt(pg)).y + kDocGap;
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// ─── PDF generation ───────────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════
// Debug counters for the export pass (read by the headless export check).
static int              gPdfDbgPages = 0;
static std::vector<int> gPdfDbgElems;
static int              gPdfDbgParas = 0;
static float            gPdfDbgY = 0.f;
static int              gPdfDbgFlush = 0;
// Width-aware line breaking for the writer. The old estimate (0.5 em per byte)
// under-measured wide runs, so a bold or W-heavy run was drawn over the run that
// followed it. Tab jumps count as the distance to the next tab stop.
static size_t PdfLineFit(const char* s, size_t len, float avail,
                         float fs, int fam, bool bold, bool italic)
{
    float w = 0.f;
    size_t lastSpace = (size_t)-1;
    for (size_t k = 0; k < len; ++k) {
        unsigned char c = (unsigned char)s[k];
        float cw;
        if (c == '\t') {
            float stop = (floorf(w / kDocDefTab) + 1.f) * kDocDefTab;
            cw = stop - w;
        } else {
            cw = PdfCharW(c, fs, fam, bold, italic);
        }
        if (w + cw > avail && k > 0)
            return (lastSpace != (size_t)-1 && lastSpace > 0) ? lastSpace : k;
        w += cw;
        if (c == ' ') lastSpace = k;
    }
    return len;
}

// Lightweight PDF writer: one text element per styled chunk of a visual line,
// so per-run colours, highlights, super/subscripts and monospace code survive
// the export (the canvas shows the same thing).
static std::vector<uint8_t> BuildDocxPdf(const std::vector<DocPara>& paras)
{
    // per-page geometry: each page can be landscape with its own margins
    int   pageIdx = 0;
    int   colIdx  = 0;
    int   curCols = 1;
    float curGap  = 16.f;
    float colShift = 0.f;
    float PW = 595.f, PH = 842.f, ML = kDocML, MR = kDocMR, MT = kDocMT, MB = kDocMB;
    float TW = PW - ML - MR;
    auto syncPage = [&](int pg) {
        pageIdx = pg; colIdx = 0; colShift = 0.f;
        DocPageSetup st = DocPageSetupAt(pg);
        ImVec2 sz = DocPageSizeOf(st);
        PW = sz.x; PH = sz.y;
        ML = st.ml; MR = st.mr; MT = st.mt; MB = st.mb;
        TW = PW - ML - MR;            // full text width; per-section column width is set per paragraph below
    };

    auto styleFS = [](DocStyle s) -> float {
        if (s == DS_H1) return 24.f;
        if (s == DS_H2) return 18.f;
        if (s == DS_H3) return 14.f;
        return 12.f;
    };
    auto spacePre = [](DocStyle s) -> float {
        if (s == DS_H1) return 18.f;
        if (s == DS_H2) return 12.f;
        if (s == DS_H3) return 8.f;
        return 4.f;
    };

    struct PageElem {
        bool isImage = false;
        DocImageLayout imgLayout = IL_Behind;
        std::string text;
        float fs = 12.f;
        int   family = 0;                 // index into kDocFamilyName[]
        bool bold = false, italic = false, ul = false, strike = false, mono = false;
        int  vert = 0;
        bool hasColor = false; ImVec4 color = {0, 0, 0, 1};
        bool hasHL = false;    ImVec4 hl = {1, 1, 0, 1};
        bool cont = false;          // continues the current visual line
        float xOff = 0.f;           // x offset inside the text column
        float w = 0.f;              // advance width of this chunk
        DocAlign align = DA_Left;
        int imgIdx = -1; float imgW = 0.f, imgH = 0.f, imgX = 0.f, imgY = 0.f;
        // table support: axis-aligned rects and elements with an absolute baseline
        bool isRect = false, rectFill = false, rectStroke = false;
        float rx = 0.f, ry = 0.f, rw = 0.f, rh = 0.f;
        float rstroke = 0.5f;
        ImVec4 rcol = {1, 1, 1, 1};
        bool hasY = false; float yAbs = 0.f;
        float pre = 0.f;                  // space-before, applied when this line starts
        bool  noAlign = false;            // xOff already absolute (table cells)
        // Which newspaper column this element belongs to. The emit pass needs it:
        // xOff is column-local, so the column's absolute left edge must travel
        // with the element, not be read from the page-level cursor (which is
        // always the *last* column by the time the page is written).
        int   col  = 0;
        int   paraIdx = -1;               // owning paragraph (justification: last line)
        float colX = 0.f;                 // absolute left edge of the text column
        float colW = 0.f;                 // width of that column (alignment basis)
    };
    using Page = std::vector<PageElem>;
    std::vector<Page> pages;
    Page cur;
    // Stamp every element with the column it was laid into. Rects and images
    // already carry an absolute x, but text keeps a column-local xOff, and one
    // page holds elements from every column — so the column geometry has to be
    // stored per element.
    int curPara = -1;                 // paragraph being emitted (for justification)
    auto pushElem = [&](PageElem& e) {
        e.col  = colIdx;
        e.paraIdx = curPara;
        e.colX = ML + colShift;
        e.colW = TW;
        cur.push_back(e);
    };
    // Start on page 0 with *page 0's* geometry: without this the first page is
    // laid out with the portrait defaults, so a landscape first page gets its
    // text placed from the wrong margins (and wrapped to the wrong width).
    syncPage(0);
    float y      = PH - MT;
    float secTop = y;             // top of the current section's column band
    auto flush = [&]() {
        gPdfDbgFlush++;
        if (colIdx + 1 < curCols) {
            ++colIdx;
            colShift = (TW + curGap) * (float)colIdx;
        } else {
            pages.push_back(cur);
            cur.clear();
            syncPage(pageIdx + 1);
            colIdx = 0; colShift = 0.f;
            secTop = PH - MT;
        }
        y = secTop;
    };

    std::vector<char> secHasImage(gDocxSections.size() + 1, 0);
    for (const auto& pp : paras)
        if (pp.imageIdx >= 0 && pp.section >= 0 && pp.section < (int)secHasImage.size())
            secHasImage[(size_t)pp.section] = 1;
    (void)secHasImage;   // inline figures are column-relative now; no single-column fallback

    int   curSec  = -1;
    for (const auto& para : paras) {
        gPdfDbgParas++;
        ++curPara;
        if (para.section != curSec) {
            DocxSection sec;
            if (para.section >= 0 && para.section < (int)gDocxSections.size())
                sec = gDocxSections[(size_t)para.section];
            if (curSec >= 0) {
                if (sec.continuous) {
                    // Same page, new column band starting where the last text ended.
                    secTop = y;
                    colIdx = 0; colShift = 0.f; y = secTop;
                } else {
                    if (!cur.empty()) { pages.push_back(cur); cur.clear(); }
                    syncPage(pageIdx + 1);
                    colIdx = 0; colShift = 0.f; y = PH - MT; secTop = y;
                }
            } else {
                secTop = y;
            }
            curSec = para.section;
        }
        {
            int pcols = 1; float pgap = 16.f;
            if (para.section >= 0 && para.section < (int)gDocxSections.size()) {
                pcols = std::max(1, gDocxSections[(size_t)para.section].cols);
                pgap  = gDocxSections[(size_t)para.section].gap;
            }
            curCols = pcols;
            curGap  = pgap > 0.f ? pgap : 16.f;
            TW = (PW - ML - MR - curGap * (pcols - 1)) / (float)pcols;
        }
        // Wrapping width for *this* paragraph. flush() calls syncPage(), which
        // resets TW to the full text width; a paragraph that continued after a
        // column break then wrapped at page width and ran into the next column.
        const float wrapW = TW;
        if (para.divider) {
            y -= 14.f;
            if (y < MB + 14.f) flush();
            if (para.divider == 1) {
                PageElem r;
                r.isRect = r.rectStroke = true;
                r.rx = ML + colShift; r.ry = y; r.rw = TW; r.rh = 0.6f;
                r.rcol = ImVec4(0.47f, 0.50f, 0.56f, 1.f);
                r.rstroke = 0.8f;
                r.hasY = true; r.cont = true;
                pushElem(r);
            } else {
                PageElem e;
                e.text = std::string(110, '.');
                e.fs = 9.f;
                e.cont = false;
                e.hasY = true; e.yAbs = y;
                e.w = PdfTextW(e.text.c_str(), e.fs, 0, false, false);
                pushElem(e);
            }
            continue;
        }
        if (para.tableIdx >= 0 && para.tableIdx < (int)gDocTables.size()) {
            const DocTable& tb = gDocTables[para.tableIdx];
            const float cfs = 10.f, cpad = 4.f, clh = 12.5f;
            const float tIndent = tb.styled ? tb.indent : 0.f;
            const float avail = std::max(40.f, TW - tIndent);
            std::vector<float> cw((size_t)tb.cols), cx((size_t)tb.cols + 1, 0.f);
            for (int c = 0; c < tb.cols; ++c) cw[(size_t)c] = tb.ColW(c, avail);
            cx[0] = tIndent;
            for (int c = 0; c < tb.cols; ++c) cx[(size_t)c + 1] = cx[(size_t)c] + cw[(size_t)c];

            // Wrap every cell before drawing; a fixed 15 pt row made long cells
            // overlap each other and the paragraph under the table.
            std::vector<std::vector<std::vector<std::string>>> cellLines((size_t)tb.rows);
            std::vector<float> rowH((size_t)tb.rows, 15.f);
            float tableH = 0.f;
            for (int r = 0; r < tb.rows; ++r) {
                cellLines[(size_t)r].resize((size_t)tb.cols);
                size_t maxLines = 1;
                for (int c = 0; c < tb.cols; ++c) {
                    const std::string txt = tb.at(r, c);
                    std::vector<std::string>& out = cellLines[(size_t)r][(size_t)c];
                    size_t pos = 0;
                    while (pos < txt.size()) {
                        size_t take = PdfLineFit(txt.c_str() + pos, txt.size() - pos,
                                                 std::max(16.f, cw[(size_t)c] - 2.f * cpad), cfs, 0,
                                                 tb.header && r == 0, false);
                        if (take == 0) take = 1;
                        out.push_back(txt.substr(pos, take));
                        pos += take;
                        while (pos < txt.size() && txt[pos] == ' ') ++pos;
                    }
                    maxLines = std::max(maxLines, out.size());
                }
                rowH[(size_t)r] = std::max(15.f, (float)maxLines * clh + 6.f);
                tableH += rowH[(size_t)r];
            }
            // Keep the whole table in one column: a row split over a column break
            // would be drawn in two places.
            if (y - tableH < MB + 4.f) flush();
            float yCur = y;
            for (int r = 0; r < tb.rows; ++r) {
                const float rh = rowH[(size_t)r];
                float ry = yCur - rh;
                for (int c = 0; c < tb.cols; ++c) {
                    // fill: authored shading first, else the header/body tint
                    ImVec4 fill = (tb.header && r == 0) ? tb.headerFill : tb.bodyFill;
                    if ((size_t)r * tb.cols + c < tb.cellFill.size()) {
                        ImVec4 cf = tb.cellFill[(size_t)r * tb.cols + (size_t)c];
                        if (cf.w > 0.01f) fill = cf;
                    }
                    if (fill.w > 0.01f) {
                        PageElem bg;
                        bg.isRect = bg.rectFill = true;
                        bg.rx = ML + colShift + cx[(size_t)c];
                        bg.ry = ry;
                        bg.rw = cw[(size_t)c];
                        bg.rh = rh;
                        bg.rcol = fill;
                        bg.hasY = true;
                        bg.cont = true;          // rects never advance the text cursor
                        pushElem(bg);
                    }
                    const std::vector<std::string>& ls = cellLines[(size_t)r][(size_t)c];
                    int ca = ((size_t)r * tb.cols + c < tb.cellAlign.size())
                                 ? tb.cellAlign[(size_t)r * tb.cols + (size_t)c] : -1;
                    for (size_t k = 0; k < ls.size(); ++k) {
                        PageElem e;
                        e.text = ls[k];
                        e.fs = cfs;
                        e.bold = tb.header && r == 0;
                        e.hasY = true;
                        e.yAbs = ry + rh - 5.f - (float)k * clh;
                        e.cont = true;
                        e.noAlign = true;
                        float tx = cx[(size_t)c] + cpad;
                        if (ca == 1 || ca == 3)
                            tx = cx[(size_t)c] + std::max(cpad, (cw[(size_t)c] -
                                 PdfTextW(ls[k].c_str(), cfs, 0, e.bold, false)) * 0.5f);
                        else if (ca == 2)
                            tx = cx[(size_t)c] + cw[(size_t)c] - cpad -
                                 PdfTextW(ls[k].c_str(), cfs, 0, e.bold, false);
                        e.xOff = tx;
                        e.w = PdfTextW(e.text.c_str(), cfs, 0, e.bold, false);
                        pushElem(e);
                    }
                }
                yCur = ry;
            }
            // borders: Word stores each side; "none" draws nothing
            auto hline = [&](float yy, float xa, float xb, const DocBorder& b) {
                if (!b.on || b.w <= 0.f) return;
                PageElem e;
                e.isRect = e.rectFill = true;
                e.rx = ML + colShift + xa; e.ry = yy - b.w * 0.5f;
                e.rw = xb - xa; e.rh = b.w;
                e.rcol = b.col; e.hasY = true; e.cont = true;
                pushElem(e);
            };
            auto vline = [&](float xx, float ya, float yb, const DocBorder& b) {
                if (!b.on || b.w <= 0.f) return;
                PageElem e;
                e.isRect = e.rectFill = true;
                e.rx = ML + colShift + xx - b.w * 0.5f; e.ry = yb;
                e.rw = b.w; e.rh = ya - yb;
                e.rcol = b.col; e.hasY = true; e.cont = true;
                pushElem(e);
            };
            if (tb.styled) {
                const float ty0 = y, ty1 = y - tableH;
                hline(ty0, cx[0], cx[(size_t)tb.cols], tb.bTop);
                hline(ty1, cx[0], cx[(size_t)tb.cols], tb.bBottom);
                vline(cx[0], ty0, ty1, tb.bLeft);
                vline(cx[(size_t)tb.cols], ty0, ty1, tb.bRight);
                float yy = ty0;
                for (int r = 0; r < tb.rows; ++r) {
                    yy -= rowH[(size_t)r];
                    if (r + 1 < tb.rows) hline(yy, cx[0], cx[(size_t)tb.cols], tb.bInsideH);
                }
                for (int c = 1; c < tb.cols; ++c) vline(cx[(size_t)c], ty0, ty1, tb.bInsideV);
            } else if (tb.borderW > 0.01f) {
                float yy = y;
                for (int r = 0; r < tb.rows; ++r) {
                    const float rh = rowH[(size_t)r];
                    float ry = yy - rh;
                    for (int c = 0; c < tb.cols; ++c) {
                        PageElem fr;
                        fr.isRect = fr.rectStroke = true;
                        fr.rx = ML + colShift + cx[(size_t)c]; fr.ry = ry;
                        fr.rw = cw[(size_t)c]; fr.rh = rh;
                        fr.rcol = tb.borderCol; fr.rstroke = tb.borderW;
                        fr.hasY = true; fr.cont = true;
                        pushElem(fr);
                    }
                    yy = ry;
                }
            }
            y = yCur - 4.f;
            if (para.caption[0]) {
                PageElem cap;
                cap.text = para.caption;
                cap.fs = 9.f;
                cap.italic = true;
                cap.align = DA_Center;
                cap.cont = false;
                cap.hasY = true; cap.yAbs = y - 10.f;
                cap.w = PdfTextW(cap.text.c_str(), cap.fs, 0, false, true);
                pushElem(cap);
                y -= 20.f;
            }
            if (y < MB + 20.f) flush();
            continue;
        }
        if (para.imageIdx >= 0 && para.imageIdx < (int)gDocImages.size()) {
            const DocImage& img = gDocImages[para.imageIdx];
            if (img.pixels.empty()) continue;
            float dW = std::min(TW * (para.imageWidthPct / 100.f), (float)img.srcW);
            float dH = img.srcH > 0 ? dW * (float)img.srcH / (float)img.srcW : dW * 0.5f;
            PageElem e; e.isImage = true; e.imgLayout = para.imageLayout;
            e.imgIdx = para.imageIdx; e.imgW = dW; e.imgH = dH; e.imgX = ML + colShift;
            if (para.imageLayout == IL_Inline) {
                y -= 8.f;
                if (y < MB + dH) flush();
                y -= dH;
                e.imgY = y;
            } else {
                e.imgY = std::max(y - dH, MB);
            }
            pushElem(e);
            if (para.caption[0] && para.imageLayout == IL_Inline) {
                PageElem cap;
                cap.text = para.caption;
                cap.fs = 9.f;
                cap.italic = true;
                cap.align = DA_Center;
                cap.cont = false;
                cap.hasY = true; cap.yAbs = y - 11.f;
                cap.w = PdfTextW(cap.text.c_str(), cap.fs, 0, false, true);
                pushElem(cap);
                y -= 18.f;
            }
            continue;
        }

        const float fs   = (para.baseFs > 0.f) ? para.baseFs : styleFS(para.style);
        const bool  mono = (para.style == DS_Code);
        const float lmul = (para.lineSpacing > 0.05f) ? para.lineSpacing : 1.f;
        const int textLen = (int)strlen(para.text);
        DocAttr base = DocBaseAttr(para);
        std::vector<DocRun> runs = para.runs;
        DocRunsNorm(runs, textLen, base);
        // A visual line is as tall as its tallest run, not as tall as the
        // paragraph style: a 24 pt run in a 12 pt paragraph overlapped the line
        // below it when spacing was taken from the style alone.
        float maxRunFs = fs;
        for (const DocRun& rr : runs)
            if (rr.a.fsPt > 0.f && rr.a.fsPt > maxRunFs) maxRunFs = rr.a.fsPt;
        const float lh = maxRunFs * 1.20f * lmul;   // same factor as doctext LineHeightFor
        float pendingPre = (para.spaceBefore >= 0.f) ? para.spaceBefore
                                                     : (para.baseFs > 0.f ? 0.f : spacePre(para.style));
        y -= pendingPre;
        if (y < MB + lh) flush();
        if (textLen == 0) { y -= lh * 0.5f; continue; }

        // Column-local x. Absolute (ML + colShift + x) made every wrapped line in
        // column 2 look like it was mid-line: e.cont stayed true, so the whole
        // column collapsed onto one baseline, and maxC went negative.
        float cursorX = 0.f;
        int   acc = 0;
        bool  paraFirstLine = true;
        for (size_t ri = 0; ri < runs.size(); ++ri) {
            if (runs[ri].len <= 0) continue;
            std::string seg(para.text + acc, (size_t)runs[ri].len);
            acc += runs[ri].len;
            const DocAttr& a = runs[ri].a;

            const float rfs  = (a.fsPt > 0.f) ? a.fsPt : fs;
            // The PDF writer only carries the base-14 faces, so every embedded
            // family collapses onto its class: sans -> Helvetica, serif -> Times,
            // mono -> Courier.
            int         rfam = kDocFamilyClass[DocFamilyClamp(a.family)];
            if (para.style == DS_Code && rfam == 0) rfam = 2;

            std::string rem = seg;
            while (!rem.empty()) {
                // hard newline inside the run
                size_t nl = rem.find('\n');
                std::string piece = (nl == std::string::npos) ? rem : rem.substr(0, nl);
                bool hardBreak = (nl != std::string::npos);
                rem = hardBreak ? rem.substr(nl + 1) : std::string();

                std::string rest = piece;
                while (!rest.empty()) {
                    size_t take = PdfLineFit(rest.c_str(), rest.size(),
                                             std::max(4.f, wrapW - cursorX), rfs, rfam,
                                             a.bold, a.italic);
                    if (take == 0) take = 1;
                    std::string line = rest.substr(0, take);
                    rest = rest.substr(take);
                    while (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
                    if (line.empty()) continue;
                    // Split the chunk at tabs: each gap becomes a leader run so
                    // the PDF shows the same ... / --- / ___ the canvas does.
                    struct Seg { std::string txt; float xoff = 0.f; float w = 0.f; };
                    std::vector<Seg> segs;
                    {
                        float cxpt = cursorX;
                        std::string buf;
                        for (size_t k = 0; k <= line.size(); ++k) {
                            char ch = (k < line.size()) ? line[k] : '\0';
                            if (ch == '\t' || ch == '\0') {
                                if (!buf.empty()) {
                                    Seg s; s.txt = buf; s.xoff = cxpt;
                                    s.w = PdfTextW(buf.c_str(), rfs, rfam, a.bold, a.italic);
                                    segs.push_back(s);
                                    cxpt += s.w;
                                    buf.clear();
                                }
                                if (ch == '\0') break;
                                float target = -1.f;
                                for (const DocTabStop& t : gDocTabs)
                                    if (t.x > cxpt + 0.5f) { target = t.x; break; }
                                if (target < 0.f) {
                                    float kk = floorf((cxpt + 0.5f) / kDocDefTab) + 1.f;
                                    target = kk * kDocDefTab;
                                }
                                float gap = std::max(2.f, target - cxpt);
                                float xPt = cxpt + gap;
                                int leader = 0;
                                for (const DocTabStop& t : gDocTabs)
                                    if (fabsf(t.x - xPt) < 1.5f) { leader = t.leader; break; }
                                if (leader > 0) {
                                    char lc = (leader == 1) ? '.' : (leader == 2) ? '-' : '_';
                                    float step = PdfCharW((unsigned char)lc, rfs, rfam, a.bold, false);
                                    int n = std::max(1, (int)(gap / std::max(1.f, step)));
                                    Seg s; s.txt = std::string((size_t)n, lc); s.xoff = cxpt; s.w = gap;
                                    segs.push_back(s);
                                }
                                cxpt += gap;
                            } else {
                                buf.push_back(ch);
                            }
                        }
                    }
                    // Justification is resolved here, where the line's full width is
                    // known: split the line into words and hand the emitter absolute
                    // x positions. Doing it at emit time needs the slack of every
                    // preceding element, which the pre-stretch offsets do not carry.
                    if (para.align == DA_Justify && !rest.empty() && !segs.empty()) {
                        float total = 0.f;
                        int   spaces = 0;
                        for (const Seg& sg : segs) total += sg.w;
                        for (char ch : line) if (ch == ' ') ++spaces;
                        // wrapW, not TW: flush() resets TW to page width, and a
                        // paragraph continuing after a column break would then justify
                        // its lines across both columns.
                        const float slack = wrapW - total;
                        // A wrapped line is nearly full; a huge slack means the
                        // measurement is wrong (run boundary, trailing spaces), and
                        // stretching it would scatter the words across the column.
                        if (spaces > 0 && slack > 0.f && slack < wrapW * 0.35f) {
                            const float per = slack / (float)spaces;
                            const float spaceW = PdfCharW(' ', rfs, rfam, a.bold, a.italic);
                            std::vector<Seg> out;
                            float acc = 0.f;      // slack spent before this point
                            for (const Seg& sg : segs) {
                                size_t pos = 0;
                                float  local = 0.f;   // un-stretched offset inside this seg
                                while (pos <= sg.txt.size()) {
                                    size_t sp = sg.txt.find(' ', pos);
                                    std::string w = (sp == std::string::npos)
                                                        ? sg.txt.substr(pos)
                                                        : sg.txt.substr(pos, sp - pos);
                                    if (!w.empty()) {
                                        Seg n;
                                        n.txt  = w;
                                        n.xoff = sg.xoff + local + acc;
                                        n.w    = PdfTextW(w.c_str(), rfs, rfam, a.bold, a.italic);
                                        out.push_back(n);
                                    }
                                    if (sp == std::string::npos) break;
                                    // advance over the word, then the gap: the word
                                    // width is not in sg.xoff (that is the seg start)
                                    local += PdfTextW(w.c_str(), rfs, rfam, a.bold, a.italic) + spaceW;
                                    acc   += per;      // plus the justification slack
                                    pos = sp + 1;
                                }
                            }
                            segs.swap(out);
                        }
                    }
                    bool firstSeg = true;
                    for (const Seg& sg : segs) {
                        if (sg.txt.empty()) continue;
                        PageElem e;
                        e.text  = sg.txt;
                        e.fs    = rfs;
                        e.family = rfam;
                        e.mono  = (rfam == 2);
                        e.bold  = a.bold;
                        e.italic = a.italic;
                        e.ul    = a.underline;
                        e.strike = a.strike;
                        e.vert  = a.vert;
                        e.hasColor = a.hasColor; e.color = a.color;
                        e.hasHL    = a.hasHL;    e.hl    = a.hl;
                        e.align = para.align;
                        e.cont  = firstSeg ? (cursorX > 0.01f) : true;
                        e.xOff  = sg.xoff;
                        e.w     = sg.w;
                        if (!e.cont) {                  // first chunk of a new visual line
                            if (!paraFirstLine) y -= lh;
                            paraFirstLine = false;
                        }
                        e.hasY  = true;
                        e.yAbs  = y;
                        if (!e.cont && pendingPre > 0.f) { e.pre = pendingPre; pendingPre = 0.f; }
                        pushElem(e);
                        firstSeg = false;
                    }
                    cursorX = (segs.empty() ? 0.f : segs.back().xoff + segs.back().w);
                    if (!rest.empty()) {
                        cursorX = 0.f;
                        if (y < MB + lh) flush();
                    }
                }
                if (hardBreak) {
                    y -= lh * (piece.empty() ? 0.5f : 1.f);
                    cursorX = 0.f;
                    if (y < MB + lh) flush();
                }
            }
        }
        y -= lh;                          // move below the paragraph's last line
        if (para.spaceAfter > 0.f) y -= para.spaceAfter;
        if (y < MB + lh) flush();         // column (or page) is full
    }
    // Commit the last page. flush() must NOT be used here: with more than one
    // column it advances to the next column instead of closing the page, so a
    // short last page (one that ends in column 1) was silently dropped.
    if (!cur.empty()) { pages.push_back(cur); cur.clear(); }
    if (pages.empty()) pages.push_back({});
    gPdfDbgY = y;
    gPdfDbgPages = (int)pages.size();
    gPdfDbgElems.assign(pages.size(), 0);
    for (size_t i = 0; i < pages.size(); ++i) gPdfDbgElems[i] = (int)pages[i].size();

    const int np = (int)pages.size();
    const int kFirstPageObj = 15;    // 1 catalog, 2 pages, 3..6 Helvetica, 7..10 Courier, 11..14 Times
    std::vector<int> usedImgs;
    for (const auto& pg : pages)
        for (const auto& e : pg)
            if (e.isImage && std::find(usedImgs.begin(), usedImgs.end(), e.imgIdx) == usedImgs.end())
                usedImgs.push_back(e.imgIdx);

    std::string buf;
    buf.reserve(65536);
    buf += "%PDF-1.4\n";
    std::vector<size_t> xoff;

    auto obj_dict = [&](const std::string& d) {
        xoff.push_back(buf.size());
        buf += std::to_string(xoff.size()) + " 0 obj\n" + d + "\nendobj\n";
    };
    auto obj_stream = [&](const std::string& s) {
        xoff.push_back(buf.size());
        buf += std::to_string(xoff.size()) + " 0 obj\n<< /Length "
             + std::to_string(s.size()) + " >>\nstream\n" + s + "endstream\nendobj\n";
    };

    std::string kids = "[";
    for (int i = 0; i < np; i++) { if (i) kids += ' '; kids += std::to_string(kFirstPageObj + i * 2) + " 0 R"; }
    kids += "]";

    std::string imgXobjDict;
    for (int ii = 0; ii < (int)usedImgs.size(); ii++) {
        if (ii) imgXobjDict += " ";
        imgXobjDict += "/Img" + std::to_string(usedImgs[ii]) + " "
                     + std::to_string(kFirstPageObj + np * 2 + ii) + " 0 R";
    }

    obj_dict("<< /Type /Catalog /Pages 2 0 R >>");
    obj_dict("<< /Type /Pages /Kids " + kids + " /Count " + std::to_string(np) + " >>");
    obj_dict("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>");
    obj_dict("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold /Encoding /WinAnsiEncoding >>");
    obj_dict("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Oblique /Encoding /WinAnsiEncoding >>");
    obj_dict("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-BoldOblique /Encoding /WinAnsiEncoding >>");
    obj_dict("<< /Type /Font /Subtype /Type1 /BaseFont /Courier /Encoding /WinAnsiEncoding >>");
    obj_dict("<< /Type /Font /Subtype /Type1 /BaseFont /Courier-Bold /Encoding /WinAnsiEncoding >>");
    obj_dict("<< /Type /Font /Subtype /Type1 /BaseFont /Courier-Oblique /Encoding /WinAnsiEncoding >>");
    obj_dict("<< /Type /Font /Subtype /Type1 /BaseFont /Courier-BoldOblique /Encoding /WinAnsiEncoding >>");
    obj_dict("<< /Type /Font /Subtype /Type1 /BaseFont /Times-Roman /Encoding /WinAnsiEncoding >>");
    obj_dict("<< /Type /Font /Subtype /Type1 /BaseFont /Times-Bold /Encoding /WinAnsiEncoding >>");
    obj_dict("<< /Type /Font /Subtype /Type1 /BaseFont /Times-Italic /Encoding /WinAnsiEncoding >>");
    obj_dict("<< /Type /Font /Subtype /Type1 /BaseFont /Times-BoldItalic /Encoding /WinAnsiEncoding >>");

    for (int pi = 0; pi < np; pi++) {
        syncPage(pi);
        const int cont = kFirstPageObj + pi * 2 + 1;
        std::string cs;
        cs.reserve(16384);
        cs += "0.5 w\n";
        // One y-cursor per text column: the page object holds elements from every
        // column of that page, and a single cursor made column 2 advance from
        // column 1's position.
        float cyCol[8];
        for (int ci = 0; ci < 8; ++ci) cyCol[ci] = PH - MT;
        auto colSlot = [](int c) { return c < 0 ? 0 : (c > 7 ? 7 : c); };
        float lineOff = 0.f;
        float lineStretch = 0.f;      // accumulated justification slack on this line

        {
            DocPageSetup pst = DocPageSetupAt(pi);
            const float kHFfs = 9.f, kHFlh = 12.f;
            auto pdfEsc = [](const std::string& s) {
                std::string e;
                for (unsigned char c : s) {
                    if (c == '(') e += "\\(";
                    else if (c == ')') e += "\\)";
                    else if (c == '\\') e += "\\\\";
                    else if (c >= 32 && c <= 126) e += (char)c;
                }
                return e;
            };
            char row[768];
            if (gDocHeader[0] && pst.header) {
                std::vector<std::string> hls;
                DocHFTextLines(gDocHeader, pi + 1, hls);
                // Anchored to the paper edge via pst.hd, never to the top margin.
                for (size_t li = 0; li < hls.size(); ++li) {
                    snprintf(row, sizeof(row), "BT\n/F1 %.1f Tf\n1 0 0 1 %.2f %.2f Tm\n(%s) Tj\nET\n",
                             kHFfs, ML, PH - pst.hd - kHFfs * 0.9f - (float)li * kHFlh,
                             pdfEsc(hls[li]).c_str());
                    cs += row;
                }
                float ruleY = PH - pst.hd - kHFfs * 0.9f - (float)(hls.empty() ? 0 : hls.size() - 1) * kHFlh - 3.f;
                snprintf(row, sizeof(row), "0.3 w\n%.2f %.2f m %.2f %.2f l S\n", ML, ruleY, PW - MR, ruleY);
                cs += row;
            }
            if (gDocFooter[0] && pst.footer) {
                std::vector<std::string> fls;
                DocHFTextLines(gDocFooter, pi + 1, fls);
                // The footer block's bottom line sits pst.fd above the paper edge.
                for (size_t li = 0; li < fls.size(); ++li) {
                    float fy = pst.fd + kHFfs * 0.35f + (float)(fls.size() - 1 - li) * kHFlh;
                    snprintf(row, sizeof(row), "BT\n/F1 %.1f Tf\n1 0 0 1 %.2f %.2f Tm\n(%s) Tj\nET\n",
                             kHFfs, ML, fy, pdfEsc(fls[li]).c_str());
                    cs += row;
                }
                float ruleY2 = pst.fd + kHFfs * 0.35f + (float)fls.size() * kHFlh - 2.f;
                snprintf(row, sizeof(row), "0.3 w\n%.2f %.2f m %.2f %.2f l S\n", ML, ruleY2, PW - MR, ruleY2);
                cs += row;
            }
        }

        auto emitImg = [&](const PageElem& e) {
            char row[256];
            snprintf(row, sizeof(row), "q %.2f 0 0 %.2f %.2f %.2f cm /Img%d Do Q\n",
                     e.imgW, e.imgH, e.imgX, e.imgY, e.imgIdx);
            cs += row;
        };
        for (const auto& e : pages[pi])
            if (e.isImage && e.imgLayout == IL_Behind) emitImg(e);

        const Page& pg2 = pages[pi];
        for (int ei = 0; ei < (int)pg2.size(); ++ei) {
            const PageElem& e = pg2[ei];
            float& cy = cyCol[colSlot(e.col)];
            if (e.isImage) {
                if (e.imgLayout == IL_Inline) { emitImg(e); cy = e.imgY; }
                continue;
            }
            if (!e.cont) {
                // start of a visual line: advance, and compute alignment once
                cy -= e.fs * 1.5f + e.pre;
                float lineW = e.w;
                int   endK  = ei;
                for (int k = ei + 1; k < (int)pg2.size() && pg2[k].cont; ++k) { lineW += pg2[k].w; endK = k; }
                const float colW = e.colW > 1.f ? e.colW : TW;
                lineOff = (e.align == DA_Center) ? (colW - std::min(lineW, colW)) * 0.5f
                        : (e.align == DA_Right)  ? (colW - std::min(lineW, colW))        : 0.f;
                if (lineOff < 0.f) lineOff = 0.f;
            }

            // xOff is relative to this element's own column (stamped by pushElem);
            // the page-level ML is added once, here.
            float cx = (e.colW > 1.f ? e.colX : ML) + (e.noAlign ? 0.f : lineOff) + e.xOff;
            float efs = e.vert ? e.fs * 0.70f : e.fs;
            float eyBase = e.hasY ? e.yAbs : cy;
            float ey  = eyBase + (e.vert > 0 ? e.fs * 0.33f : (e.vert < 0 ? -e.fs * 0.12f : 0.f));

            if (e.isRect) {
                char rrow[256];
                if (e.rectFill)
                    snprintf(rrow, sizeof(rrow), "q %.3f %.3f %.3f rg %.2f %.2f %.2f %.2f re f Q\n",
                             e.rcol.x, e.rcol.y, e.rcol.z, e.rx, e.ry, e.rw, e.rh);
                else
                    snprintf(rrow, sizeof(rrow), "q %.2f w %.3f %.3f %.3f RG %.2f %.2f %.2f %.2f re S Q\n",
                             e.rstroke, e.rcol.x, e.rcol.y, e.rcol.z, e.rx, e.ry, e.rw, e.rh);
                cs += rrow;
                continue;
            }
            if (e.text.empty()) continue;
            const char* fn;
            if (e.family == 2)
                fn = (e.bold && e.italic) ? "/F8" : e.bold ? "/F6" : e.italic ? "/F7" : "/F5";
            else if (e.family == 1)
                fn = (e.bold && e.italic) ? "/F12" : e.bold ? "/F10" : e.italic ? "/F11" : "/F9";
            else
                fn = (e.bold && e.italic) ? "/F4" : e.bold ? "/F2" : e.italic ? "/F3" : "/F1";

            // highlight behind the run — on the run's real baseline, not the
            // page-level cursor (which belonged to another column entirely).
            if (e.hasHL) {
                char hrow[256];
                snprintf(hrow, sizeof(hrow), "q %.3f %.3f %.3f rg %.2f %.2f %.2f %.2f re f Q\n",
                         e.hl.x, e.hl.y, e.hl.z, cx, eyBase - efs * 0.28f,
                         std::max(e.w, 1.f), efs * 1.30f);
                cs += hrow;
            }

            auto emitRunAt = [&](const std::string& txt, float x) {
                std::string esc;
                for (unsigned char c : txt) {
                    if (c == '(') esc += "\\(";
                    else if (c == ')') esc += "\\)";
                    else if (c == '\\') esc += "\\\\";
                    else if (c >= 32 && c <= 126) esc += (char)c;
                }
                char row[2048];
                if (e.hasColor)
                    snprintf(row, sizeof(row), "BT\n%.3f %.3f %.3f rg\n%s %.2f Tf\n1 0 0 1 %.2f %.2f Tm\n(%s) Tj\nET\n",
                             e.color.x, e.color.y, e.color.z, fn, efs, x, ey, esc.c_str());
                else
                    snprintf(row, sizeof(row), "BT\n0 0 0 rg\n%s %.2f Tf\n1 0 0 1 %.2f %.2f Tm\n(%s) Tj\nET\n",
                             fn, efs, x, ey, esc.c_str());
                cs += row;
            };
            emitRunAt(e.text, cx);

            {
                char drow[256];
                if (e.ul) {
                    snprintf(drow, sizeof(drow), "0.5 G %.2f %.2f m %.2f %.2f l S\n",
                             cx, ey - 1.5f, cx + e.w, ey - 1.5f);
                    cs += drow;
                }
                if (e.strike) {
                    snprintf(drow, sizeof(drow), "0.5 G %.2f %.2f m %.2f %.2f l S\n",
                             cx, ey + efs * 0.3f, cx + e.w, ey + efs * 0.3f);
                    cs += drow;
                }
            }
            cs += "0 G\n";
        }
        for (const auto& e : pages[pi])
            if (e.isImage && e.imgLayout == IL_Front) emitImg(e);

        // Shapes on this page
        for (const auto& sh : gDocShapes) {
            if (sh.page != pi) continue;
            float X = sh.x;
            float Ytop = PH - sh.y;
            float Ybot = PH - sh.y - sh.h;
            float W = sh.w, H = sh.h;
            char cmd[1024];
            switch (sh.type) {
            case SH_Rect:
                snprintf(cmd, sizeof(cmd),
                    "q %.3f %.3f %.3f rg %.3f %.3f %.3f RG %.2f w %.2f %.2f %.2f %.2f re B Q\n",
                    sh.fill.x, sh.fill.y, sh.fill.z,
                    sh.stroke.x, sh.stroke.y, sh.stroke.z, sh.thick,
                    X, Ybot, W, H);
                cs += cmd;
                break;
            case SH_Oval: {
                float cx2 = X + W * 0.5f, cy2 = Ybot + H * 0.5f, rx = W * 0.5f, ry = H * 0.5f;
                float kx = rx * 0.5522847f, ky = ry * 0.5522847f;
                snprintf(cmd, sizeof(cmd),
                    "q %.3f %.3f %.3f rg %.3f %.3f %.3f RG %.2f w "
                    "%.2f %.2f m %.2f %.2f %.2f %.2f %.2f %.2f c "
                    "%.2f %.2f %.2f %.2f %.2f %.2f c %.2f %.2f %.2f %.2f %.2f %.2f c "
                    "%.2f %.2f %.2f %.2f %.2f %.2f c B Q\n",
                    sh.fill.x, sh.fill.y, sh.fill.z,
                    sh.stroke.x, sh.stroke.y, sh.stroke.z, sh.thick,
                    cx2 + rx, cy2,
                    cx2 + rx, cy2 + ky, cx2 + kx, cy2 + ry, cx2, cy2 + ry,
                    cx2 - kx, cy2 + ry, cx2 - rx, cy2 + ky, cx2 - rx, cy2,
                    cx2 - rx, cy2 - ky, cx2 - kx, cy2 - ry, cx2, cy2 - ry,
                    cx2 + kx, cy2 - ry, cx2 + rx, cy2 - ky, cx2 + rx, cy2);
                cs += cmd;
                break; }
            case SH_Line:
                snprintf(cmd, sizeof(cmd),
                    "q %.3f %.3f %.3f RG %.2f w %.2f %.2f m %.2f %.2f l S Q\n",
                    sh.stroke.x, sh.stroke.y, sh.stroke.z, sh.thick,
                    X, Ytop, X + W, Ybot);
                cs += cmd;
                break;
            case SH_Triangle:
                snprintf(cmd, sizeof(cmd),
                    "q %.3f %.3f %.3f rg %.3f %.3f %.3f RG %.2f w "
                    "%.2f %.2f m %.2f %.2f l %.2f %.2f l h B Q\n",
                    sh.fill.x, sh.fill.y, sh.fill.z,
                    sh.stroke.x, sh.stroke.y, sh.stroke.z, sh.thick,
                    X + W * 0.5f, Ytop, X + W, Ybot, X, Ybot);
                cs += cmd;
                break;
            }
        }

        std::string res = "/Font << /F1 3 0 R /F2 4 0 R /F3 5 0 R /F4 6 0 R"
                          " /F5 7 0 R /F6 8 0 R /F7 9 0 R /F8 10 0 R"
                          " /F9 11 0 R /F10 12 0 R /F11 13 0 R /F12 14 0 R >>";
        if (!imgXobjDict.empty()) res += " /XObject << " + imgXobjDict + " >>";
        // Build the page object with std::string: a fixed char[] here truncates
        // the /Resources dictionary and strict viewers then drop the page.
        char mbox[48];
        snprintf(mbox, sizeof(mbox), "%.0f %.0f", PW, PH);
        obj_dict("<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " + std::string(mbox) + "]"
                 " /Contents " + std::to_string(cont) + " 0 R"
                 " /Resources << " + res + " >> >>");
        obj_stream(cs);
    }

    for (int ii = 0; ii < (int)usedImgs.size(); ii++) {
        const DocImage& img = gDocImages[usedImgs[ii]];
        int maxDim = 1024;
        int outW = (img.srcW > maxDim) ? maxDim : img.srcW;
        int outH = outW * img.srcH / std::max(1, img.srcW);
        if (outH > maxDim) { outH = maxDim; outW = outH * img.srcW / std::max(1, img.srcH); }
        if (outW < 1) outW = 1;
        if (outH < 1) outH = 1;
        std::string hexData;
        hexData.reserve((size_t)outW * outH * 6 + 2);
        for (int iy = 0; iy < outH; iy++) {
            for (int ix = 0; ix < outW; ix++) {
                int sx = ix * img.srcW / outW, sy = iy * img.srcH / outH;
                int pidx = (sy * img.srcW + sx) * 4;
                uint8_t a = img.pixels[pidx + 3];
                uint8_t cr = (uint8_t)(img.pixels[pidx]     * a / 255u + 255u * (255u - a) / 255u);
                uint8_t cg = (uint8_t)(img.pixels[pidx + 1] * a / 255u + 255u * (255u - a) / 255u);
                uint8_t cb2 = (uint8_t)(img.pixels[pidx + 2] * a / 255u + 255u * (255u - a) / 255u);
                char buf2[7];
                snprintf(buf2, 7, "%02X%02X%02X", cr, cg, cb2);
                hexData += buf2;
            }
        }
        hexData += ">";
        xoff.push_back(buf.size());
        buf += std::to_string(xoff.size()) + " 0 obj\n"
             "<< /Type /XObject /Subtype /Image"
             " /Width " + std::to_string(outW) +
             " /Height " + std::to_string(outH) +
             " /ColorSpace /DeviceRGB /BitsPerComponent 8"
             " /Filter /ASCIIHexDecode"
             " /Length " + std::to_string(hexData.size()) + " >>\n"
             "stream\n" + hexData + "endstream\nendobj\n";
    }

    size_t xstart = buf.size();
    int total = (int)xoff.size() + 1;
    char row[32];
    buf += "xref\n0 " + std::to_string(total) + "\n";
    buf += "0000000000 65535 f \n";
    for (size_t off : xoff) { snprintf(row, sizeof(row), "%010zu 00000 n \n", off); buf += row; }
    buf += "trailer\n<< /Size " + std::to_string(total) + " /Root 1 0 R >>\n";
    buf += "startxref\n" + std::to_string(xstart) + "\n%%EOF\n";
    return {buf.begin(), buf.end()};
}
// ─── image import ─────────────────────────────────────────────────────────────
extern "C" {
EMSCRIPTEN_KEEPALIVE
void doc_receive_image(const uint8_t* rgba, int w, int h) {
    DocImage img; img.srcW=w; img.srcH=h;
    img.pixels.assign(rgba, rgba + (size_t)w*h*4);
    GLuint tex; glGenTextures(1,&tex);
    glBindTexture(GL_TEXTURE_2D,tex);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    img.texID=(unsigned int)tex;
    int idx=(int)gDocImages.size();
    gDocImages.push_back(img);
    DocPushSnap();
    DocPara p; p.imageIdx=idx;
    int ins=(gDocSel>=0)?gDocSel+1:(int)gDocParas.size();
    gDocParas.insert(gDocParas.begin()+ins,p);
    gDocSel=ins; gDocLayoutDirty=true;
}
}

static std::vector<uint8_t> MakeBMP(const DocImage& img, int maxW=1024) {
    int srcW=img.srcW, srcH=img.srcH;
    float sc=(srcW>maxW)?(float)maxW/srcW:1.f;
    int dW=(int)(srcW*sc), dH=(int)(srcH*sc);
    if(dW<1) dW=1; if(dH<1) dH=1;
    int rowPad=(4-(dW*3)%4)%4, rowBytes=dW*3+rowPad;
    int pixSz=rowBytes*dH, fileSz=54+pixSz;
    std::vector<uint8_t> bmp(fileSz,0);
    uint8_t* p=bmp.data();
    p[0]='B'; p[1]='M';
    p[2]=fileSz&0xff; p[3]=(fileSz>>8)&0xff; p[4]=(fileSz>>16)&0xff; p[5]=(fileSz>>24)&0xff;
    p[10]=54;
    p+=14;
    p[0]=40;
    p[4]=dW&0xff; p[5]=(dW>>8)&0xff; p[6]=(dW>>16)&0xff; p[7]=(dW>>24)&0xff;
    int negH=-dH;
    p[8]=negH&0xff; p[9]=(negH>>8)&0xff; p[10]=(negH>>16)&0xff; p[11]=(negH>>24)&0xff;
    p[12]=1; p[14]=24;
    p+=40;
    for(int y=0;y<dH;y++) {
        for(int x=0;x<dW;x++) {
            int sx=x*srcW/dW, sy=y*srcH/dH;
            int idx=(sy*srcW+sx)*4;
            uint8_t a=img.pixels[idx+3];
            uint8_t r=(uint8_t)(img.pixels[idx  ]*a/255u+255u*(255u-a)/255u);
            uint8_t g2=(uint8_t)(img.pixels[idx+1]*a/255u+255u*(255u-a)/255u);
            uint8_t b=(uint8_t)(img.pixels[idx+2]*a/255u+255u*(255u-a)/255u);
            *p++=b; *p++=g2; *p++=r;
        }
        for(int pad=0;pad<rowPad;pad++) *p++=0;
    }
    return bmp;
}

// ══════════════════════════════════════════════════════════════════════════════
// ── DocxInjectImages — includes VML shape injection ──────────────────────────
// ══════════════════════════════════════════════════════════════════════════════
static void DocxInjectImages(const std::vector<std::pair<int,int>>& imgList) {
    zip_t* zr=zip_open("/output.docx",0,'r');
    if(!zr) return;
    int n=zip_total_entries(zr);
    struct Entry { std::string name; std::vector<uint8_t> data; };
    std::vector<Entry> entries;
    entries.reserve(n);
    for(int i=0;i<n;i++){
        zip_entry_openbyindex(zr,i);
        const char* nm=zip_entry_name(zr);
        void* buf=nullptr; size_t sz=0;
        zip_entry_read(zr,&buf,&sz);
        Entry e; e.name=nm; e.data.assign((uint8_t*)buf,(uint8_t*)buf+sz);
        free(buf);
        zip_entry_close(zr);
        entries.push_back(std::move(e));
    }
    zip_close(zr);

    auto xmlEsc=[](const char* s)->std::string{
        std::string o;
        for(;*s;s++){
            if(*s=='&') o+="&amp;"; else if(*s=='<') o+="&lt;";
            else if(*s=='>') o+="&gt;"; else if(*s=='"') o+="&quot;";
            else o+=(char)*s;
        }
        return o;
    };

    // ── Patch word/_rels/document.xml.rels ────────────────────────────────────
    for(auto& e:entries){
        if(e.name!="word/_rels/document.xml.rels") continue;
        std::string xml(e.data.begin(),e.data.end());
        std::string inserts;
        for(auto& [seq,imgIdx]:imgList){
            char rel[256];
            snprintf(rel,sizeof(rel),
                "<Relationship Id=\"rId_img%d\" "
                "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" "
                "Target=\"media/image%d.bmp\"/>",
                seq,seq);
            inserts+=rel;
        }
        if(gDocHeader[0]||gDocFooter[0]){
            inserts+="<Relationship Id=\"rId_hdr1\" "
                "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/header\" "
                "Target=\"header1.xml\"/>"
                "<Relationship Id=\"rId_ftr1\" "
                "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer\" "
                "Target=\"footer1.xml\"/>";
        }
        size_t pos=xml.rfind("</Relationships>");
        if(pos!=std::string::npos) xml.insert(pos,inserts);
        e.data.assign(xml.begin(),xml.end());
        break;
    }

    // ── Patch word/document.xml — pgMar, header/footer refs, VML shapes ───────
    for(auto& e:entries){
        if(e.name!="word/document.xml") continue;
        std::string xml(e.data.begin(),e.data.end());

        // (i) Ensure VML namespace on <w:document ...>
        if (!gDocShapes.empty() && xml.find("xmlns:v=") == std::string::npos) {
            size_t p = xml.find("<w:document");
            if (p != std::string::npos) {
                size_t e2 = xml.find('>', p);
                if (e2 != std::string::npos)
                    xml.insert(e2, " xmlns:v=\"urn:schemas-microsoft-com:vml\"");
            }
        }

        // pgMar / pgSz replacement — from the first page's setup, so a
        // landscape page really exports landscape instead of portrait.
        {
            DocPageSetup ps0 = DocPageSetupAt(0);
            ImVec2 sz0 = DocPageSizeOf(ps0);
            int mlTw=(int)(ps0.ml*20.f), mrTw=(int)(ps0.mr*20.f);
            int mtTw=(int)(ps0.mt*20.f), mbTw=(int)(ps0.mb*20.f);
            char pgMar[256];
            snprintf(pgMar,sizeof(pgMar),
                "<w:pgMar w:top=\"%d\" w:right=\"%d\" w:bottom=\"%d\" w:left=\"%d\""
                " w:header=\"%d\" w:footer=\"%d\" w:gutter=\"0\"/>",
                mtTw,mrTw,mbTw,mlTw,(int)(ps0.hd*20.f),(int)(ps0.fd*20.f));
            size_t pgS=xml.find("<w:pgMar");
            if(pgS!=std::string::npos){
                size_t pgE=xml.find("/>",pgS);
                if(pgE!=std::string::npos) xml.erase(pgS,pgE+2-pgS);
            }
            size_t pos=xml.rfind("</w:sectPr>");
            if(pos!=std::string::npos) xml.insert(pos,pgMar);
            size_t szS = xml.find("<w:pgSz");
            if (szS != std::string::npos) {
                size_t szE = xml.find("/>", szS);
                if (szE != std::string::npos) {
                    char pgsz[192];
                    snprintf(pgsz, sizeof(pgsz), "<w:pgSz w:w=\"%d\" w:h=\"%d\"%s/>",
                             (int)(sz0.x*20.f), (int)(sz0.y*20.f),
                             ps0.landscape ? " w:orient=\"landscape\"" : "");
                    xml.replace(szS, szE + 2 - szS, pgsz);
                }
            }
        }
        // Tab characters become real OOXML tabs (Word lays them out on its own
        // default stop grid; custom stops and leaders stay canvas/PDF-side).
        if (xml.find('\t') != std::string::npos) {
            std::string outx;
            outx.reserve(xml.size() + 32);
            bool inText = false;
            for (size_t i = 0; i < xml.size(); ++i) {
                char c = xml[i];
                if (xml.compare(i, 4, "<w:t") == 0 && i + 4 < xml.size() &&
                    (xml[i+4] == '>' || xml[i+4] == ' ')) inText = true;
                else if (xml.compare(i, 6, "</w:t>") == 0) inText = false;
                if (c == '\t' && inText) {
                    outx += "</w:t><w:tab/><w:t xml:space=\"preserve\">";
                    continue;
                }
                outx += c;
            }
            xml.swap(outx);
        }
        if(gDocHeader[0]||gDocFooter[0]){
            std::string hfRefs=
                "<w:headerReference w:type=\"default\" r:id=\"rId_hdr1\"/>"
                "<w:footerReference w:type=\"default\" r:id=\"rId_ftr1\"/>";
            size_t pos=xml.rfind("</w:sectPr>");
            if(pos!=std::string::npos) xml.insert(pos,hfRefs);
        }

        // (ii) VML shape paragraphs — inserted BEFORE <w:sectPr>
        if (!gDocShapes.empty()) {
            std::string shapes;
            for (const auto& sh : gDocShapes) {
                char fillHex[16], strokeHex[16];
                snprintf(fillHex,   sizeof(fillHex),   "#%02X%02X%02X",
                         (int)(sh.fill.x  *255), (int)(sh.fill.y  *255), (int)(sh.fill.z  *255));
                snprintf(strokeHex, sizeof(strokeHex), "#%02X%02X%02X",
                         (int)(sh.stroke.x*255), (int)(sh.stroke.y*255), (int)(sh.stroke.z*255));
                char v[1024];
                const char* pPr =
                    "<w:p><w:pPr><w:spacing w:before=\"0\" w:after=\"0\" "
                    "w:line=\"1\" w:lineRule=\"exact\"/></w:pPr><w:r><w:pict>";
                switch (sh.type) {
                case SH_Rect:
                    snprintf(v, sizeof(v),
                        "%s<v:rect style=\"position:absolute;left:%dpt;top:%dpt;"
                        "width:%dpt;height:%dpt;z-index:-1;"
                        "mso-position-horizontal-relative:page;"
                        "mso-position-vertical-relative:page\" "
                        "fillcolor=\"%s\" strokecolor=\"%s\" strokeweight=\"%.1fpt\"/>"
                        "</w:pict></w:r></w:p>",
                        pPr, (int)sh.x,(int)sh.y,(int)sh.w,(int)sh.h,
                        fillHex,strokeHex,sh.thick);
                    break;
                case SH_Oval:
                    snprintf(v, sizeof(v),
                        "%s<v:oval style=\"position:absolute;left:%dpt;top:%dpt;"
                        "width:%dpt;height:%dpt;z-index:-1;"
                        "mso-position-horizontal-relative:page;"
                        "mso-position-vertical-relative:page\" "
                        "fillcolor=\"%s\" strokecolor=\"%s\" strokeweight=\"%.1fpt\"/>"
                        "</w:pict></w:r></w:p>",
                        pPr, (int)sh.x,(int)sh.y,(int)sh.w,(int)sh.h,
                        fillHex,strokeHex,sh.thick);
                    break;
                case SH_Line:
                    snprintf(v, sizeof(v),
                        "%s<v:line style=\"position:absolute;z-index:-1;"
                        "mso-position-horizontal-relative:page;"
                        "mso-position-vertical-relative:page\" "
                        "from=\"%dpt,%dpt\" to=\"%dpt,%dpt\" "
                        "strokecolor=\"%s\" strokeweight=\"%.1fpt\"/>"
                        "</w:pict></w:r></w:p>",
                        pPr, (int)sh.x,(int)sh.y,(int)(sh.x+sh.w),(int)(sh.y+sh.h),
                        strokeHex,sh.thick);
                    break;
                case SH_Triangle:
                    snprintf(v, sizeof(v),
                        "%s<v:shape style=\"position:absolute;left:%dpt;top:%dpt;"
                        "width:%dpt;height:%dpt;z-index:-1;"
                        "mso-position-horizontal-relative:page;"
                        "mso-position-vertical-relative:page\" "
                        "fillcolor=\"%s\" strokecolor=\"%s\" strokeweight=\"%.1fpt\" "
                        "coordsize=\"%d,%d\" path=\"m %d,0 l %d,%d l 0,%d x e\"/>"
                        "</w:pict></w:r></w:p>",
                        pPr, (int)sh.x,(int)sh.y,(int)sh.w,(int)sh.h,
                        fillHex,strokeHex,sh.thick,
                        (int)sh.w,(int)sh.h,
                        (int)(sh.w*0.5f), (int)sh.w,(int)sh.h, (int)sh.h);
                    break;
                }
                shapes += v;
            }
            size_t pos = xml.rfind("<w:sectPr");
            if (pos == std::string::npos) pos = xml.rfind("</w:body>");
            if (pos != std::string::npos) xml.insert(pos, shapes);
        }

        e.data.assign(xml.begin(),xml.end());
        break;
    }

    // ── Patch [Content_Types].xml ─────────────────────────────────────────────
    for(auto& e:entries){
        if(e.name!="[Content_Types].xml") continue;
        std::string xml(e.data.begin(),e.data.end());
        std::string inserts;
        if(!imgList.empty()&&xml.find("image/bmp")==std::string::npos)
            inserts+="<Default Extension=\"bmp\" ContentType=\"image/bmp\"/>";
        if(gDocHeader[0]||gDocFooter[0]){
            if(xml.find("header1.xml")==std::string::npos)
                inserts+="<Override PartName=\"/word/header1.xml\" "
                    "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.header+xml\"/>"
                    "<Override PartName=\"/word/footer1.xml\" "
                    "ContentType=\"application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml\"/>";
        }
        if(!inserts.empty()){
            size_t pos=xml.find("</Types>");
            if(pos!=std::string::npos) xml.insert(pos,inserts);
        }
        e.data.assign(xml.begin(),xml.end());
        break;
    }

    // Build header/footer XML — one <w:p> per line so multi-line blocks survive
    std::string hdrXml, ftrXml;
    if(gDocHeader[0]||gDocFooter[0]){
        std::vector<std::string> hls, fls;
        DocHFTextLines(gDocHeader, 0, hls);      // 0 = keep {page} unexpanded
        DocHFTextLines(gDocFooter, 0, fls);
        hdrXml="<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<w:hdr xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">";
        for(const std::string& ln : hls)
            hdrXml+="<w:p><w:pPr><w:jc w:val=\"left\"/></w:pPr><w:r><w:t xml:space=\"preserve\">"
                   +xmlEsc(ln.c_str())+"</w:t></w:r></w:p>";
        hdrXml+="</w:hdr>";
        ftrXml="<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
            "<w:ftr xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">";
        for(const std::string& ln : fls){
            ftrXml+="<w:p>";
            const char* pg=strstr(ln.c_str(),"{page}");
            if(pg){
                std::string before(ln.c_str(),(size_t)(pg-ln.c_str()));
                std::string after(pg+6);
                if(!before.empty())
                    ftrXml+="<w:r><w:t xml:space=\"preserve\">"+xmlEsc(before.c_str())+"</w:t></w:r>";
                ftrXml+="<w:r><w:fldChar w:fldCharType=\"begin\"/></w:r>"
                        "<w:r><w:instrText xml:space=\"preserve\"> PAGE </w:instrText></w:r>"
                        "<w:r><w:fldChar w:fldCharType=\"end\"/></w:r>";
                if(!after.empty())
                    ftrXml+="<w:r><w:t xml:space=\"preserve\">"+xmlEsc(after.c_str())+"</w:t></w:r>";
            } else {
                ftrXml+="<w:r><w:t xml:space=\"preserve\">"+xmlEsc(ln.c_str())+"</w:t></w:r>";
            }
            ftrXml+="</w:p>";
        }
        ftrXml+="</w:ftr>";
    }

    zip_t* zw=zip_open("/output.docx.tmp",ZIP_DEFAULT_COMPRESSION_LEVEL,'w');
    for(auto& e:entries){
        zip_entry_open(zw,e.name.c_str());
        zip_entry_write(zw,e.data.data(),e.data.size());
        zip_entry_close(zw);
    }
    if(!hdrXml.empty()){
        zip_entry_open(zw,"word/header1.xml");
        zip_entry_write(zw,hdrXml.c_str(),hdrXml.size());
        zip_entry_close(zw);
        zip_entry_open(zw,"word/footer1.xml");
        zip_entry_write(zw,ftrXml.c_str(),ftrXml.size());
        zip_entry_close(zw);
    }
    for(auto& [seq,imgIdx]:imgList){
        if(imgIdx<0||imgIdx>=(int)gDocImages.size()) continue;
        auto bmp=MakeBMP(gDocImages[imgIdx]);
        char path[64]; snprintf(path,sizeof(path),"word/media/image%d.bmp",seq);
        zip_entry_open(zw,path);
        zip_entry_write(zw,bmp.data(),bmp.size());
        zip_entry_close(zw);
    }
    zip_close(zw);
    remove("/output.docx");
    rename("/output.docx.tmp","/output.docx");
}

// ══════════════════════════════════════════════════════════════════════════════
// ══════════════════════════════════════════════════════════════════════════════
// ─── Docx editor: text positions, caret, selection, editing operations ───────
// ══════════════════════════════════════════════════════════════════════════════
static int DocParaLen(int pi)
{
    return (pi >= 0 && pi < (int)gDocParas.size()) ? (int)strlen(gDocParas[pi].text) : 0;
}

static DocPos DocClampPos(DocPos p)
{
    if (gDocParas.empty()) return DocPos{0, 0};
    if (p.para < 0) return DocPos{0, 0};
    if (p.para >= (int)gDocParas.size())
        return DocPos{(int)gDocParas.size() - 1, DocParaLen((int)gDocParas.size() - 1)};
    p.off = std::max(0, std::min(p.off, DocParaLen(p.para)));
    return p;
}

static void DocSelRange(DocPos& a, DocPos& b)
{
    DocPos ca = DocClampPos(gDocCaret);
    DocPos an = gDocSelActive ? DocClampPos(gDocAnchor) : ca;
    if (DocPosLess(ca, an)) { a = ca; b = an; } else { a = an; b = ca; }
}

static bool DocHasSel()
{
    return gDocSelActive && !DocPosEq(gDocCaret, gDocAnchor) && !gDocParas.empty();
}

static void DocTouch()
{
    gDocLayoutDirty = true;
    gDocBlinkT      = ImGui::GetTime();
    gDocWantX       = -1.f;
    gDocSel         = gDocCaret.para;
}

static void DocSetCaret(DocPos p, bool extend)
{
    p = DocClampPos(p);
    if (!extend) {
        gDocAnchor    = p;
        gDocSelActive = false;
        gDocTypingAttrSet = false;        // inherit the style at the new caret
    } else {
        gDocSelActive = !DocPosEq(p, gDocAnchor);
    }
    gDocCaret  = p;
    gDocSel    = p.para;
    gDocBlinkT = ImGui::GetTime();
    if (!extend) gDocWantX = -1.f;
}

static std::string DocSelectedText()
{
    DocPos a, b;
    DocSelRange(a, b);
    if (DocPosEq(a, b) || gDocParas.empty()) return std::string();
    std::string out;
    for (int pi = a.para; pi <= b.para && pi < (int)gDocParas.size(); ++pi) {
        const char* t = gDocParas[pi].text;
        int len = (int)strlen(t);
        int s = (pi == a.para) ? a.off : 0;
        int e = (pi == b.para) ? b.off : len;
        s = std::max(0, std::min(s, len));
        e = std::max(0, std::min(e, len));
        if (e < s) e = s;
        out.append(t + s, (size_t)(e - s));
        if (pi != b.para) out.push_back('\n');
    }
    return out;
}

// ── mutations ────────────────────────────────────────────────────────────────
static void DocDeleteRange(DocPos a, DocPos b)
{
    if (gDocParas.empty()) return;
    a = DocClampPos(a);
    b = DocClampPos(b);
    if (DocPosLess(b, a)) std::swap(a, b);
    if (DocPosEq(a, b)) return;

    if (a.para == b.para) {
        DocPara& p = gDocParas[a.para];
        int len = (int)strlen(p.text);
        DocRunsNorm(p.runs, len, DocBaseAttr(p));
        int ia = DocRunsSplit(p.runs, a.off);
        int ib = DocRunsSplit(p.runs, b.off);
        p.runs.erase(p.runs.begin() + ia, p.runs.begin() + ib);
        memmove(p.text + a.off, p.text + b.off, (size_t)(len - b.off + 1));
        DocRunsNorm(p.runs, (int)strlen(p.text), DocBaseAttr(p));
    } else {
        DocPara& pa = gDocParas[a.para];
        DocPara& pb = gDocParas[b.para];
        // keep the head runs of pa up to a.off, then take the tail runs of pb
        int lenA = (int)strlen(pa.text), lenB = (int)strlen(pb.text);
        DocAttr baseA = DocBaseAttr(pa), baseB = DocBaseAttr(pb);
        DocRunsNorm(pa.runs, lenA, baseA);
        DocRunsNorm(pb.runs, lenB, baseB);
        std::vector<DocRun> head(pa.runs.begin(), pa.runs.begin() + DocRunsSplit(pa.runs, a.off));
        int ib = DocRunsSplit(pb.runs, b.off);
        std::vector<DocRun> tail(pb.runs.begin() + ib, pb.runs.end());
        std::string tailText(pb.text + b.off);

        size_t used = (size_t)a.off;
        size_t room = sizeof(pa.text) - 1 - used;
        if (tailText.size() > room) tailText.resize(room);
        pa.text[a.off] = 0;
        if (!tailText.empty()) memcpy(pa.text + used, tailText.data(), tailText.size());
        pa.text[used + tailText.size()] = 0;

        pa.runs.clear();
        pa.runs.insert(pa.runs.end(), head.begin(), head.end());
        pa.runs.insert(pa.runs.end(), tail.begin(), tail.end());
        DocRunsNorm(pa.runs, (int)strlen(pa.text), baseA);

        gDocParas.erase(gDocParas.begin() + a.para + 1, gDocParas.begin() + b.para + 1);
    }
    gDocCaret = a;
    gDocAnchor = a;
    gDocSelActive = false;
    gDocTypingAttrSet = false;
    DocTouch();
}

static void DocSplitPara(DocPos at)
{
    if (gDocParas.empty()) return;
    at = DocClampPos(at);
    DocPara& p = gDocParas[at.para];
    int len = (int)strlen(p.text);
    if (at.off > len) at.off = len;

    DocAttr base = DocBaseAttr(p);
    DocRunsNorm(p.runs, len, base);
    std::vector<DocRun> left(p.runs.begin(), p.runs.begin() + DocRunsSplit(p.runs, at.off));
    std::vector<DocRun> right(p.runs.begin() + DocRunsSplit(p.runs, at.off), p.runs.end());

    DocPara np = p;
    np.imageIdx      = -1;
    np.imageWidthPct = 100.f;
    np.imageLayout   = IL_Inline;

    size_t tail = (size_t)(len - at.off);
    if (tail > sizeof(np.text) - 1) tail = sizeof(np.text) - 1;
    memcpy(np.text, p.text + at.off, tail);
    np.text[tail] = 0;
    p.text[at.off] = 0;

    p.runs = left;
    np.runs = right;
    DocRunsNorm(p.runs, (int)strlen(p.text), base);
    DocRunsNorm(np.runs, (int)strlen(np.text), base);

    gDocParas.insert(gDocParas.begin() + at.para + 1, np);
    gDocCaret  = DocPos{at.para + 1, 0};
    gDocAnchor = gDocCaret;
    gDocSelActive = false;
    gDocTypingAttrSet = false;
    DocTouch();
}

// Insert text at `at`; '\n' starts new paragraphs.  `attr` styles the new text.
static void DocInsertRaw(DocPos at, const std::string& txt, const DocAttr& attr)
{
    if (gDocParas.empty()) gDocParas.push_back(DocPara{});
    at = DocClampPos(at);

    std::vector<std::string> parts(1);
    for (size_t i = 0; i < txt.size(); ++i) {
        char c = txt[i];
        if (c == '\n')      parts.push_back(std::string());
        else if (c != '\r') parts.back().push_back(c);
    }

    DocPara& p = gDocParas[at.para];
    int len = (int)strlen(p.text);
    if (at.off > len) at.off = len;
    std::string tail(p.text + at.off);
    p.text[at.off] = 0;

    // runs: keep head, add the new run, and let the tail keep its own attr
    DocAttr base = DocBaseAttr(p);
    DocRunsNorm(p.runs, len, base);
    std::vector<DocRun> head(p.runs.begin(), p.runs.begin() + DocRunsSplit(p.runs, at.off));
    std::vector<DocRun> rest(p.runs.begin() + DocRunsSplit(p.runs, at.off), p.runs.end());

    auto appendText = [](DocPara& dp, const std::string& s) {
        size_t cur  = strlen(dp.text);
        size_t room = sizeof(dp.text) - 1 - cur;
        size_t n    = std::min(room, s.size());
        if (n) memcpy(dp.text + cur, s.data(), n);
        dp.text[cur + n] = 0;
        return (int)n;
    };

    int added0 = appendText(p, parts[0]);
    p.runs.clear();
    p.runs.insert(p.runs.end(), head.begin(), head.end());
    if (added0 > 0) { DocRun r; r.len = added0; r.a = attr; p.runs.push_back(r); }
    p.runs.insert(p.runs.end(), rest.begin(), rest.end());
    DocRunsNorm(p.runs, (int)strlen(p.text), base);

    int pi = at.para;
    for (size_t i = 1; i < parts.size(); ++i) {
        DocPara np = p;
        np.text[0]       = 0;
        np.imageIdx      = -1;
        np.imageWidthPct = 100.f;
        np.imageLayout   = IL_Inline;
        strncpy(np.text, parts[i].c_str(), sizeof(np.text) - 1);
        np.text[sizeof(np.text) - 1] = 0;
        std::vector<DocRun> nr;
        if (np.text[0]) { DocRun r; r.len = (int)strlen(np.text); r.a = attr; nr.push_back(r); }
        np.runs = nr;
        ++pi;
        gDocParas.insert(gDocParas.begin() + pi, np);
    }

    DocPara& lastPara = gDocParas[pi];
    int lastLen0 = (int)strlen(lastPara.text);
    int tailAdded = appendText(lastPara, tail);
    // the trailing text keeps the style it had before the insertion
    if (tailAdded > 0) {
        DocRunsNorm(lastPara.runs, lastLen0, base);
        lastPara.runs.insert(lastPara.runs.end(), rest.begin(), rest.end());
        DocRunsNorm(lastPara.runs, (int)strlen(lastPara.text), base);
    }

    DocPos caret{pi, (int)strlen(lastPara.text) - (int)tail.size()};
    if (caret.off < 0) caret.off = 0;
    gDocCaret  = caret;
    gDocAnchor = caret;
    gDocSelActive = false;
    DocTouch();
}

static int DocWordStart(const char* t, int off)
{
    int n = (int)strlen(t);
    off = std::max(0, std::min(off, n));
    int i = off;
    while (i > 0) {
        int j = dt::U8Prev(t, i);
        if (!dt::U8IsWordChar(dt::U8Decode(t, j, n))) break;
        i = j;
    }
    return i;
}

static int DocWordEnd(const char* t, int off)
{
    int n = (int)strlen(t);
    off = std::max(0, std::min(off, n));
    int i = off;
    while (i < n) {
        if (!dt::U8IsWordChar(dt::U8Decode(t, i, n))) break;
        i += dt::U8Seq(t, i, n);
    }
    return i;
}

// Attribute that typing should use at the caret.
static DocAttr DocTypingAttrNow()
{
    if (gDocTypingAttrSet) return gDocTypingAttr;
    if (gDocParas.empty()) return DocAttr{};
    const DocPara& p = gDocParas[gDocCaret.para];
    int off = gDocCaret.off;
    if (off > 0) return DocAttrAt(p, off - 1);
    if ((int)strlen(p.text) > 0) return DocAttrAt(p, 0);
    return DocBaseAttr(p);
}

// Apply a character-formatting operation to the selection (or, when the
// selection is empty, to the attribute used by the next typed characters).
enum { OP_BOLD = 0, OP_ITALIC, OP_UNDERLINE, OP_STRIKE, OP_SUP, OP_SUB, OP_COLOR, OP_HL,
       OP_FAMILY, OP_SIZE };

static void DocApplyOp(int op, ImVec4 v = ImVec4(0.f, 0.f, 0.f, 1.f))
{
    if (gDocParas.empty()) return;
    DocPos a, b;
    DocSelRange(a, b);
    const bool collapsed = DocPosEq(a, b);

    if (collapsed) {
        DocAttr at = DocTypingAttrNow();
        bool all = false;
        switch (op) {
            case OP_BOLD:      all = at.bold; break;
            case OP_ITALIC:    all = at.italic; break;
            case OP_UNDERLINE: all = at.underline; break;
            case OP_STRIKE:    all = at.strike; break;
            case OP_SUP:       all = (at.vert > 0); break;
            case OP_SUB:       all = (at.vert < 0); break;
            case OP_COLOR:     all = at.hasColor && fabsf(at.color.x-v.x)<0.004f && fabsf(at.color.y-v.y)<0.004f && fabsf(at.color.z-v.z)<0.004f; break;
            case OP_HL:        all = at.hasHL && fabsf(at.hl.x-v.x)<0.004f && fabsf(at.hl.y-v.y)<0.004f && fabsf(at.hl.z-v.z)<0.004f; break;
            case OP_FAMILY:    all = (at.family == (int)v.x); break;
            case OP_SIZE:      all = (fabsf(at.fsPt - v.x) < 0.01f); break;
        }
        DocAttrApplyOp(at, op, !all, v);
        gDocTypingAttr    = at;
        gDocTypingAttrSet = true;
        gDocBlinkT        = ImGui::GetTime();
        return;
    }

    DocPushSnap();
    for (int pi = a.para; pi <= b.para && pi < (int)gDocParas.size(); ++pi) {
        DocPara& p = gDocParas[pi];
        int len = (int)strlen(p.text);
        int s = (pi == a.para) ? a.off : 0;
        int e = (pi == b.para) ? b.off : len;
        if (e <= s) continue;
        bool all = DocRangeAllHas(p, s, e, op, v);
        DocAttr at = DocAttrAt(p, s);
        DocAttrApplyOp(at, op, !all, v);
        DocSetAttrRange(p, s, e, at);
    }
    // typing continues in the new style
    gDocTypingAttr    = DocAttrAt(gDocParas[b.para], std::max(0, b.off - 1));
    gDocTypingAttrSet = true;
    DocTouch();
}

// "Chapter ......... 12" with the dots filled to the current text column.
static std::string DocIndexLine(const std::string& label, int page)
{
    float fs = kDocBaseFS;
    ImFont* f = gDocFonts[0].regular ? gDocFonts[0].regular : ImGui::GetFont();
    float scale = (gDocPrevDocScale > 0.05f) ? gDocPrevDocScale : 1.f;
    float width = (kDocPageW - kDocML - kDocMR) * scale;
    float wL = f->CalcTextSizeA(fs, FLT_MAX, 0.f, label.c_str()).x;
    char num[16]; snprintf(num, sizeof(num), "%d", page);
    float wN = f->CalcTextSizeA(fs, FLT_MAX, 0.f, num).x;
    float wDot = std::max(1.f, f->CalcTextSizeA(fs, FLT_MAX, 0.f, ".").x);
    int n = (int)floorf((width - wL - wN - 12.f) / wDot);
    if (n < 3) n = 3;
    if (n > 400) n = 400;
    return label + " " + std::string((size_t)n, '.') + " " + num;
}

// kind: 1 = table of contents, 2 = list of figures, 3 = list of tables.
static void DocInsertIndex(int kind)
{
    DocPushSnap();
    std::vector<DocPara> out;
    const char* title = (kind == 1) ? "Contents"
                      : (kind == 2) ? "List of Figures" : "List of Tables";
    {
        DocPara h;
        strncpy(h.text, title, sizeof(h.text) - 1);
        h.style = DS_H2;
        h.indexEntry = kind;
        out.push_back(h);
    }
    int counter = 0;
    for (int pi = 0; pi < (int)gDocParas.size(); ++pi) {
        const DocPara& p = gDocParas[pi];
        if (p.indexEntry) continue;                 // never index the index
        std::string label;
        if (kind == 1) {
            if (!(p.style == DS_H1 || p.style == DS_H2 || p.style == DS_H3)) continue;
            int lvl = (p.style == DS_H2) ? 1 : (p.style == DS_H3) ? 2 : 0;
            label = std::string((size_t)lvl * 3, ' ') + std::string(p.text);
        } else if (kind == 2) {
            if (p.imageIdx < 0 || !p.caption[0]) continue;
            char b[300];
            snprintf(b, sizeof(b), "Figure %d: %s", ++counter, p.caption);
            label = b;
        } else {
            if (p.tableIdx < 0 || !p.caption[0]) continue;
            char b[300];
            snprintf(b, sizeof(b), "Table %d: %s", ++counter, p.caption);
            label = b;
        }
        int page = (pi < (int)gDocLayout.size()) ? gDocLayout[pi].page + 1 : 1;
        DocPara e;
        std::string line = DocIndexLine(label, page);
        strncpy(e.text, line.c_str(), sizeof(e.text) - 1);
        e.indexEntry = kind;
        out.push_back(e);
    }

    // replace an existing block of this kind in place, else insert at the caret
    std::vector<DocPara> keep;
    int at = -1;
    for (int i = 0; i < (int)gDocParas.size(); ++i) {
        if (gDocParas[i].indexEntry == kind) { if (at < 0) at = (int)keep.size(); continue; }
        keep.push_back(gDocParas[i]);
    }
    if (at < 0) at = (gDocSel >= 0) ? gDocSel + 1 : (int)keep.size();
    if (at > (int)keep.size()) at = (int)keep.size();
    keep.insert(keep.begin() + at, out.begin(), out.end());
    gDocParas.swap(keep);
    DocSetCaret(DocPos{at, 0}, false);
    gDocLayoutDirty = true;
}

static void DocRefreshIndexes()
{
    for (int kind = 1; kind <= 3; ++kind) {
        bool has = false;
        for (size_t i = 0; i < gDocParas.size(); ++i)
            if (gDocParas[i].indexEntry == kind) { has = true; break; }
        if (has) DocInsertIndex(kind);
    }
}

// Word-style AutoFormat: a paragraph that is only "---" / "***" / "___" becomes
// a horizontal rule, "..." becomes a dot leader.
static bool DocAutoFormatLine()
{
    if (gDocParas.empty()) return false;
    DocPara& cur = gDocParas[gDocCaret.para];
    int rule = 0;
    if (!strcmp(cur.text, "---") || !strcmp(cur.text, "***") || !strcmp(cur.text, "___")) rule = 1;
    else if (!strcmp(cur.text, "...") || !strcmp(cur.text, "···")) rule = 2;
    if (!rule) return false;
    cur.divider = rule;
    cur.text[0] = 0;
    cur.runs.clear();
    DocPara np;
    np.align = cur.align;
    np.style = cur.style;
    gDocParas.insert(gDocParas.begin() + gDocCaret.para + 1, np);
    DocSetCaret(DocPos{gDocCaret.para + 1, 0}, false);
    gDocLayoutDirty = true;
    return true;
}

// Insert a table as its own paragraph after the caret paragraph.
static void DocInsertTable(int rows, int cols)
{
    if (rows < 1 || cols < 1) return;
    DocPushSnap();
    DocTable t;
    t.rows = rows;
    t.cols = cols;
    t.header = gDocTableHeader;
    t.cells.assign((size_t)rows * cols, std::string());
    int idx = (int)gDocTables.size();
    gDocTables.push_back(t);
    DocPara np;
    np.tableIdx = idx;
    int ins = (gDocSel >= 0) ? gDocSel + 1 : (int)gDocParas.size();
    gDocParas.insert(gDocParas.begin() + ins, np);
    DocSetCaret(DocPos{ins, 0}, false);
    gDocLayoutDirty = true;
}

// Turn the selected paragraphs into a bulleted / numbered list (or clear it).
static void DocApplyList(int type)
{
    if (gDocParas.empty()) return;
    DocPos a, b;
    DocSelRange(a, b);
    bool all = true;
    for (int i = a.para; i <= b.para; ++i) if (gDocParas[i].list != type) { all = false; break; }
    DocPushSnap();
    for (int i = a.para; i <= b.para; ++i) {
        gDocParas[i].list = all ? 0 : type;
        if (gDocParas[i].list) gDocParas[i].imageIdx = gDocParas[i].imageIdx;   // keep images as-is
    }
    DocTouch();
}

static void DocCopySelection(bool cut)
{
    if (!DocHasSel()) return;
    DocPos a, b;
    DocSelRange(a, b);
    gDocClipText.clear();
    gDocClipParas.clear();
    for (int pi = a.para; pi <= b.para; ++pi) {
        DocPara p = gDocParas[pi];
        int len = (int)strlen(p.text);
        int s = (pi == a.para) ? a.off : 0;
        int e = (pi == b.para) ? b.off : len;
        s = std::max(0, std::min(s, len));
        e = std::max(0, std::min(e, len));
        if (e < s) e = s;
        DocAttr base = DocBaseAttr(p);
        DocRunsNorm(p.runs, len, base);
        int ia = DocRunsSplit(p.runs, s);
        int ib = DocRunsSplit(p.runs, e);
        std::vector<DocRun> cutRuns(p.runs.begin() + ia, p.runs.begin() + ib);
        std::string txt(p.text + s, (size_t)(e - s));
        DocPara part = p;
        strncpy(part.text, txt.c_str(), sizeof(part.text) - 1);
        part.text[sizeof(part.text) - 1] = 0;
        part.runs = cutRuns;
        DocRunsNorm(part.runs, (int)strlen(part.text), base);
        gDocClipParas.push_back(part);
    }
    gDocClipText = DocSelectedText();
#ifdef __EMSCRIPTEN__
    if (!gDocClipText.empty())
        EM_ASM({ if (navigator.clipboard && navigator.clipboard.writeText)
                    navigator.clipboard.writeText(UTF8ToString($0)).catch(function(){}); },
               gDocClipText.c_str());
#endif
    if (cut) {
        DocPushSnap();
        DocDeleteRange(a, b);
    }
}

static void DocPasteClipboard()
{
    if (gDocClipParas.empty() && gDocClipText.empty()) return;
    DocPos a, b;
    DocSelRange(a, b);
    const bool had = DocHasSel();
    DocPushSnap();
    if (had) { DocDeleteRange(a, b); a = gDocCaret; b = gDocCaret; }

    if (!gDocClipParas.empty()) {
        for (size_t i = 0; i < gDocClipParas.size(); ++i) {
            if (i > 0) DocSplitPara(gDocCaret);
            const DocPara& src = gDocClipParas[i];
            if (src.tableIdx >= 0 && src.tableIdx < (int)gDocTables.size()) {
                // clone the table so the copy is independent
                DocTable copy = gDocTables[src.tableIdx];
                int newIdx = (int)gDocTables.size();
                gDocTables.push_back(copy);
                DocPara& dst = gDocParas[gDocCaret.para];
                dst.tableIdx = newIdx;
                dst.text[0] = 0;
                dst.runs.clear();
                gDocCaret.off = 0;
                gDocAnchor = gDocCaret;
                gDocSelActive = false;
                gDocLayoutDirty = true;
                continue;
            }
            size_t room = (size_t)gDocImages.size();
            (void)room;
            // insert each run with its own attribute so formatting survives
            DocAttr base = DocBaseAttr(src);
            std::vector<DocRun> runs = src.runs;
            DocRunsNorm(runs, (int)strlen(src.text), base);
            int acc = 0;
            for (size_t r = 0; r < runs.size(); ++r) {
                std::string seg(src.text + acc, (size_t)runs[r].len);
                acc += runs[r].len;
                if (!seg.empty()) DocInsertRaw(gDocCaret, seg, runs[r].a);
            }
        }
        DocTouch();
    } else {
        DocInsertRaw(gDocCaret, gDocClipText, DocTypingAttrNow());
    }
}

// ── view geometry: screen position of paragraphs, hit-testing ────────────────
struct DocView {
    ImVec2 winPos{0, 0};
    float  scrollY   = 0.f;
    float  pageLeft  = 0.f;
    float  pageW     = 0.f;
    float  pageH     = 0.f;
    float  pageGap   = 0.f;
    float  docScale  = 1.f;
    float  contentW  = 0.f;
    float  canvasW   = 0.f;
    int    pageCount = 1;
    bool   valid     = false;
};
static DocView gDocView;

static ImVec2 DocParaOrigin(int pi)
{
    if (pi < 0 || pi >= (int)gDocLayout.size()) return gDocView.winPos;
    const DocParaLayout& lo = gDocLayout[pi];
    DocPageSetup st = DocPageSetupAt(lo.page);
    float pw   = DocPageSizeOf(st).x * gDocView.docScale;
    float left = (gDocView.canvasW > 0.f) ? (gDocView.canvasW - pw) * 0.5f : gDocView.pageLeft;
    return ImVec2(gDocView.winPos.x + left + st.ml * gDocView.docScale + lo.xOff + lo.indent,
                  gDocView.winPos.y + DocPageTopPx(lo.page, gDocView.docScale) + lo.yStart - gDocView.scrollY);
}

// page currently under the middle of the viewport (used by the page-setup panel)
static int DocVisiblePage()
{
    if (gDocPageTops.empty()) return 0;
    float target = gDocView.scrollY + 260.f;
    int best = 0;
    float bestDist = 1e9f;
    for (int pg = 0; pg < (int)gDocPageTops.size(); ++pg) {
        float top = DocPageTopPx(pg, gDocView.docScale);
        float h   = DocPageSizeOf(DocPageSetupAt(pg)).y * gDocView.docScale;
        if (target >= top && target <= top + h) return pg;
        float d = fabsf(target - (top + h * 0.5f));
        if (d < bestDist) { bestDist = d; best = pg; }
    }
    return best;
}

static DocPos DocHitTest(ImVec2 m)
{
    DocPos fallback = DocClampPos(gDocCaret);
    if (gDocParas.empty()) return DocPos{0, 0};
    for (int pi = 0; pi < (int)gDocParas.size(); ++pi) {
        if (pi >= (int)gDocLayout.size() || pi >= (int)gDocLines.size()) break;
        const DocParaLayout& lo = gDocLayout[pi];
        ImVec2 o = DocParaOrigin(pi);
        float h = lo.yEnd - lo.yStart;
        if (gDocLines[pi].lineH > 0.f) h = std::max(h, gDocLines[pi].h);
        if (h <= 0.f) h = kDocBaseFS;
        if (m.y < o.y) return DocPos{pi, 0};
        if (m.y <= o.y + h) {
            const DocPara& dp = gDocParas[pi];
            float cwp = DocContentWAt(lo.page, gDocView.docScale);
            int off = dt::OffsetAtPoint(gDocLines[pi], dp.text, m.x - o.x, m.y - o.y, nullptr,
                                        (int)dp.align, cwp);
            return DocPos{pi, off};
        }
        fallback = DocPos{pi, DocParaLen(pi)};
    }
    return fallback;
}
// ══════════════════════════════════════════════════════════════════════════════
// ─── Export helpers (docx runs + PDF text) ───────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════

// Nearest OOXML highlight name for an RGB colour (Word only knows 16 names).
static const char* DocHlName(const ImVec4& c)
{
    static const struct { const char* name; int r, g, b; } kMap[] = {
        {"yellow",    255, 255,   0}, {"green",       0, 255,   0},
        {"cyan",        0, 255, 255}, {"magenta",   255,   0, 255},
        {"blue",        0,   0, 255}, {"red",       255,   0,   0},
        {"darkBlue",    0,   0, 128}, {"darkCyan",    0, 128, 128},
        {"darkGreen",   0, 128,   0}, {"darkMagenta",128,   0, 128},
        {"darkRed",   128,   0,   0}, {"darkYellow",128, 128,   0},
        {"darkGray",  128, 128, 128}, {"lightGray", 192, 192, 192},
        {"black",       0,   0,   0}, {"white",     255, 255, 255},
    };
    int r = (int)(c.x * 255.f + 0.5f), g = (int)(c.y * 255.f + 0.5f), b = (int)(c.z * 255.f + 0.5f);
    const char* best = "yellow";
    int bestD = 1 << 30;
    for (int i = 0; i < 16; ++i) {
        int dr = r - kMap[i].r, dg = g - kMap[i].g, db = b - kMap[i].b;
        int d = dr * dr + dg * dg + db * db;
        if (d < bestD) { bestD = d; best = kMap[i].name; }
    }
    return best;
}

static float DocStylePt(DocStyle s)
{
    switch (s) {
        case DS_H1:    return 20.f;
        case DS_H2:    return 15.f;
        case DS_H3:    return 12.5f;
        case DS_Code:  return 10.f;
        default:       return 11.f;
    }
}

static int listNumFor(int pi)
{
    return (pi >= 0 && pi < (int)gDocListNum.size()) ? gDocListNum[pi] : 1;
}


// ══════════════════════════════════════════════════════════════════════════════
// ─── DOCX import: Office Open XML → document model ───────────────────────────
// The editor writes .docx with duckx, but duckx cannot *read* formatting, so a
// file produced by Word / LibreOffice / Google Docs is parsed directly out of
// the package: word/document.xml is unzipped and walked with pugixml, turning
// w:p into DocPara (runs, style, alignment, numbering) and w:tbl into DocTable.
// ══════════════════════════════════════════════════════════════════════════════

static std::string DocxAttr(pugi::xml_node n, const char* name)
{
    if (!n) return std::string();
    const char* v = n.attribute(name).value();
    return v ? std::string(v) : std::string();
}

static std::string DocxLower(std::string v)
{
    for (char& c : v) c = (char)tolower((unsigned char)c);
    return v;
}

// <w:b/> means "on"; <w:b w:val="0"/> means off.
static bool DocxToggle(pugi::xml_node n)
{
    if (!n) return false;
    std::string v = DocxLower(DocxAttr(n, "w:val"));
    return !(v == "0" || v == "false" || v == "off" || v == "none");
}

static ImVec4 DocxHex(const std::string& hex, ImVec4 fallback)
{
    if (hex.size() != 6) return fallback;
    auto h = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    float r = (h(hex[0]) * 16 + h(hex[1])) / 255.f;
    float g = (h(hex[2]) * 16 + h(hex[3])) / 255.f;
    float b = (h(hex[4]) * 16 + h(hex[5])) / 255.f;
    return ImVec4(r, g, b, 1.f);
}

// Word highlight colours (w:highlight w:val="yellow" …).
static ImVec4 DocxHighlight(const std::string& name)
{
    std::string v = DocxLower(name);
    if (v == "yellow")       return {1.00f, 0.95f, 0.35f, 1.f};
    if (v == "green")        return {0.55f, 0.95f, 0.55f, 1.f};
    if (v == "cyan")         return {0.55f, 0.95f, 0.95f, 1.f};
    if (v == "magenta")      return {1.00f, 0.65f, 0.95f, 1.f};
    if (v == "blue")         return {0.62f, 0.80f, 1.00f, 1.f};
    if (v == "red")          return {1.00f, 0.62f, 0.62f, 1.f};
    if (v == "darkblue")     return {0.35f, 0.45f, 0.85f, 1.f};
    if (v == "darkcyan")     return {0.30f, 0.70f, 0.75f, 1.f};
    if (v == "darkgreen")    return {0.35f, 0.70f, 0.35f, 1.f};
    if (v == "darkmagenta")  return {0.70f, 0.40f, 0.70f, 1.f};
    if (v == "darkred")      return {0.75f, 0.30f, 0.30f, 1.f};
    if (v == "darkyellow")   return {0.75f, 0.70f, 0.25f, 1.f};
    if (v == "gray" || v == "lightgray") return {0.80f, 0.80f, 0.80f, 1.f};
    if (v == "black")        return {0.25f, 0.25f, 0.25f, 1.f};
    return {1.00f, 0.95f, 0.35f, 1.f};
}

static DocStyle DocxStyleFromId(const std::string& id)
{
    std::string v = DocxLower(id);
    if (v.empty()) return DS_Normal;
    if (v == "title" || v == "heading1" || v == "heading 1" || v == "h1") return DS_H1;
    if (v == "heading2" || v == "heading 2" || v == "subtitle" || v == "h2") return DS_H2;
    if (v == "heading3" || v == "heading 3" || v == "h3") return DS_H3;
    if (v.find("quote")    != std::string::npos) return DS_Quote;
    if (v.find("code")     != std::string::npos) return DS_Code;
    if (v.find("htmlpre")  != std::string::npos) return DS_Code;
    return DS_Normal;
}

static DocAttr DocxRunAttr(pugi::xml_node run)
{
    DocAttr a;
    pugi::xml_node rPr = run.child("w:rPr");
    if (!rPr) return a;
    a.bold      = DocxToggle(rPr.child("w:b"));
    a.italic    = DocxToggle(rPr.child("w:i"));
    a.underline = rPr.child("w:u") != nullptr;
    a.strike    = DocxToggle(rPr.child("w:strike")) || DocxToggle(rPr.child("w:dstrike"));

    std::string va = DocxLower(DocxAttr(rPr.child("w:vertAlign"), "w:val"));
    if (va == "superscript")      a.vert =  1;
    else if (va == "subscript")   a.vert = -1;

    pugi::xml_node sz = rPr.child("w:sz");
    if (sz) {
        float half = (float)atof(DocxAttr(sz, "w:val").c_str());
        if (half > 0.f) a.fsPt = half / 2.f;
    }

    pugi::xml_node col = rPr.child("w:color");
    if (col) {
        std::string hex = DocxAttr(col, "w:val");
        if (hex.size() == 6 && DocxLower(hex) != "auto") {
            a.color    = DocxHex(hex, ImVec4(0,0,0,1));
            a.hasColor = true;
        }
        pugi::xml_node theme = col.child("w:themeColor");
        (void)theme;   // theme colours resolve through the styles part; keep raw RGB.
    }

    pugi::xml_node hl = rPr.child("w:highlight");
    if (hl) {
        a.hl      = DocxHighlight(DocxAttr(hl, "w:val"));
        a.hasHL   = true;
    }

    // Run font: w:rFonts/@w:ascii is the face Word would use. Map it onto the
    // embedded families (the metric-compatible replacement first), so a Word file
    // asking for Times New Roman opens in Liberation Serif instead of DejaVu Sans.
    if (pugi::xml_node rf = rPr.child("w:rFonts")) {
        std::string name = DocxAttr(rf, "w:ascii");
        if (name.empty()) name = DocxAttr(rf, "w:hAnsi");
        if (name.empty()) name = DocxAttr(rf, "w:cs");
        if (!name.empty()) { a.family = DocFamilyFromName(name); a.hasFamily = true; }
    }
    return a;
}

// Walk the inline children of a paragraph: w:r runs, plus the wrappers Word
// puts around them (hyperlinks, tracked insertions, content controls).
static void DocxCollectInline(pugi::xml_node node, DocPara& p, int& used)
{
    for (pugi::xml_node c = node.first_child(); c; c = c.next_sibling()) {
        std::string name = c.name();
        if (name == "w:r") {
            DocAttr a = DocxRunAttr(c);
            std::string txt;
            for (pugi::xml_node t = c.first_child(); t; t = t.next_sibling()) {
                std::string tn = t.name();
                if (tn == "w:t")               txt += t.text().get();
                else if (tn == "w:tab")        txt += "    ";
                else if (tn == "w:br")         txt += " ";
                else if (tn == "w:cr")         txt += " ";
                else if (tn == "w:noBreakHyphen") txt += "-";
            }
            if (txt.empty()) continue;
            int room = (int)sizeof(p.text) - 1 - used;
            if (room <= 0) break;
            if ((int)txt.size() > room) txt.resize((size_t)room);
            memcpy(p.text + used, txt.data(), txt.size());
            DocRun r;
            r.len = (int)txt.size();
            r.a   = a;
            p.runs.push_back(r);
            used += (int)txt.size();
        }
        else if (name == "w:hyperlink" || name == "w:ins" || name == "w:smartTag" ||
                 name == "w:sdtContent" || name == "w:fldSimple" || name == "w:customXml" ||
                 name == "w:moveTo" || name == "w:moveFrom") {
            DocxCollectInline(c, p, used);
        }
        else if (name == "w:sdt") {
            DocxCollectInline(c.child("w:sdtContent"), p, used);
        }
    }
    p.text[used] = '\0';
}

static int DocxCellText(pugi::xml_node tc, std::string& out)
{
    int chars = 0;
    for (pugi::xml_node p = tc.child("w:p"); p; p = p.next_sibling("w:p")) {
        DocPara tmp;
        int used = 0;
        DocxCollectInline(p, tmp, used);
        if (used > 0) {
            if (!out.empty()) out += " ";
            out.append(tmp.text, (size_t)used);
            chars += used;
        }
    }
    return chars;
}

// Paragraph styles from styles.xml (Word keeps jc / ind / spacing / run weight
// there, not on the paragraph), resolved through w:basedOn chains.
struct DocxStyleDef {
    std::string basedOn;
    int   jc = -1;                 // 0 left, 1 centre, 2 right, 3 justify, -1 inherit
    float firstIndent = 0.f;       // pt
    float before = -1.f, after = -1.f;
    float line = 0.f;              // multiplier
    int   bold = -1;               // -1 inherit, 0 off, 1 on
    float sz   = 0.f;              // pt, 0 = inherit
    int   family = -1;             // kDocFamilyName index, -1 = inherit
};

// w:docDefaults -- the document's own base typography.
static float gDocxDefaultFs  = 0.f;
static int   gDocxDefaultFam = -1;
static float gDocxDefaultAfter = -1.f;
static float gDocxDefaultLine  = 0.f;
static std::map<std::string, DocxStyleDef> gDocxStyles;
// The paragraph style marked w:default="1" (usually "Normal"): every paragraph
// without a w:pStyle inherits from it, including its size and face.
static std::string gDocxDefaultStyleId;

static void DocxReadSectPr(pugi::xml_node sect)
{
    DocxSection sec;
    if (pugi::xml_node ty = sect.child("w:type")) {
        std::string v = DocxLower(DocxAttr(ty, "w:val"));
        // "continuous" keeps the following section on the same page (an abstract
        // over a two-column body is the classic case); everything else starts a page.
        sec.continuous = (v == "continuous");
    }
    if (pugi::xml_node cols = sect.child("w:cols")) {
        std::string num = DocxAttr(cols, "w:num");
        std::string sp  = DocxAttr(cols, "w:space");
        if (!num.empty()) sec.cols = std::max(1, atoi(num.c_str()));
        if (!sp.empty())  sec.gap  = (float)atof(sp.c_str()) / 20.f;
    }
    gDocxSections.push_back(sec);
    gDocxSectionCur = sec;
}

static void DocxLoadStyles(const std::string& xml)
{
    pugi::xml_document d;
    if (!d.load_buffer(xml.data(), xml.size())) return;

    // docDefaults first: Normal very often inherits its size and face from here.
    if (pugi::xml_node dd = d.child("w:styles").child("w:docDefaults")) {
        pugi::xml_node rp = dd.child("w:rPrDefault").child("w:rPr");
        if (pugi::xml_node sz = rp.child("w:sz")) {
            float half = (float)atof(DocxAttr(sz, "w:val").c_str());
            if (half > 0.f) gDocxDefaultFs = half / 2.f;
        }
        if (pugi::xml_node rf = rp.child("w:rFonts")) {
            std::string nm = DocxAttr(rf, "w:ascii");
            if (nm.empty()) nm = DocxAttr(rf, "w:hAnsi");
            if (!nm.empty()) gDocxDefaultFam = DocFamilyFromName(nm);
        }
        pugi::xml_node pp = dd.child("w:pPrDefault").child("w:pPr");
        if (pugi::xml_node sp = pp.child("w:spacing")) {
            std::string a = DocxAttr(sp, "w:after"), l = DocxAttr(sp, "w:line"),
                        r = DocxLower(DocxAttr(sp, "w:lineRule"));
            if (!a.empty()) gDocxDefaultAfter = (float)atof(a.c_str()) / 20.f;
            if (!l.empty() && (r.empty() || r == "auto"))
                gDocxDefaultLine = (float)atof(l.c_str()) / 240.f;
        }
    }

    for (pugi::xml_node st = d.child("w:styles").child("w:style"); st;
         st = st.next_sibling("w:style")) {
        std::string type = DocxLower(DocxAttr(st, "w:type"));
        if (!type.empty() && type != "paragraph" && type != "character") continue;
        std::string id = DocxAttr(st, "w:styleId");
        if (id.empty()) continue;
        if (DocxAttr(st, "w:default") == "1" && type == "paragraph")
            gDocxDefaultStyleId = id;
        DocxStyleDef def;
        def.basedOn = DocxAttr(st.child("w:basedOn"), "w:val");
        pugi::xml_node pPr = st.child("w:pPr");
        if (pPr) {
            std::string jc = DocxLower(DocxAttr(pPr.child("w:jc"), "w:val"));
            if (jc == "both" || jc == "distribute") def.jc = 3;
            else if (jc == "center") def.jc = 1;
            else if (jc == "right" || jc == "end") def.jc = 2;
            else if (jc == "left" || jc == "start") def.jc = 0;
            if (pugi::xml_node ind = pPr.child("w:ind")) {
                auto tw = [&](const char* a) {
                    std::string v = DocxAttr(ind, a);
                    return v.empty() ? 0.f : (float)atof(v.c_str()) / 20.f;
                };
                float fl = tw("w:firstLine"), hg = tw("w:hanging");
                if (fl > 0.f) def.firstIndent = fl;
                else if (hg > 0.f) def.firstIndent = -hg;
            }
            if (pugi::xml_node sp = pPr.child("w:spacing")) {
                std::string b = DocxAttr(sp, "w:before"), a = DocxAttr(sp, "w:after"),
                            l = DocxAttr(sp, "w:line");
                if (!b.empty()) def.before = (float)atof(b.c_str()) / 20.f;
                if (!a.empty()) def.after  = (float)atof(a.c_str()) / 20.f;
                if (!l.empty()) { float lv = (float)atof(l.c_str()); if (lv > 0.f) def.line = lv / 240.f; }
            }
        }
        pugi::xml_node rPr = st.child("w:rPr");
        if (rPr) {
            def.bold = DocxToggle(rPr.child("w:b")) ? 1 : 0;
            if (pugi::xml_node sz = rPr.child("w:sz")) {
                float half = (float)atof(DocxAttr(sz, "w:val").c_str());
                if (half > 0.f) def.sz = half / 2.f;
            }
            if (pugi::xml_node rf = rPr.child("w:rFonts")) {
                std::string nm = DocxAttr(rf, "w:ascii");
                if (nm.empty()) nm = DocxAttr(rf, "w:hAnsi");
                if (!nm.empty()) def.family = DocFamilyFromName(nm);
            }
        }
        gDocxStyles[id] = def;
    }
}

static DocxStyleDef DocxStyleResolved(const std::string& id)
{
    DocxStyleDef out;
    std::string cur = id;
    for (int depth = 0; depth < 12 && !cur.empty(); ++depth) {
        auto it = gDocxStyles.find(cur);
        if (it == gDocxStyles.end()) break;
        const DocxStyleDef& d = it->second;
        if (out.jc          < 0 && d.jc          >= 0) out.jc = d.jc;
        if (out.firstIndent == 0.f && d.firstIndent != 0.f) out.firstIndent = d.firstIndent;
        if (out.before      < 0.f && d.before      >= 0.f) out.before = d.before;
        if (out.after       < 0.f && d.after       >= 0.f) out.after  = d.after;
        if (out.line        == 0.f && d.line        > 0.f) out.line   = d.line;
        if (out.bold        < 0 && d.bold          >= 0)   out.bold   = d.bold;
        if (out.sz          == 0.f && d.sz         > 0.f)  out.sz     = d.sz;
        if (out.family      < 0 && d.family        >= 0)   out.family = d.family;
        cur = d.basedOn;
    }
    return out;
}


// ── OMML (Word equations) → LaTeX ───────────────────────────────────────────
// Enough of the grammar for engineering papers: fractions, roots, scripts,
// n-ary operators, delimiters, functions and accents. Anything unrecognised
// degrades to its text content instead of vanishing.
static std::string OmmlLatex(pugi::xml_node n);

static std::string OmmlChild(pugi::xml_node n, const char* name)
{
    pugi::xml_node c = n.child(name);
    return c ? OmmlLatex(c) : std::string();
}

static std::string OmmlLatex(pugi::xml_node n)
{
    std::string nm = n.name();
    if (nm == "m:t") { std::string t = n.text().get(); return t; }

    if (nm == "m:f") {                                   // fraction
        return "\\frac{" + OmmlChild(n, "m:num") + "}{" + OmmlChild(n, "m:den") + "}";
    }
    if (nm == "m:rad") {                                 // root
        pugi::xml_node pr = n.child("m:radPr");
        std::string deg;
        if (pr) { pugi::xml_node dd = pr.child("m:degHide"); if (!dd) deg = OmmlChild(n, "m:deg"); }
        std::string body = OmmlChild(n, "m:e");
        return deg.empty() ? ("\\sqrt{" + body + "}") : ("\\sqrt[" + deg + "]{" + body + "}");
    }
    if (nm == "m:sSup")  return "{" + OmmlChild(n, "m:e") + "}^{" + OmmlChild(n, "m:sup") + "}";
    if (nm == "m:sSub")  return "{" + OmmlChild(n, "m:e") + "}_{" + OmmlChild(n, "m:sub") + "}";
    if (nm == "m:sSubSup") {
        return "{" + OmmlChild(n, "m:e") + "}_{" + OmmlChild(n, "m:sub") +
               "}^{" + OmmlChild(n, "m:sup") + "}";
    }
    if (nm == "m:nary") {                                // sum / product / integral
        pugi::xml_node pr = n.child("m:naryPr");
        std::string chr = "\\sum";
        if (pr) {
            std::string c = DocxAttr(pr.child("m:chr"), "m:val");
            if (!c.empty()) {
                if (c == "∑") chr = "\\sum";
                else if (c == "∏") chr = "\\prod";
                else if (c == "∫") chr = "\\int";
                else chr = c;
            }
        }
        std::string sub = OmmlChild(n, "m:sub"), sup = OmmlChild(n, "m:sup");
        std::string op = chr;
        if (!sub.empty()) op += "_{" + sub + "}";
        if (!sup.empty()) op += "^{" + sup + "}";
        return op + " " + OmmlChild(n, "m:e");
    }
    if (nm == "m:d") {                                   // delimiters
        std::string inner;
        for (pugi::xml_node e = n.child("m:e"); e; e = e.next_sibling("m:e"))
            inner += OmmlLatex(e);
        return "\\left(" + inner + "\\right)";
    }
    if (nm == "m:func") {
        return OmmlChild(n, "m:fName") + "\\left(" + OmmlChild(n, "m:e") + "\\right)";
    }
    if (nm == "m:acc" || nm == "m:bar" || nm == "m:box" || nm == "m:groupChr")
        return OmmlChild(n, "m:e");

    std::string out;
    for (pugi::xml_node c = n.first_child(); c; c = c.next_sibling()) out += OmmlLatex(c);
    return out;
}

static std::string OmmlPlainText(pugi::xml_node n)
{
    std::string nm = n.name();
    if (nm == "m:t") return n.text().get();
    std::string out;
    for (pugi::xml_node c = n.first_child(); c; c = c.next_sibling()) out += OmmlPlainText(c);
    return out;
}

static void DocxCollectMath(pugi::xml_node n, std::string& inlineTex, std::string& displayTex)
{
    for (pugi::xml_node c = n.first_child(); c; c = c.next_sibling()) {
        std::string nm = c.name();
        if (nm == "m:oMathPara")      displayTex += OmmlLatex(c);
        else if (nm == "m:oMath")     inlineTex  += OmmlPlainText(c);
        else                          DocxCollectMath(c, inlineTex, displayTex);
    }
}

// Find the first image reference and its requested size inside a paragraph.
static void DocxScanDrawing(pugi::xml_node n, std::string& rel, float& wpt, float& hpt)
{
    for (pugi::xml_node c = n.first_child(); c; c = c.next_sibling()) {
        std::string nm = c.name();
        if (rel.empty() && nm == "a:blip") {
            std::string id = DocxAttr(c, "r:embed");
            if (!id.empty()) rel = id;
        }
        if (hpt <= 0.f && nm == "wp:extent") {
            float cx = (float)atof(DocxAttr(c, "cx").c_str());
            float cy = (float)atof(DocxAttr(c, "cy").c_str());
            if (cx > 0.f && cy > 0.f) { wpt = cx / 12700.f; hpt = cy / 12700.f; }
        }
        DocxScanDrawing(c, rel, wpt, hpt);
    }
}

static DocPara DocxParagraph(pugi::xml_node pnode)
{
    DocPara p;
    pugi::xml_node pPr = pnode.child("w:pPr");
    // A paragraph without w:pStyle uses the document's default paragraph style,
    // not the editor's built-in Normal: that style carries the body size/spacing.
    std::string sid = pPr ? DocxAttr(pPr.child("w:pStyle"), "w:val") : std::string();
    if (sid.empty()) sid = gDocxDefaultStyleId;
    DocxStyleDef sdef = DocxStyleResolved(sid);
    if (pPr) {
        p.style = DocxStyleFromId(sid);

        std::string jc = DocxLower(DocxAttr(pPr.child("w:jc"), "w:val"));
        if (jc == "center")                          p.align = DA_Center;
        else if (jc == "right" || jc == "end")       p.align = DA_Right;
        else if (jc == "both" || jc == "distribute") p.align = DA_Justify;

        pugi::xml_node sp = pPr.child("w:spacing");
        if (sp) {
            auto tw = [&](const char* a) -> float {
                std::string v = DocxAttr(sp, a);
                return v.empty() ? -1.f : (float)atof(v.c_str()) / 20.f;   // twips -> pt
            };
            float b = tw("w:before"), aft = tw("w:after");
            if (b   >= 0.f) p.spaceBefore = b;
            if (aft >= 0.f) p.spaceAfter  = aft;
            float line = (float)atof(DocxAttr(sp, "w:line").c_str());
            std::string rule = DocxLower(DocxAttr(sp, "w:lineRule"));
            if (line > 0.f && (rule.empty() || rule == "auto"))
                p.lineSpacing = line / 240.f;                          // 240 = single
        }

        // fall back to the style's own values where the paragraph is silent
        if (p.firstIndent == 0.f) p.firstIndent = sdef.firstIndent;
        if (p.spaceBefore < 0.f)  p.spaceBefore = sdef.before;
        if (p.spaceAfter  < 0.f)  p.spaceAfter  = sdef.after;
        if (p.lineSpacing == 0.f) p.lineSpacing = sdef.line;
        if (p.align == DA_Left && sdef.jc >= 0) p.align = (DocAlign)sdef.jc;
        if (p.spaceAfter  < 0.f && gDocxDefaultAfter >= 0.f) p.spaceAfter  = gDocxDefaultAfter;
        if (p.lineSpacing == 0.f && gDocxDefaultLine > 0.f)  p.lineSpacing = gDocxDefaultLine;

        if (pugi::xml_node ind = pPr.child("w:ind")) {
            auto tw = [&](const char* a) {
                std::string v = DocxAttr(ind, a);
                return v.empty() ? 0.f : (float)atof(v.c_str()) / 20.f;
            };
            float fl = tw("w:firstLine"), hg = tw("w:hanging");
            p.firstIndent = fl > 0.f ? fl : (hg > 0.f ? -hg : 0.f);
        }

        if (pPr.child("w:numPr")) p.list = 1;        // bulleted; numbering.xml would refine this
        // Paragraph spacing: w:before/w:after are twips (1pt = 20), w:line is
        // 240ths of a line when w:lineRule is "auto" (else twips).
        if (pugi::xml_node spc = pPr.child("w:spacing")) {
            std::string bef = DocxAttr(spc, "w:before");
            std::string aft = DocxAttr(spc, "w:after");
            std::string ln  = DocxAttr(spc, "w:line");
            if (!bef.empty()) p.spaceBefore = (float)atof(bef.c_str()) / 20.f;
            if (!aft.empty()) p.spaceAfter  = (float)atof(aft.c_str()) / 20.f;
            if (!ln.empty()) {
                float v = (float)atof(ln.c_str());
                std::string rule = DocxLower(DocxAttr(spc, "w:lineRule"));
                if (rule.empty() || rule == "auto")
                    p.lineSpacing = (v > 1.f) ? v / 240.f : 1.f;
                else
                    p.lineSpacing = (v / 20.f) / 12.f;    // exact/atLeast: pt vs 12pt base
            }
        }
        if (pPr.child("w:pBdr").child("w:bottom")) {
            std::string bv = DocxAttr(pPr.child("w:pBdr").child("w:bottom"), "w:val");
            if (bv != "nil" && bv != "none" && !bv.empty()) p.divider = 1;
        }
    }
    // Base typography from the style chain, then the file's docDefaults. This has
    // to run even for paragraphs with no <w:pPr> (cover pages, captions), or the
    // whole document falls back to the editor's 15 pt DejaVu.
    if (sdef.sz > 0.f)             p.baseFs = sdef.sz;
    else if (gDocxDefaultFs > 0.f) p.baseFs = gDocxDefaultFs;
    if (sdef.family >= 0)          p.baseFamily = sdef.family;
    else if (gDocxDefaultFam >= 0) p.baseFamily = gDocxDefaultFam;

    int used = 0;
    DocxCollectInline(pnode, p, used);
    // A run that never named a face follows the paragraph's face (w:rFonts on the
    // style / docDefaults), otherwise the whole body would render in the UI font.
    for (DocRun& r : p.runs)
        if (!r.a.hasFamily) r.a.family = p.baseFamily;
    {
        std::string inlineTex, displayTex;
        DocxCollectMath(pnode, inlineTex, displayTex);
        p.mathTex = displayTex;
        if (!inlineTex.empty()) {
            int room = (int)sizeof(p.text) - 1 - used;
            if (room > 0) {
                std::string add = inlineTex;
                if ((int)add.size() > room) add.resize((size_t)room);
                memcpy(p.text + used, add.data(), add.size());
                DocRun r; r.len = (int)add.size(); r.a = DocAttr{};
                p.runs.push_back(r);
                used += (int)add.size();
                p.text[used] = '\0';
            }
        }
    }
    {
        std::string rel; float wpt = 0.f, hpt = 0.f;
        DocxScanDrawing(pnode, rel, wpt, hpt);
        if (!rel.empty()) {
            strncpy(p.imgRel, rel.c_str(), sizeof(p.imgRel) - 1);
            p.imgWpt = wpt; p.imgHpt = hpt;
        }
    }
    if (p.align == DA_Left && p.style == DS_Normal && used >= 40)
        p.align = DA_Justify;          // body copy: justify unless Word said otherwise

    if (!p.runs.empty()) {
        int sum = 0;
        for (const DocRun& r : p.runs) sum += r.len;
        if (sum != used) p.runs.clear();             // never leave runs out of sync with text
    }
    return p;
}

// Dispatch over body-level nodes; w:sdt (content controls) can wrap paragraphs.
static void DocxWalkBlock(pugi::xml_node node, std::vector<DocPara>& paras,
                          std::vector<DocTable>& tables)
{
    for (pugi::xml_node c = node.first_child(); c; c = c.next_sibling()) {
        std::string name = c.name();
        if (name == "w:p") {
            DocPara par = DocxParagraph(c);
            par.section = (int)gDocxSections.size();
            paras.push_back(par);
            if (pugi::xml_node sect = c.child("w:pPr").child("w:sectPr"))
                DocxReadSectPr(sect);
        }
        else if (name == "w:tbl") {
            std::vector<pugi::xml_node> rows;
            int cols = 0;
            for (pugi::xml_node tr = c.child("w:tr"); tr; tr = tr.next_sibling("w:tr")) {
                rows.push_back(tr);
                int n = 0;
                for (pugi::xml_node tc = tr.child("w:tc"); tc; tc = tc.next_sibling("w:tc")) {
                    int span = 1;
                    if (pugi::xml_node gs = tc.child("w:tcPr").child("w:gridSpan"))
                        span = std::max(1, atoi(DocxAttr(gs, "w:val").c_str()));
                    n += span;
                }
                if (n > cols) cols = n;
            }
            if (rows.empty() || cols == 0) continue;
            DocTable t;
            t.rows = (int)rows.size();
            t.cols = cols;
            t.cells.assign((size_t)t.rows * t.cols, std::string());
            t.cellFill.assign((size_t)t.rows * t.cols, ImVec4(0, 0, 0, 0));
            t.cellAlign.assign((size_t)t.rows * t.cols, -1);
            t.styled = true;

            // w:tblPr: width, indent, and each border side separately ("none" = absent).
            pugi::xml_node tp = c.child("w:tblPr");
            if (tp) {
                if (pugi::xml_node ind = tp.child("w:tblInd")) {
                    std::string ty = DocxLower(DocxAttr(ind, "w:type"));
                    if (ty.empty() || ty == "dxa") t.indent = (float)atof(DocxAttr(ind, "w:w").c_str()) / 20.f;
                }
                if (pugi::xml_node bd = tp.child("w:tblBorders")) {
                    auto side = [&](const char* nm, DocBorder& d) {
                        pugi::xml_node e = bd.child(nm);
                        if (!e) return;
                        std::string val = DocxLower(DocxAttr(e, "w:val"));
                        if (val.empty() || val == "nil" || val == "none") return;
                        d.on  = true;
                        float sz = (float)atof(DocxAttr(e, "w:sz").c_str());   // eighths of a point
                        d.w   = sz > 0.f ? sz / 8.f : 0.75f;
                        std::string hex = DocxAttr(e, "w:color");
                        if (hex.size() == 6 && DocxLower(hex) != "auto")
                            d.col = DocxHex(hex, ImVec4(0, 0, 0, 1));
                    };
                    side("w:top", t.bTop); side("w:bottom", t.bBottom);
                    side("w:left", t.bLeft); side("w:right", t.bRight);
                    side("w:insideH", t.bInsideH); side("w:insideV", t.bInsideV);
                }
            }
            // w:tblGrid: the authored column widths, in twips.
            if (pugi::xml_node grid = c.child("w:tblGrid")) {
                for (pugi::xml_node gc = grid.child("w:gridCol"); gc; gc = gc.next_sibling("w:gridCol")) {
                    float w = (float)atof(DocxAttr(gc, "w:w").c_str()) / 20.f;
                    t.colW.push_back(w > 0.f ? w : 60.f);
                }
                if ((int)t.colW.size() != t.cols) t.colW.assign((size_t)t.cols, 60.f);
            }
            for (int r = 0; r < t.rows; ++r) {
                int gcol = 0;
                for (pugi::xml_node tc = rows[(size_t)r].child("w:tc"); tc && gcol < t.cols;
                     tc = tc.next_sibling("w:tc")) {
                    pugi::xml_node tcPr = tc.child("w:tcPr");
                    int span = 1;
                    if (pugi::xml_node gs = tcPr.child("w:gridSpan"))
                        span = std::max(1, atoi(DocxAttr(gs, "w:val").c_str()));
                    bool vcont = false;
                    if (pugi::xml_node vm = tcPr.child("w:vMerge")) {
                        std::string v = DocxLower(DocxAttr(vm, "w:val"));
                        vcont = !(v == "restart");        // bare <w:vMerge/> continues the cell above
                    }
                    if (pugi::xml_node shd = tcPr.child("w:shd")) {
                        std::string hex = DocxAttr(shd, "w:fill");
                        if (hex.size() == 6 && DocxLower(hex) != "auto")
                            t.cellFill[(size_t)r * t.cols + gcol] = DocxHex(hex, ImVec4(1, 1, 1, 1));
                    }
                    if (!vcont && gcol < t.cols) {
                        DocxCellText(tc, t.at(r, gcol));
                        // first paragraph's alignment inside the cell
                        for (pugi::xml_node cp = tc.child("w:p"); cp; cp = cp.next_sibling("w:p")) {
                            std::string jc = DocxLower(DocxAttr(cp.child("w:pPr").child("w:jc"), "w:val"));
                            if (!jc.empty()) {
                                int al = (jc == "center") ? 1 : (jc == "right" || jc == "end") ? 2
                                       : (jc == "both" || jc == "distribute") ? 3 : 0;
                                t.cellAlign[(size_t)r * t.cols + gcol] = al;
                                break;
                            }
                        }
                    }
                    gcol += span;
                }
            }
            DocPara p;
            p.tableIdx = (int)tables.size();
            p.section  = (int)gDocxSections.size();
            snprintf(p.caption, sizeof(p.caption), "Tabel %d", p.tableIdx + 1);
            tables.push_back(std::move(t));
            paras.push_back(p);
        }
        else if (name == "w:sdt") {
            DocxWalkBlock(c.child("w:sdtContent"), paras, tables);
        }
        else if (name == "w:customXml" || name == "w:ins" || name == "w:moveTo") {
            DocxWalkBlock(c, paras, tables);
        }
    }
}

static std::vector<int>         gImportedMathPara;
static std::vector<std::string> gImportedMathTex;
static std::vector<int>         gImportedImgPara;
static std::vector<std::string> gImportedImgPath;

extern "C" {

// Entry point used by the shell: pointer to the .docx bytes, length in bytes.
// Returns the number of imported paragraphs, or a negative error code.
EMSCRIPTEN_KEEPALIVE
int doc_import_docx(const uint8_t* data, int len)
{
    if (!data || len <= 0) return -1;

    FILE* f = fopen("/import.docx", "wb");
    if (!f) return -2;
    size_t wrote = fwrite(data, 1, (size_t)len, f);
    fclose(f);
    if ((int)wrote != len) return -3;

    zip_t* z = zip_open("/import.docx", 0, 'r');
    if (!z) return -4;

    std::string xml, stylesXml;
    int entries = zip_total_entries(z);
    for (int i = 0; i < entries; ++i) {
        zip_entry_openbyindex(z, i);
        const char* nm = zip_entry_name(z);
        if (nm && strcmp(nm, "word/styles.xml") == 0) {
            void* buf = nullptr; size_t sz = 0;
            if (zip_entry_read(z, &buf, &sz) > 0 && buf && sz > 0)
                stylesXml.assign((const char*)buf, sz);
            if (buf) free(buf);
        }
        if (nm && strcmp(nm, "word/document.xml") == 0) {
            void*  buf = nullptr;
            size_t sz  = 0;
            if (zip_entry_read(z, &buf, &sz) > 0 && buf && sz > 0)
                xml.assign((const char*)buf, sz);
            if (buf) free(buf);
        }
        zip_entry_close(z);
        // Do NOT stop at document.xml: Word writes styles.xml and the media last,
        // and the styles have to be parsed before any paragraph names a style id.
        // (Breaking here silently skipped every style in the file, which is why
        // imported documents used to ignore their own size/spacing/faces.)
        if (!xml.empty() && !stylesXml.empty()) break;
    }
    zip_close(z);
    if (xml.empty()) return -5;

    pugi::xml_document doc;
    // parse_ws_pcdata keeps space-only <w:t> runs, which Word emits constantly;
    // without it "Menggunakan perkalian" collapses to "Menggunakanperkalian".
    pugi::xml_parse_result pr =
        doc.load_buffer(xml.data(), xml.size(), pugi::parse_default | pugi::parse_ws_pcdata);
    if (!pr) return -6;

    // styles must be known before paragraphs are parsed: Word keeps jc / ind /
    // spacing / size / face on the style, and the paragraph only names the id.
    gDocxStyles.clear();
    gDocxDefaultStyleId.clear();
    gDocxDefaultFs = 0.f; gDocxDefaultFam = -1;
    gDocxDefaultAfter = -1.f; gDocxDefaultLine = 0.f;
    if (!stylesXml.empty()) DocxLoadStyles(stylesXml);

    pugi::xml_node body = doc.child("w:document").child("w:body");
    if (!body) return -7;

    gDocxSections.clear();
    gDocxSectionCur = DocxSection();

    std::vector<DocPara>  paras;
    std::vector<DocTable> tables;
    DocxWalkBlock(body, paras, tables);

    if (pugi::xml_node sect = body.child("w:sectPr")) {
        DocxSection lastSec;
        if (pugi::xml_node ty = sect.child("w:type")) {
            std::string v = DocxLower(DocxAttr(ty, "w:val"));
            lastSec.continuous = (v == "continuous");
        }
        if (pugi::xml_node cols = sect.child("w:cols")) {
            std::string num = DocxAttr(cols, "w:num"), sp = DocxAttr(cols, "w:space");
            if (!num.empty()) lastSec.cols = std::max(1, atoi(num.c_str()));
            if (!sp.empty())  lastSec.gap  = (float)atof(sp.c_str()) / 20.f;
        }
        gDocxSections.push_back(lastSec);
    }
    // page margins from the section properties: 1418 twips = 70.9 pt (2.5 cm)
    if (pugi::xml_node sect = body.child("w:sectPr")) {
        if (pugi::xml_node mar = sect.child("w:pgMar")) {
            auto tw = [&](const char* a, float def) -> float {
                std::string v = DocxAttr(mar, a);
                return v.empty() ? def : (float)atof(v.c_str()) / 20.f;
            };
            kDocML = tw("w:left",  kDocML);
            kDocMR = tw("w:right", kDocMR);
            kDocMT = tw("w:top",   kDocMT);
            kDocMB = tw("w:bottom",kDocMB);
        }
    }

    if (paras.empty()) return -8;
    while (!paras.empty() && paras.back().text[0] == '\0' && paras.back().tableIdx < 0)
        paras.pop_back();

    gDocParas  = std::move(paras);
    gDocTables = std::move(tables);
    gDocImages.clear();
    // A freshly imported document owns its own furniture: keeping the editor's
    // demo header/footer would print "Document Header"/"Page 1" on every page of
    // someone else's file. Headers and footers are not parsed from the package
    // yet (see docs/OOXML-COMPATIBILITY.md), so clear them instead of inventing.
    gDocHeader[0] = '\0';
    gDocFooter[0] = '\0';
    gDocShapes.clear();
    gDocSel       = 0;
    gDocCaret     = DocPos{0, 0};
    gDocAnchor    = DocPos{0, 0};
    gDocSelActive = false;
    gDocLayoutDirty = true;
    gDocHistory.clear();
    gDocHistIdx = -1;
    DocPushSnap();

    gImportedMathPara.clear();
    gImportedMathTex.clear();
    for (int pi = 0; pi < (int)gDocParas.size(); ++pi) {
        if (!gDocParas[pi].mathTex.empty()) {
            gImportedMathPara.push_back(pi);
            gImportedMathTex.push_back(gDocParas[pi].mathTex);
        }
    }

    // ── images: resolve r:embed ids to media files and stage them in MEMFS ──
    gImportedImgPara.clear();
    gImportedImgPath.clear();
    {
        std::map<std::string, std::string> relTarget;      // rId -> media file name
        std::map<std::string, std::vector<uint8_t>> media;  // file name -> bytes
        zip_t* zr = zip_open("/import.docx", 0, 'r');
        if (zr) {
            int ne = zip_total_entries(zr);
            for (int i = 0; i < ne; ++i) {
                zip_entry_openbyindex(zr, i);
                const char* nm = zip_entry_name(zr);
                if (nm) {
                    std::string name(nm);
                    bool wantRels  = (name == "word/_rels/document.xml.rels");
                    bool wantStyles = (name == "word/styles.xml");
                    bool wantMedia = (name.rfind("word/media/", 0) == 0);
                    if (wantRels || wantMedia || wantStyles) {
                        void* buf = nullptr; size_t sz = 0;
                        if (zip_entry_read(zr, &buf, &sz) > 0 && buf && sz > 0) {
                            if (wantMedia) {
                                media[name] = std::vector<uint8_t>((uint8_t*)buf, (uint8_t*)buf + sz);
                            } else if (wantStyles) {
                                DocxLoadStyles(std::string((const char*)buf, sz));
                            } else {
                                std::string rels((const char*)buf, sz);
                                pugi::xml_document rd;
                                if (rd.load_buffer(rels.data(), rels.size())) {
                                    for (pugi::xml_node r = rd.child("Relationships").child("Relationship");
                                         r; r = r.next_sibling("Relationship")) {
                                        std::string id  = DocxAttr(r, "Id");
                                        std::string tgt = DocxAttr(r, "Target");
                                        if (!id.empty() && !tgt.empty()) relTarget[id] = tgt;
                                    }
                                }
                            }
                            free(buf);
                        }
                    }
                }
                zip_entry_close(zr);
            }
            zip_close(zr);
        }

        int imgNo = 0;
        for (int pi = 0; pi < (int)gDocParas.size(); ++pi) {
            DocPara& p = gDocParas[pi];
            if (p.imgRel[0] == '\0') continue;
            auto it = relTarget.find(p.imgRel);
            if (it == relTarget.end()) continue;
            std::string target = it->second;
            std::string file = (target.rfind("media/", 0) == 0) ? ("word/" + target) : target;
            auto mt = media.find(file);
            if (mt == media.end()) continue;

            std::ostringstream path; path << "/media" << imgNo << ".img";
            FILE* f = fopen(path.str().c_str(), "wb");
            if (!f) continue;
            fwrite(mt->second.data(), 1, mt->second.size(), f);
            fclose(f);

            gImportedImgPara.push_back(pi);
            gImportedImgPath.push_back(path.str());
            ++imgNo;
        }
    }

    gDocStatus = "Imported .docx - " + std::to_string(gDocParas.size()) + " blok, " +
                 std::to_string(gDocTables.size()) + " tabel, " +
                 std::to_string(gImportedImgPara.size()) + " gambar";
    return (int)gDocParas.size();
}

}  // extern "C"

static std::vector<uint8_t> DocBuildDocx()
{
    std::vector<uint8_t> out;
    FILE* s = fopen("template.docx", "rb");
    FILE* d = fopen("/output.docx", "wb");
    if (!s || !d) { if (s) fclose(s); if (d) fclose(d); return out; }
    { char tb[4096]; size_t nn; while ((nn = fread(tb, 1, sizeof(tb), s)) > 0) fwrite(tb, 1, nn, d); }
    fclose(s); fclose(d);

    duckx::Document doc("/output.docx");
    doc.open();
    if (!doc.is_open()) return out;

    auto& p = doc.paragraphs();
    int imgSeq = 0;
    std::vector<std::pair<int,int>> imgList;
    const float kEMUperPt  = 12700.f;
    const float kContentPt = kDocPageW - kDocML - kDocMR;

    auto flagsFor = [](const DocAttr& a) -> unsigned int {
        unsigned int f = duckx::none;
        if (a.bold)      f |= duckx::bold;
        if (a.italic)    f |= duckx::italic;
        if (a.underline) f |= duckx::underline;
        if (a.strike)    f |= duckx::strikethrough;
        if (a.vert > 0)  f |= duckx::superscript;
        else if (a.vert < 0) f |= duckx::subscript;
        return f;
    };

    auto injectImg = [&](duckx::Paragraph& para, const DocPara& dp) {
        const DocImage& img = gDocImages[dp.imageIdx];
        float dW = std::min(kContentPt * (dp.imageWidthPct / 100.f), (float)img.srcW);
        float dH = img.srcH > 0 ? dW * (float)img.srcH / (float)img.srcW : dW * 0.5f;
        int cx = (int)(dW * kEMUperPt), cy = (int)(dH * kEMUperPt);
        para.clear_runs();
        if (dp.imageLayout == IL_Inline) para.inject_drawing(imgSeq, cx, cy);
        else para.inject_drawing_anchor(imgSeq, cx, cy, dp.imageLayout == IL_Behind);
        imgList.push_back({imgSeq, dp.imageIdx});
        imgSeq++;
    };

    const int n = (int)gDocParas.size();
    bool cursorReady = false;     // set after a table: p already sits on a fresh w:p
    bool forceInsert = false;     // a caption paragraph was written into p; start a new one
    for (int idx = 0; idx < n; ++idx) {
        const DocPara& dp = gDocParas[idx];
        if (idx > 0 && !cursorReady) {
            if (forceInsert) { p.insert_paragraph_after("", duckx::none); p.next(); forceInsert = false; }
            else if (p.has_next_sibling()) p.next();
            else { p.insert_paragraph_after("", duckx::none); p.next(); }
        }
        cursorReady = false;
        p.clear_runs();

        if (dp.divider) {
            if (dp.divider == 2) {
                std::string dots(110, '.');
                p.set_para_props("left", 0, 40, 40, nullptr);
                p.add_run_ex(dots.c_str(), duckx::none, nullptr, nullptr, nullptr, 10.f);
            } else {
                p.set_para_props("left", 0, 80, 80, nullptr);
                p.add_run_ex("", duckx::none, nullptr, nullptr, nullptr, 2.f);
                p.set_para_border_bottom(0.75f, "8A90A0");
            }
            continue;
        }

        if (dp.tableIdx >= 0 && dp.tableIdx < (int)gDocTables.size()) {
            const DocTable& tb = gDocTables[dp.tableIdx];
            std::vector<std::string> cells;
            cells.reserve((size_t)tb.rows * tb.cols);
            for (int r = 0; r < tb.rows; ++r)
                for (int c = 0; c < tb.cols; ++c) cells.push_back(tb.at(r, c));
            char bHex[8] = {0}, hHex[8] = {0};
            snprintf(bHex, sizeof(bHex), "%02X%02X%02X",
                     (int)(tb.borderCol.x * 255.f + 0.5f),
                     (int)(tb.borderCol.y * 255.f + 0.5f),
                     (int)(tb.borderCol.z * 255.f + 0.5f));
            if (tb.header)
                snprintf(hHex, sizeof(hHex), "%02X%02X%02X",
                         (int)(tb.headerFill.x * 255.f + 0.5f),
                         (int)(tb.headerFill.y * 255.f + 0.5f),
                         (int)(tb.headerFill.z * 255.f + 0.5f));
            p.insert_table_after(tb.rows, tb.cols, cells, (int)(kContentPt * 20.f),
                                 tb.borderW, bHex, tb.header ? hHex : nullptr);
            if (dp.caption[0]) {
                // caption goes below the table, into the paragraph the table left behind
                p.add_run_ex(dp.caption, duckx::italic, nullptr, nullptr, nullptr, 9.f);
                p.set_para_props("center", 0, 60, 60, nullptr);
                forceInsert = true;
            } else {
                cursorReady = true;  // p already sits on a fresh w:p
            }
            continue;
        }

        if (dp.imageIdx >= 0 && dp.imageIdx < (int)gDocImages.size() &&
            !gDocImages[dp.imageIdx].pixels.empty()) {
            injectImg(p, dp);
            if (dp.caption[0]) {
                p.insert_paragraph_after(dp.caption, duckx::italic);
                p.next();
                p.set_para_props("center", 0, 60, 120, nullptr);
                forceInsert = true;
            }
            continue;
        }

        // paragraph properties: alignment, quote indent, code shading, spacing
        const char* jc = (dp.align == DA_Center) ? "center"
                       : (dp.align == DA_Right)  ? "right" : "left";
        int indLeft   = (dp.style == DS_Quote) ? 480 : 0;
        int hanging   = 0;
        if (dp.list) { indLeft = 567; hanging = 284; }
        int spaceBefore = (dp.style == DS_H1) ? 320 : (dp.style == DS_H2) ? 240
                        : (dp.style == DS_H3) ? 160 : 60;
        const char* shd = (dp.style == DS_Code) ? "F1F3F7" : nullptr;
        p.set_para_props(jc, indLeft, spaceBefore, 60, shd, hanging);

        const float sizePt   = (dp.baseFs > 0.f) ? dp.baseFs : DocStylePt(dp.style);
        const char* fontName = (dp.style == DS_Code) ? DocxFontName(2) : nullptr;

        DocAttr base = DocBaseAttr(dp);
        int len = (int)strlen(dp.text);
        std::vector<DocRun> runs = dp.runs;
        DocRunsNorm(runs, len, base);

        if (dp.list) {
            char mk[24];
            if (dp.list == 1) snprintf(mk, sizeof(mk), "%s\t", "\xE2\x80\xA2");
            else              snprintf(mk, sizeof(mk), "%d.\t", listNumFor(idx));
            p.add_run_ex(mk, duckx::none, nullptr, nullptr, nullptr, sizePt);
        }
        if (len == 0) { p.add_run_ex("", flagsFor(base), nullptr, nullptr, fontName, sizePt); continue; }

        int acc = 0;
        for (size_t i = 0; i < runs.size(); ++i) {
            if (runs[i].len <= 0) continue;
            std::string seg(dp.text + acc, (size_t)runs[i].len);
            acc += runs[i].len;
            char hex[16] = {0};
            if (runs[i].a.hasColor)
                snprintf(hex, sizeof(hex), "%02X%02X%02X",
                         (int)(runs[i].a.color.x * 255.f + 0.5f),
                         (int)(runs[i].a.color.y * 255.f + 0.5f),
                         (int)(runs[i].a.color.z * 255.f + 0.5f));
            const char* hlName = runs[i].a.hasHL ? DocHlName(runs[i].a.hl) : nullptr;
            int fam = runs[i].a.family;
            // only write an explicit font when the run differs from the default,
            // so ordinary text keeps inheriting the document style
            const char* rFont = (fam == 0) ? (dp.style == DS_Code ? DocxFontName(2) : nullptr)
                                           : DocxFontName(fam);
            float rSize = (runs[i].a.fsPt > 0.f) ? runs[i].a.fsPt : sizePt;
            p.add_run_ex(seg.c_str(), flagsFor(runs[i].a),
                         hex[0] ? hex : nullptr, hlName, rFont, rSize);
        }
    }
    // drop the template paragraphs we did not use
    {
        std::vector<duckx::Paragraph> extras;
        while (p.has_next_sibling()) { p.next(); extras.push_back(p); }
        for (size_t i = 0; i < extras.size(); ++i) {
            extras[i].clear_runs();
            extras[i].remove_self();
        }
    }

    doc.save();
    DocxInjectImages(imgList);

    FILE* f = fopen("/output.docx", "rb");
    if (!f) return out;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize((size_t)sz);
    if (sz > 0) fread(out.data(), 1, (size_t)sz, f);
    fclose(f);
    return out;
}

// ─── Vector toolbar icons ────────────────────────────────────────────────────
// The UI font is ASCII-only (ProggyClean), so emoji would render as "?" — these
// icons are drawn with ImDrawList primitives instead, Word-ribbon style.
enum DocIcon {
    DI_BOLD, DI_ITALIC, DI_UNDERLINE, DI_STRIKE, DI_SUP, DI_SUB,
    DI_ALIGN_L, DI_ALIGN_C, DI_ALIGN_R,
    DI_BULLET, DI_NUMBER,
    DI_UNDO, DI_REDO, DI_ADD, DI_DEL, DI_IMAGE, DI_HEADER, DI_SHAPE,
    DI_DIAGRAM, DI_TABLE, DI_MATH
};

static void DocIconText(ImDrawList* dl, ImFont* f, const char* s, float fs,
                        ImVec2 c, ImU32 col, ImVec2 off = ImVec2(0, 0))
{
    if (!f) return;
    ImVec2 t = f->CalcTextSizeA(fs, FLT_MAX, 0.f, s);
    dl->AddText(f, fs, {c.x - t.x * 0.5f + off.x, c.y - t.y * 0.5f + off.y}, col, s);
}

static void DocDrawIcon(ImDrawList* dl, DocIcon ic, ImVec2 c, ImU32 col)
{
    ImFont* reg = gDocFonts[0].regular ? gDocFonts[0].regular : ImGui::GetFont();
    ImFont* bld = gDocFonts[0].bold ? gDocFonts[0].bold : reg;
    ImFont* ita = gDocFonts[0].italic ? gDocFonts[0].italic : reg;
    const float x0 = c.x - 7.f, x1 = c.x + 7.f;
    const float y0 = c.y - 6.f, y1 = c.y + 6.f;

    switch (ic) {
    case DI_BOLD:      DocIconText(dl, bld, "B", 15.f, c, col); break;
    case DI_ITALIC:    DocIconText(dl, ita, "I", 15.f, c, col); break;
    case DI_UNDERLINE:
        DocIconText(dl, reg, "U", 14.f, c, col, ImVec2(0, -1.f));
        dl->AddLine({x0 + 2.f, y1 - 1.f}, {x1 - 2.f, y1 - 1.f}, col, 1.6f);
        break;
    case DI_STRIKE:
        DocIconText(dl, reg, "S", 14.f, c, col);
        dl->AddLine({x0 + 2.f, c.y + 1.f}, {x1 - 2.f, c.y + 1.f}, col, 1.6f);
        break;
    case DI_SUP:
        DocIconText(dl, reg, "x", 13.f, c, col, ImVec2(-2.f, 1.5f));
        DocIconText(dl, reg, "2", 8.f, c, col, ImVec2(5.f, -5.f));
        break;
    case DI_SUB:
        DocIconText(dl, reg, "x", 13.f, c, col, ImVec2(-2.f, -1.5f));
        DocIconText(dl, reg, "2", 8.f, c, col, ImVec2(5.f, 5.f));
        break;
    case DI_ALIGN_L:
        dl->AddLine({x0, y0 + 1.f}, {x1, y0 + 1.f}, col, 1.5f);
        dl->AddLine({x0, y0 + 5.f}, {x1 - 4.f, y0 + 5.f}, col, 1.5f);
        dl->AddLine({x0, y0 + 9.f}, {x1, y0 + 9.f}, col, 1.5f);
        dl->AddLine({x0, y0 + 13.f}, {x1 - 4.f, y0 + 13.f}, col, 1.5f);
        break;
    case DI_ALIGN_C:
        dl->AddLine({x0, y0 + 1.f}, {x1, y0 + 1.f}, col, 1.5f);
        dl->AddLine({x0 + 2.f, y0 + 5.f}, {x1 - 2.f, y0 + 5.f}, col, 1.5f);
        dl->AddLine({x0, y0 + 9.f}, {x1, y0 + 9.f}, col, 1.5f);
        dl->AddLine({x0 + 2.f, y0 + 13.f}, {x1 - 2.f, y0 + 13.f}, col, 1.5f);
        break;
    case DI_ALIGN_R:
        dl->AddLine({x0, y0 + 1.f}, {x1, y0 + 1.f}, col, 1.5f);
        dl->AddLine({x0 + 4.f, y0 + 5.f}, {x1, y0 + 5.f}, col, 1.5f);
        dl->AddLine({x0, y0 + 9.f}, {x1, y0 + 9.f}, col, 1.5f);
        dl->AddLine({x0 + 4.f, y0 + 13.f}, {x1, y0 + 13.f}, col, 1.5f);
        break;
    case DI_BULLET:
        for (int i = 0; i < 3; i++) {
            float yy = y0 + 2.f + i * 5.f;
            dl->AddCircleFilled({x0 + 2.f, yy}, 1.7f, col);
            dl->AddLine({x0 + 6.f, yy}, {x1, yy}, col, 1.5f);
        }
        break;
    case DI_NUMBER:
        for (int i = 0; i < 3; i++) {
            char n[4]; snprintf(n, sizeof(n), "%d", i + 1);
            float yy = y0 + 2.f + i * 5.f;
            DocIconText(dl, reg, n, 8.f, {x0 + 2.f, yy}, col);
            dl->AddLine({x0 + 6.f, yy}, {x1, yy}, col, 1.5f);
        }
        break;
    case DI_UNDO:
    case DI_REDO: {
        float s = (ic == DI_REDO) ? -1.f : 1.f;
        dl->PathClear();
        dl->PathArcTo({c.x, c.y + 2.f}, 5.5f, s > 0 ? -2.6f : -0.5f, s > 0 ? 0.5f : 2.6f, 10);
        dl->PathStroke(col, 0, 1.6f);
        float ax = c.x + s * 5.5f, ay = c.y + 2.f;
        dl->AddTriangleFilled({ax, ay - 3.f}, {ax, ay + 3.f}, {ax + s * 4.f, ay}, col);
        break; }
    case DI_ADD:
    case DI_DEL:
        dl->AddLine({c.x - 5.f, c.y}, {c.x + 5.f, c.y}, col, 1.7f);
        if (ic == DI_ADD) dl->AddLine({c.x, c.y - 5.f}, {c.x, c.y + 5.f}, col, 1.7f);
        break;
    case DI_IMAGE:
        dl->AddRect({x0, y0}, {x1, y1}, col, 1.f, 0, 1.3f);
        dl->AddCircleFilled({x0 + 4.f, y0 + 4.f}, 1.5f, col);
        dl->AddTriangleFilled({x0 + 1.f, y1 - 1.f}, {x0 + 6.f, y0 + 6.f}, {x0 + 10.f, y1 - 1.f}, col);
        dl->AddTriangleFilled({x0 + 7.f, y1 - 1.f}, {x0 + 10.f, y0 + 8.f}, {x1 - 1.f, y1 - 1.f}, col);
        break;
    case DI_HEADER:
        dl->AddRect({x0, y0}, {x1, y1}, col, 1.f, 0, 1.3f);
        dl->AddLine({x0, y0 + 4.f}, {x1, y0 + 4.f}, col, 1.3f);
        DocIconText(dl, reg, "T", 8.f, {c.x, y0 + 9.f}, col);
        break;
    case DI_SHAPE:
        dl->AddRect({x0, y0 + 1.f}, {c.x + 1.f, c.y + 4.f}, col, 1.f, 0, 1.3f);
        dl->AddCircle({c.x + 3.f, c.y + 4.f}, 4.f, col, 0, 1.3f);
        break;
    case DI_DIAGRAM: {
        // three boxes with connectors, flowchart-ish
        ImVec2 b1{x0 + 1.f, y0 + 1.f}, b2{x1 - 1.f, y0 + 5.f};
        ImVec2 b3{x0 + 3.f, y1 - 5.f}, b4{x1 - 3.f, y1 - 1.f};
        dl->AddRect(b1, {x0 + 8.f, y0 + 5.f}, col, 1.f, 0, 1.2f);
        dl->AddRect({x1 - 8.f, y0 + 1.f}, b2, col, 1.f, 0, 1.2f);
        dl->AddRect(b3, b4, col, 1.f, 0, 1.2f);
        dl->AddLine({x0 + 4.5f, y0 + 5.f}, {x0 + 4.5f, y1 - 5.f}, col, 1.f);
        dl->AddLine({x0 + 4.5f, c.y}, {c.x, c.y}, col, 1.f);
        dl->AddLine({c.x, c.y}, {c.x, y1 - 3.f}, col, 1.f);
        break; }
    case DI_MATH:
        dl->AddLine({x1, y0 + 1.f}, {x0, y0 + 1.f}, col, 1.5f);
        dl->AddLine({x0, y0 + 1.f}, {c.x + 1.f, c.y}, col, 1.5f);
        dl->AddLine({c.x + 1.f, c.y}, {x0, y0 + 13.f}, col, 1.5f);
        dl->AddLine({x0, y0 + 13.f}, {x1, y0 + 13.f}, col, 1.5f);
        break;
    case DI_TABLE:
        dl->AddRect({x0, y0 + 1.f}, {x1, y1 - 1.f}, col, 1.f, 0, 1.2f);
        dl->AddLine({x0, y0 + 4.5f}, {x1, y0 + 4.5f}, col, 1.f);
        dl->AddLine({x0, y0 + 8.5f}, {x1, y0 + 8.5f}, col, 1.f);
        dl->AddLine({c.x, y0 + 1.f}, {c.x, y1 - 1.f}, col, 1.f);
        break;
    }
}

static bool DocIconButton(const char* id, DocIcon ic, bool active, const char* tip,
                          ImVec2 size = ImVec2(26, 26), bool enabled = true,
                          const char* label = nullptr)
{
    ImVec2 p   = ImGui::GetCursorScreenPos();
    bool   hit = ImGui::InvisibleButton(id, size);
    bool hovered = enabled && ImGui::IsItemHovered();
    ImU32 bg = active ? ToU32(Col::primary)
             : hovered ? IM_COL32(46, 60, 84, 255)
             : IM_COL32(30, 40, 58, 255);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, {p.x + size.x, p.y + size.y}, bg, 4.f);
    ImU32 fg = enabled ? IM_COL32(235, 240, 250, 255) : IM_COL32(120, 130, 150, 255);
    ImVec2 c{p.x + size.x * 0.5f, p.y + size.y * 0.5f};
    if (label) DocIconText(dl, gDocFonts[0].regular ? gDocFonts[0].regular : ImGui::GetFont(),
                           label, 11.f, c, fg);
    else       DocDrawIcon(dl, ic, c, fg);
    if (tip && hovered) ImGui::SetTooltip("%s", tip);
    return enabled && hit;
}

static void DocTbSeparator()
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = 24.f;
    ImGui::GetWindowDrawList()->AddLine({p.x, p.y + 1.f}, {p.x, p.y + h}, ToU32(Col::border), 1.f);
    ImGui::Dummy({9.f, h});
}

// ══════════════════════════════════════════════════════════════════════════════
// ─── TabDocx ──────────────────────────────────────────────────────────────────
// ══════════════════════════════════════════════════════════════════════════════
static void TabDocx()
{
    if (gDocParas.empty()) {
        auto mk = [](const char* t, DocStyle s = DS_Normal,
                     bool b = false, bool it = false) -> DocPara {
            DocPara p; strncpy(p.text, t, 2047); p.style = s; p.bold = b; p.italic = it;
            return p;
        };
        gDocParas = {
            mk("My Document",                                               DS_H1),
            mk("Introduction",                                              DS_H2),
            mk("This document was created in WebAssembly using Dear ImGui and OOXML."),
            mk("Drag across paragraphs to select them, then press B, I, U, S or x^2 "
               "to format just the selection. Type to replace it, Enter to split a "
               "paragraph, Backspace at the start of one to join it with the previous."),
            mk("The best way to learn is by doing.",         DS_Quote, false, true),
            mk("printf(\"Hello, OOXML!\");",                               DS_Code),
            mk("Features",                                                  DS_H3),
            mk("Bold, italic, underline and strikethrough apply to the selected "
               "characters only, and continue on newly typed text. You can also "
               "import images, use undo/redo (Ctrl+Z/Y) and copy/paste (Ctrl+C/V). "
               "Colours and highlights are exported to .docx and .pdf too."),
        };
        for (auto& p : gDocParas) {
            int len = (int)strlen(p.text);
            p.runs.clear();
            if (len > 0) { DocRun r; r.len = len; r.a = DocBaseAttr(p); p.runs.push_back(r); }
        }
        gDocCaret  = DocPos{0, 0};
        gDocAnchor = gDocCaret;
        gDocSel    = 0;
        gDocTypingAttrSet = false;
        gDocLayoutDirty = true;
        DocPushSnap();
    }
    gDocCaret  = DocClampPos(gDocCaret);
    gDocAnchor = DocClampPos(gDocAnchor);
    gDocSel    = gDocCaret.para;
    if (gDocTableEditPara >= (int)gDocParas.size()) gDocTableEditPara = -1;

    ImGuiIO& io  = ImGui::GetIO();
    float W      = io.DisplaySize.x - 32.f;
    float cursorY0 = ImGui::GetCursorPosY();

    float toolH = 42.f, actH = 50.f;
    DocPara* caretP = (gDocCaret.para >= 0 && gDocCaret.para < (int)gDocParas.size())
                        ? &gDocParas[gDocCaret.para] : nullptr;
    const bool isImg = caretP && caretP->imageIdx >= 0;
    const DocAttr caretAttr = DocTypingAttrNow();

    // ── Ribbon (tabs + body) ─────────────────────────────────────────────────
    const float rowH = 28.f;
    const bool  narrowUi = W < 840.f;
    static int  gRibbonTab = 1;      // 0 File, 1 Home, 2 Insert, 3 Layout, 4 References, 5 Review, 6 View
    toolH = 34.f + (gRibbonTab == 2 ? 104.f : 52.f) + (narrowUi ? 12.f : 0.f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.10f, 0.14f, 0.22f, 1.f});
    ImGui::BeginChild("##tb", {W, toolH}, ImGuiChildFlags_None);
    // ── Ribbon: tabs + groups (Word-like) ───────────────────────────────────
    // One tab strip, one body. Home = formatting, Insert = objects and indexes,
    // File = document actions; the rest are small panels. The old flat 3-row
    // toolbar is kept as the body of Home/Insert so every control keeps working.
    static const char* kRibbonTabs[7] = { "File", "Home", "Insert", "Layout",
                                          "References", "Review", "View" };
    static const char* kRibbonTabsNarrow[7] = { "File", "Home", "Insert", "Layout",
                                               "Refs", "Review", "View" };
    const char* const* tabNames = narrowUi ? kRibbonTabsNarrow : kRibbonTabs;
    // The tab strip lives in the parent; only the body scrolls on phones.
    float bodyY = 34.f;

    ImGui::SetCursorPos({6.f, 4.f});
    for (int t = 0; t < 7; ++t) {
        if (t) ImGui::SameLine(0, 2.f);
        const bool on = (gRibbonTab == t);
        ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(0.16f, 0.24f, 0.38f, 1.f)
                                                  : ImVec4(0.f, 0.f, 0.f, 0.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.29f, 0.45f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_Text, on ? ImVec4(0.96f, 0.98f, 1.f, 1.f)
                                                : ImVec4(0.60f, 0.67f, 0.80f, 1.f));
        const float tw = ImGui::CalcTextSize(tabNames[t]).x + (narrowUi ? 12.f : 22.f);
        if (ImGui::Button(tabNames[t], {tw, 24.f})) gRibbonTab = t;
        if (on) {
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled({a.x + 5.f, b.y - 2.f}, {b.x - 5.f, b.y},
                                                      IM_COL32(96, 156, 255, 255));
        }
        ImGui::PopStyleColor(3);
    }
    ImGui::GetWindowDrawList()->AddLine({6.f, bodyY - 3.f}, {W - 6.f, bodyY - 3.f},
                                        IM_COL32(255, 255, 255, 20), 1.f);

    // Phones: the body scrolls horizontally instead of clipping the right-hand
    // controls off the screen; the tabs stay pinned above it.
    if (narrowUi) {
        ImGui::BeginChild("##rbb", {W - 8.f, toolH - 40.f}, ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar);
        bodyY = 2.f;
    }

    if (gRibbonTab == 0) {
        // File: page setup + open/export, top-left of the body
    {
        const float gap = 6.f, bh = 26.f;
        const int   perRow = narrowUi ? 2 : 5;
        const float bw = narrowUi ? (W - 14.f - gap * (perRow - 1)) / (float)perRow : 108.f;
        (void)narrowUi;
        const float x0 = 10.f;
        const float y0 = bodyY + 6.f;

        auto doPageSetup = [&]() { gDocHFOpen = true; gDocSetupLoaded = false; };
        auto doOpenPdf = [&]() {
            gDocStatus = "Opening PDF ...";
            EM_ASM({ if (window.pdfPick) window.pdfPick(); });
        };
        auto doOpen = [&]() {
            gDocStatus = "Opening local .docx ...";
            EM_ASM({ if (window.docPickDocx) window.docPickDocx(); });
        };
        auto doExportDocx = [&]() {
            gDocStatus.clear();
            if (gDocParas.empty()) { gDocStatus = "Nothing to export."; return; }
            std::vector<uint8_t> data = DocBuildDocx();
            if (data.empty()) { gDocStatus = "Error: docx export failed."; return; }
            EM_ASM({
                var b = HEAPU8.slice($0, $0 + $1);
                var blob = new Blob([b], {type:'application/vnd.openxmlformats-officedocument.wordprocessingml.document'});
                var url = URL.createObjectURL(blob);
                var a = document.createElement('a'); a.href = url; a.download = 'document.docx';
                a.style.display = 'none'; document.body.appendChild(a); a.click();
                document.body.removeChild(a);
                setTimeout(function(){URL.revokeObjectURL(url);}, 1000);
            }, data.data(), (int)data.size());
            gDocStatus = "DOCX exported (" + std::to_string(data.size()) + " bytes)";
        };
        auto doExportPdf = [&]() {
            gDocStatus.clear();
            if (gDocParas.empty()) { gDocStatus = "Nothing to export."; return; }
            auto pdf = BuildDocxPdf(gDocParas);
            if (pdf.empty()) { gDocStatus = "Error: PDF generation failed."; return; }
            EM_ASM({
                var b = HEAPU8.slice($0, $0 + $1);
                var blob = new Blob([b], {type:'application/pdf'});
                var url = URL.createObjectURL(blob);
                var a = document.createElement('a'); a.href = url; a.download = 'document.pdf';
                a.style.display = 'none'; document.body.appendChild(a); a.click();
                document.body.removeChild(a);
                setTimeout(function(){URL.revokeObjectURL(url);}, 1000);
            }, pdf.data(), (int)pdf.size());
            gDocStatus = "PDF exported (" + std::to_string(pdf.size()) + " bytes)";
        };

        struct FileAction { const char* label; void (*unused)(); };
        const char* labels[5] = { "Page setup", "Open .docx", "Open PDF", "Export .docx", "Export PDF" };
        for (int i = 0; i < 5; ++i) {
            int r = i / perRow, c = i % perRow;
            ImGui::SetCursorPos({x0 + c * (bw + gap), y0 + r * (bh + 6.f)});
            if (ImGui::Button(labels[i], {bw, bh})) {
                if (i == 0) doPageSetup();
                else if (i == 1) doOpen();
                else if (i == 2) doOpenPdf();
                else if (i == 3) doExportDocx();
                else doExportPdf();
            }
        }
    }

    } else if (gRibbonTab == 1) {
    // ── row 1: character + paragraph formatting ─────────────────────────────
    ImGui::SetCursorPos({6.f, bodyY + 1.f});
    { int si = caretP ? (int)caretP->style : 0;
      ImGui::SetNextItemWidth(92.f);
      if (ImGui::Combo("##sty", &si, kStyleName, 6) && caretP) {
          DocPos a, b; DocSelRange(a, b);
          DocPushSnap();
          for (int i = a.para; i <= b.para; i++) gDocParas[i].style = (DocStyle)si;
          gDocLayoutDirty = true;
      } }
    ImGui::SameLine(0, 6);
    { int fam = DocFamilyClamp(caretAttr.family);
      ImGui::SetNextItemWidth(88.f);
      if (ImGui::Combo("##fam", &fam, kDocFamilyShort, kDocFamilyCount))
          DocApplyOp(OP_FAMILY, ImVec4((float)fam, 0, 0, 0));
      if (ImGui::IsItemHovered()) {
          const int hf = DocFamilyClamp(caretAttr.family);
          if (kDocFamilyAlias[hf][0])
              ImGui::SetTooltip("%s\nfree, metric-compatible with %s",
                                kDocFamilyName[hf], kDocFamilyAlias[hf]);
          else
              ImGui::SetTooltip("%s", kDocFamilyName[hf]);
      } }
    ImGui::SameLine(0, 4);
    { static const float kSizes[8] = {0.f, 10.f, 12.f, 14.f, 16.f, 18.f, 24.f, 32.f};
      static const char* kSizeLabels[8] = {"Auto","10","12","14","16","18","24","32"};
      int si = 0;
      for (int i = 0; i < 8; i++) if (fabsf(caretAttr.fsPt - kSizes[i]) < 0.01f) si = i;
      ImGui::SetNextItemWidth(62.f);
      if (ImGui::Combo("##fs", &si, kSizeLabels, 8))
          DocApplyOp(OP_SIZE, ImVec4(kSizes[si], 0, 0, 0)); }

    ImGui::SameLine(0, 6);
    DocTbSeparator(); ImGui::SameLine(0, 4);

    if (DocIconButton("##b", DI_BOLD, caretAttr.bold, "Bold  (Ctrl+B)", {26,26}, !isImg))
        DocApplyOp(OP_BOLD);
    ImGui::SameLine(0, 2);
    if (DocIconButton("##i", DI_ITALIC, caretAttr.italic, "Italic  (Ctrl+I)", {26,26}, !isImg))
        DocApplyOp(OP_ITALIC);
    ImGui::SameLine(0, 2);
    if (DocIconButton("##u", DI_UNDERLINE, caretAttr.underline, "Underline  (Ctrl+U)", {26,26}, !isImg))
        DocApplyOp(OP_UNDERLINE);
    ImGui::SameLine(0, 2);
    if (DocIconButton("##s", DI_STRIKE, caretAttr.strike, "Strikethrough", {26,26}, !isImg))
        DocApplyOp(OP_STRIKE);

    ImGui::SameLine(0, 6);
    DocTbSeparator(); ImGui::SameLine(0, 4);
    if (DocIconButton("##sup", DI_SUP, caretAttr.vert > 0, "Superscript  (Ctrl+.)", {28,26}, !isImg))
        DocApplyOp(OP_SUP);
    ImGui::SameLine(0, 2);
    if (DocIconButton("##sub", DI_SUB, caretAttr.vert < 0, "Subscript  (Ctrl+,)", {28,26}, !isImg))
        DocApplyOp(OP_SUB);

    ImGui::SameLine(0, 6);
    DocTbSeparator(); ImGui::SameLine(0, 4);
    if (!isImg) {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {2, 3});
        ImVec4 colV = caretAttr.hasColor ? caretAttr.color : ImVec4(0.07f, 0.07f, 0.12f, 1.f);
        if (ImGui::ColorEdit3("##tc", &colV.x,
                ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoInputs))
            DocApplyOp(OP_COLOR, colV);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Text color (selection / new text)");
        ImGui::SameLine(0, 2);

        ImVec4 hlV = caretAttr.hasHL ? caretAttr.hl : ImVec4(1.f, 0.92f, 0.23f, 0.5f);
        if (ImGui::ColorEdit4("##hl", &hlV.x,
                ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoInputs |
                ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)) {
            if (hlV.w < 0.01f && (hlV.x > 0.01f || hlV.y > 0.01f || hlV.z > 0.01f))
                hlV.w = 0.5f;
            DocApplyOp(OP_HL, hlV);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Highlight (selection / new text)");
        ImGui::PopStyleVar();
        ImGui::SameLine(0, 2);
        if (DocIconButton("##hlx", DI_DEL, false, "Remove highlight", {24,26}, true)) {
            DocPos a2, b2; DocSelRange(a2, b2);
            DocPushSnap();
            for (int i = a2.para; i <= b2.para; i++) {
                int len = (int)strlen(gDocParas[i].text);
                int s = (i == a2.para) ? a2.off : 0;
                int e = (i == b2.para) ? b2.off : len;
                DocAttr at = DocAttrAt(gDocParas[i], s);
                at.hasHL = false;
                DocSetAttrRange(gDocParas[i], s, e, at);
            }
            if (gDocTypingAttrSet) gDocTypingAttr.hasHL = false;
            gDocLayoutDirty = true;
        }
    }

    ImGui::SameLine(0, 6);
    DocTbSeparator(); ImGui::SameLine(0, 4);
    if (!isImg) {
        if (DocIconButton("##al", DI_ALIGN_L, caretP && caretP->align == DA_Left, "Align left", {26,26})) {
            DocPos a2, b2; DocSelRange(a2, b2);
            DocPushSnap();
            for (int i = a2.para; i <= b2.para; i++) gDocParas[i].align = DA_Left;
        }
        ImGui::SameLine(0, 2);
        if (DocIconButton("##ac", DI_ALIGN_C, caretP && caretP->align == DA_Center, "Align center", {26,26})) {
            DocPos a2, b2; DocSelRange(a2, b2);
            DocPushSnap();
            for (int i = a2.para; i <= b2.para; i++) gDocParas[i].align = DA_Center;
        }
        ImGui::SameLine(0, 2);
        if (DocIconButton("##ar", DI_ALIGN_R, caretP && caretP->align == DA_Right, "Align right", {26,26})) {
            DocPos a2, b2; DocSelRange(a2, b2);
            DocPushSnap();
            for (int i = a2.para; i <= b2.para; i++) gDocParas[i].align = DA_Right;
        }

        // ── paragraph spacing (before / after / line) ───────────────────────
        ImGui::SameLine(0, 4);
        if (ImGui::Button("Spacing", {74, 26})) ImGui::OpenPopup("##spacingpop");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Paragraph spacing");
        if (ImGui::BeginPopup("##spacingpop")) {
            DocPos a2, b2; DocSelRange(a2, b2);
            DocPara* sp0 = caretP;
            float vBef = (sp0 && sp0->spaceBefore >= 0.f) ? sp0->spaceBefore
                       : DocSpaceBefore(sp0 ? sp0->style : DS_Normal, kDocBaseFS);
            float vAft = (sp0 && sp0->spaceAfter >= 0.f) ? sp0->spaceAfter : 0.f;
            float vLin = (sp0 && sp0->lineSpacing > 0.05f) ? sp0->lineSpacing : 1.f;
            bool  touched = false;

            ImGui::SetNextItemWidth(220.f);
            if (ImGui::SliderFloat("Before (pt)", &vBef, 0.f, 72.f, "%.0f")) {
                if (ImGui::IsItemActivated()) DocPushSnap();
                for (int i = a2.para; i <= b2.para && i < (int)gDocParas.size(); ++i)
                    gDocParas[i].spaceBefore = vBef;
                touched = true;
            }
            ImGui::SetNextItemWidth(220.f);
            if (ImGui::SliderFloat("After (pt)", &vAft, 0.f, 72.f, "%.0f")) {
                if (ImGui::IsItemActivated()) DocPushSnap();
                for (int i = a2.para; i <= b2.para && i < (int)gDocParas.size(); ++i)
                    gDocParas[i].spaceAfter = vAft;
                touched = true;
            }
            ImGui::SetNextItemWidth(220.f);
            if (ImGui::SliderFloat("Line spacing", &vLin, 0.8f, 3.f, "%.2f x")) {
                if (ImGui::IsItemActivated()) DocPushSnap();
                for (int i = a2.para; i <= b2.para && i < (int)gDocParas.size(); ++i)
                    gDocParas[i].lineSpacing = vLin;
                touched = true;
            }
            ImGui::Spacing();
            ImGui::TextUnformatted("Line:");
            ImGui::SameLine(0, 6);
            const float kQuick[4] = {1.0f, 1.15f, 1.5f, 2.0f};
            for (int q = 0; q < 4; ++q) {
                char lbl[24]; snprintf(lbl, sizeof(lbl), "%.2gx##lsq%d", kQuick[q], q);
                if (ImGui::SmallButton(lbl)) {
                    DocPushSnap();
                    for (int i = a2.para; i <= b2.para && i < (int)gDocParas.size(); ++i)
                        gDocParas[i].lineSpacing = kQuick[q];
                    vLin = kQuick[q];
                    touched = true;
                }
                ImGui::SameLine(0, 3);
            }
            ImGui::NewLine();
            if (ImGui::Button("Reset to style default", {180, 24})) {
                DocPushSnap();
                for (int i = a2.para; i <= b2.para && i < (int)gDocParas.size(); ++i) {
                    gDocParas[i].spaceBefore = -1.f;
                    gDocParas[i].spaceAfter  = -1.f;
                    gDocParas[i].lineSpacing = 0.f;
                }
                touched = true;
            }
            if (touched) gDocLayoutDirty = true;
            ImGui::EndPopup();
        }
    }

    ImGui::SameLine(0, 6);
    DocTbSeparator(); ImGui::SameLine(0, 4);
    if (DocIconButton("##bl", DI_BULLET, caretP && caretP->list == 1, "Bulleted list", {28,26}, !isImg))
        DocApplyList(1);
    ImGui::SameLine(0, 2);
    if (DocIconButton("##nl", DI_NUMBER, caretP && caretP->list == 2, "Numbered list", {28,26}, !isImg))
        DocApplyList(2);
    ImGui::SameLine(0, 6);
    DocTbSeparator(); ImGui::SameLine(0, 4);
    if (DocIconButton("##undo", DI_UNDO, false, "Undo  (Ctrl+Z)", {28,26})) { DocUndo(); gDocLayoutDirty = true; }
    ImGui::SameLine(0, 2);
    if (DocIconButton("##redo", DI_REDO, false, "Redo  (Ctrl+Y)", {28,26})) { DocRedo(); gDocLayoutDirty = true; }
    ImGui::SameLine(0, 6);
    DocTbSeparator(); ImGui::SameLine(0, 4);
    if (DocIconButton("##addp", DI_ADD, false, "New paragraph", {26,26})) {
        DocPushSnap();
        DocPara np;
        if (caretP) { np.align = caretP->align; np.style = caretP->style; }
        int ins = std::min((int)gDocParas.size(), gDocCaret.para + 1);
        gDocParas.insert(gDocParas.begin() + ins, np);
        DocSetCaret(DocPos{ins, 0}, false);
        gDocLayoutDirty = true;
        gDocFocus = true;
    }
    ImGui::SameLine(0, 2);
    if (DocIconButton("##delp", DI_DEL, false, "Delete paragraph", {26,26})) {
        DocPushSnap();
        int di = std::max(0, std::min(gDocCaret.para, (int)gDocParas.size() - 1));
        if (!gDocParas.empty()) gDocParas.erase(gDocParas.begin() + di);
        if (gDocParas.empty()) gDocParas.push_back(DocPara{});
        gDocCaret  = DocClampPos(DocPos{di, 0});
        gDocAnchor = gDocCaret;
        gDocSelActive = false;
        gDocLayoutDirty = true;
    }

    } else if (gRibbonTab == 2) {
    ImGui::SetCursorPos({6.f, bodyY + 1.f});
    ImGui::SameLine(0, 6);
    DocTbSeparator(); ImGui::SameLine(0, 4);
    if (DocIconButton("##img", DI_IMAGE, false, "Import image", {26,26}))
        EM_ASM({ document.getElementById('img-file-input').click(); });
    ImGui::SameLine(0, 2);
    if (DocIconButton("##hdr", DI_HEADER, gDocHFOpen, "Header / footer", {26,26}))
        gDocHFOpen = true;
    ImGui::SameLine(0, 2);
    if (DocIconButton("##shp", DI_SHAPE, gDocShapeMode, "Shapes", {26,26})) {
        gDocShapeMode = !gDocShapeMode;
        gDocSelShape = -1;
    }
    ImGui::SameLine(0, 2);
    if (DocIconButton("##dgm", DI_DIAGRAM, gDocDiagramOpen, "Insert diagram (Mermaid / Graphviz)", {26,26}))
        gDocDiagramOpen = true;
    ImGui::SameLine(0, 2);
    if (DocIconButton("##tbl", DI_TABLE, false, "Insert table", {26,26}))
        gDocTableOpen = true;
    ImGui::SameLine(0, 2);
    if (DocIconButton("##mth", DI_MATH, gDocMathOpen, "Insert LaTeX math", {26,26}))
        gDocMathOpen = true;
    if (gDocShapeMode) {
        ImGui::SameLine(0, 6);
        DocTbSeparator(); ImGui::SameLine(0, 4);
        const char* shNames[] = {"Rect", "Oval", "Line", "Tri"};
        for (int si = 0; si < 4; si++) {
            bool active = (gDocShapeType == (DocShapeType)si);
            ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4{0.231f,0.510f,0.965f,1.f} : Col::surface);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.376f,0.647f,0.98f,1.f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.149f,0.392f,0.922f,1.f});
            if (ImGui::Button(shNames[si], {40, 26})) gDocShapeType = (DocShapeType)si;
            ImGui::PopStyleColor(3);
            ImGui::SameLine(0, 2);
        }
        if (gDocSelShape >= 0 && gDocSelShape < (int)gDocShapes.size()) {
            DocShape& sh = gDocShapes[gDocSelShape];
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {2, 2});
            ImGui::ColorEdit4("Fill##sf", &sh.fill.x,
                ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
            ImGui::SameLine(0, 4);
            ImGui::ColorEdit4("Stroke##ss", &sh.stroke.x,
                ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
            ImGui::SameLine(0, 4);
            ImGui::SetNextItemWidth(60.f);
            ImGui::SliderFloat("##sth", &sh.thick, 0.5f, 8.f, "%.1f");
            ImGui::PopStyleVar();
            ImGui::SameLine(0, 4);
            if (ImGui::Button("Del##sd", {40, 26})) {
                gDocShapes.erase(gDocShapes.begin() + gDocSelShape);
                gDocSelShape = -1;
            }
        }
    }
    if (isImg) {
        ImGui::SameLine(0, 6);
        DocTbSeparator(); ImGui::SameLine(0, 4);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {4, 4});
        ImGui::SetNextItemWidth(120.f);
        if (ImGui::SliderFloat("##imgW", &caretP->imageWidthPct, 25.f, 100.f, "W:%.0f%%"))
            { DocPushSnap(); gDocLayoutDirty = true; }
        ImGui::PopStyleVar();
        ImGui::SameLine(0, 6);
        auto LBtn = [&](const char* label, DocImageLayout mode) {
            bool act = caretP->imageLayout == mode;
            ImGui::PushStyleColor(ImGuiCol_Button, act ? ImVec4{0.231f,0.510f,0.965f,1.f} : Col::surface);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.376f,0.647f,0.98f,1.f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.149f,0.392f,0.922f,1.f});
            if (ImGui::Button(label, {50, 26}) && caretP) { DocPushSnap(); caretP->imageLayout = mode; gDocLayoutDirty = true; }
            ImGui::PopStyleColor(3);
            ImGui::SameLine(0, 2);
        };
        LBtn("Inline", IL_Inline); LBtn("Behind", IL_Behind); LBtn("Front", IL_Front);
    }

    // ── row 3: insert (indexes, rules, dot leaders) ─────────────────────────
    ImGui::SetCursorPos({6.f, bodyY + 1.f + 2.f * (rowH + 4.f)});
    if (DocIconButton("##toc", DI_ADD, false, "Table of contents", {40, 26}, true, "TOC"))
        DocInsertIndex(1);
    ImGui::SameLine(0, 2);
    if (DocIconButton("##tof", DI_ADD, false, "List of figures", {40, 26}, true, "TOF"))
        DocInsertIndex(2);
    ImGui::SameLine(0, 2);
    if (DocIconButton("##tot", DI_ADD, false, "List of tables", {40, 26}, true, "TOT"))
        DocInsertIndex(3);
    ImGui::SameLine(0, 2);
    if (DocIconButton("##idxr", DI_REDO, false, "Refresh all indexes", {26, 26}))
        DocRefreshIndexes();
    ImGui::SameLine(0, 6);
    DocTbSeparator(); ImGui::SameLine(0, 4);
    if (DocIconButton("##rule", DI_ADD, false, "Horizontal rule (or type --- then Enter)", {34, 26}, true, "---")) {
        DocPushSnap();
        DocPara& cur = gDocParas[gDocCaret.para];
        cur.divider = 1; cur.text[0] = 0; cur.runs.clear();
        gDocLayoutDirty = true;
    }
    ImGui::SameLine(0, 2);
    if (DocIconButton("##dots", DI_ADD, false, "Dot leader (or type ... then Enter)", {34, 26}, true, "...")) {
        DocPushSnap();
        DocPara& cur = gDocParas[gDocCaret.para];
        cur.divider = 2; cur.text[0] = 0; cur.runs.clear();
        gDocLayoutDirty = true;
    }

    } else if (gRibbonTab == 3) {
        ImGui::SetCursorPos({8.f, bodyY + 4.f});
        if (ImGui::Button("Page setup", {118.f, 30.f})) { gDocHFOpen = true; gDocSetupLoaded = false; }
        ImGui::SameLine(0, 10.f);
        const int curCols = DocPageSetupAt(DocVisiblePage()).cols;
        const char* colLbl[3] = { "1 column", "2 columns", "3 columns" };
        for (int c = 0; c < 3; ++c) {
            if (c) ImGui::SameLine(0, 4.f);
            if (c + 1 == curCols) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.24f, 0.38f, 1.f));
            if (ImGui::Button(colLbl[c], {84.f, 30.f})) {
                DocPageSetup st = DocPageSetupAt(DocVisiblePage());
                st.cols = c + 1;
                DocPushSnap();
                DocSetPageSetup(DocVisiblePage(), st);
                gDocLayoutDirty = true;
            }
            if (c + 1 == curCols) ImGui::PopStyleColor();
        }
        ImGui::SameLine(0, 10.f);
        {
            DocPageSetup st = DocPageSetupAt(DocVisiblePage());
            bool land = st.landscape;
            if (ImGui::Checkbox("Landscape", &land)) {
                DocPushSnap();
                st = DocPageSetupAt(DocVisiblePage());
                st.landscape = land;
                if (land) { std::swap(st.ml, st.mt); std::swap(st.mr, st.mb); }
                else      { std::swap(st.ml, st.mt); std::swap(st.mr, st.mb); }
                DocSetPageSetup(DocVisiblePage(), st);
                gDocLayoutDirty = true;
            }
        }
    } else if (gRibbonTab == 4) {
        ImGui::SetCursorPos({8.f, bodyY + 4.f});
        if (ImGui::Button("Table of contents", {130.f, 30.f})) DocInsertIndex(1);
        ImGui::SameLine(0, 4.f);
        if (ImGui::Button("List of figures", {110.f, 30.f})) DocInsertIndex(2);
        ImGui::SameLine(0, 4.f);
        if (ImGui::Button("List of tables", {104.f, 30.f})) DocInsertIndex(3);
        ImGui::SameLine(0, 10.f);
        if (ImGui::Button("Refresh all indexes", {136.f, 30.f})) DocRefreshIndexes();
        ImGui::SameLine(0, 10.f);
        if (ImGui::Button("Horizontal rule", {112.f, 30.f})) {
            DocPushSnap();
            DocPara& cur = gDocParas[gDocCaret.para];
            cur.divider = 1; cur.text[0] = 0; cur.runs.clear();
            gDocLayoutDirty = true;
        }
        ImGui::SameLine(0, 4.f);
        if (ImGui::Button("Dot leader", {86.f, 30.f})) {
            DocPushSnap();
            DocPara& cur = gDocParas[gDocCaret.para];
            cur.divider = 2; cur.text[0] = 0; cur.runs.clear();
            gDocLayoutDirty = true;
        }
    } else if (gRibbonTab == 5) {
        int words = 0, chars = 0, paras = (int)gDocParas.size();
        for (const auto& p : gDocParas) {
            bool inWord = false;
            for (const char* q = p.text; *q; ++q) {
                ++chars;
                const bool sp = (*q == ' ' || *q == '\t' || *q == '\n');
                if (!sp && !inWord) { ++words; inWord = true; }
                else if (sp) inWord = false;
            }
        }
        ImGui::SetCursorPos({8.f, bodyY + 6.f});
        ImGui::Text("%d words   %d characters   %d paragraphs", words, chars, paras);
        ImGui::SetCursorPos({8.f, bodyY + 30.f});
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.67f, 0.80f, 1.f));
        ImGui::TextUnformatted("Spelling, comments and track changes are not implemented yet (see docs/PLAN.md, W5-W6).");
        ImGui::PopStyleColor();
    } else {
        ImGui::SetCursorPos({8.f, bodyY + 4.f});
        if (ImGui::Button("-", {34.f, 30.f})) gDocZoom = std::max(0.5f, gDocZoom - 0.1f);
        ImGui::SameLine(0, 4.f);
        ImGui::SetNextItemWidth(90.f);
        ImGui::Text("%.0f%%", gDocZoom * 100.f);
        ImGui::SameLine(0, 4.f);
        if (ImGui::Button("+", {34.f, 30.f})) gDocZoom = std::min(3.f, gDocZoom + 0.1f);
        ImGui::SameLine(0, 10.f);
        if (ImGui::Button("Fit page width", {120.f, 30.f})) gDocZoom = 1.f;
        ImGui::SameLine(0, 10.f);
        ImGui::TextDisabled("Ctrl + mouse wheel to zoom");
    }
    if (narrowUi) ImGui::EndChild();

    ImGui::EndChild();
    ImGui::PopStyleColor();

    if (gDocHFOpen && !gDocSetupLoaded) {
        gDocSetupDraft = DocPageSetupAt(DocVisiblePage());
        gDocSetupLoaded = true;
    }
    if (gDocHFOpen) {
        ImGui::SetNextWindowSize({600, 700}, ImGuiCond_Always);
        ImGui::SetNextWindowPos({W / 2.f - 300.f, cursorY0 + toolH + 10.f}, ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.12f, 0.17f, 0.26f, 0.97f});
        if (ImGui::Begin("Page setup & header/footer##hf", &gDocHFOpen,
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse)) {
            ImGui::TextUnformatted("Header (multi-line: Enter for a new line, {page} = page number)");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextMultiline("##hdr", &gDocHeader[0], sizeof(gDocHeader), {-1, 46});
            ImGui::Spacing();
            ImGui::TextUnformatted("Footer");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputTextMultiline("##ftr", &gDocFooter[0], sizeof(gDocFooter), {-1, 46});
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextUnformatted("Page layout (this page can differ)");
            int cp = DocVisiblePage();
            int maxPage = 0;
            for (const auto& lo : gDocLayout) maxPage = std::max(maxPage, lo.page);
            ImGui::Text("Editing page %d of %d", cp + 1, maxPage + 1);

            ImGui::TextUnformatted("Columns");
            ImGui::SameLine(160.f);
            ImGui::SetNextItemWidth(150.f);
            ImGui::SliderInt("##cols", &gDocSetupDraft.cols, 1, 3);
            if (gDocSetupDraft.cols > 1) {
                ImGui::SameLine(0, 14);
                ImGui::SetNextItemWidth(150.f);
                ImGui::SliderFloat("gutter##cg", &gDocSetupDraft.colGap, 6.f, 48.f, "%.0f pt");
            }

            if (ImGui::Checkbox("Landscape", &gDocSetupDraft.landscape)) {
                // The sheet rotates, so the margin band rotates with it: what
                // was the left/right gutter becomes the top/bottom one. The
                // swap is its own inverse, so toggling twice restores exactly.
                std::swap(gDocSetupDraft.ml, gDocSetupDraft.mt);
                std::swap(gDocSetupDraft.mr, gDocSetupDraft.mb);
            }
            ImGui::SameLine(0, 16);
            ImGui::Checkbox("Show header", &gDocSetupDraft.header);
            ImGui::SameLine(0, 12);
            ImGui::Checkbox("Show footer", &gDocSetupDraft.footer);

            // Two margin pairs per row: six rows of labels + sliders do not fit
            // a dialog this tall otherwise.
            auto marginRow = [](const char* label, const char* id, float* v, float x) {
                ImGui::SameLine(x);
                ImGui::TextUnformatted(label);
                ImGui::SameLine(x + 128.f);
                ImGui::SetNextItemWidth(140.f);
                ImGui::SliderFloat(id, v, 20.f, 220.f, "%.0f pt");
            };
            ImGui::NewLine();
            marginRow("Left",   "##ml", &gDocSetupDraft.ml, 0.f);
            marginRow("Right",  "##mr", &gDocSetupDraft.mr, 288.f);
            ImGui::NewLine();
            marginRow("Top",    "##mt", &gDocSetupDraft.mt, 0.f);
            marginRow("Bottom", "##mb", &gDocSetupDraft.mb, 288.f);
            ImGui::NewLine();
            marginRow("Header from top",    "##hd", &gDocSetupDraft.hd, 0.f);
            marginRow("Footer from bottom", "##fd", &gDocSetupDraft.fd, 288.f);

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextUnformatted("Tab stops (click the ruler to place, drag to move)");
            ImGui::TextUnformatted("New stop leader");
            ImGui::SameLine(160.f);
            ImGui::SetNextItemWidth(180.f);
            ImGui::Combo("##tabsleader", &gDocTabNewLeader, "none\0dots ...\0dashes ---\0line ___\0");
            ImGui::SameLine(0, 14);
            ImGui::Text("%d stop(s)", (int)gDocTabs.size());
            ImGui::SameLine(0, 10);
            if (ImGui::Button("Clear tab stops", {130, 26})) {
                DocPushSnap();
                gDocTabs.clear();
                gDocLayoutDirty = true;
            }
            if (!gDocTabs.empty()) {
                ImGui::TextUnformatted("Existing:");
                for (int ti = 0; ti < (int)gDocTabs.size(); ++ti) {
                    ImGui::SameLine(0, 8);
                    char lbl[64];
                    snprintf(lbl, sizeof(lbl), "%.1fcm %s##tab%d", gDocTabs[(size_t)ti].x / 28.346f,
                             gDocTabs[(size_t)ti].leader == 1 ? "..."
                           : gDocTabs[(size_t)ti].leader == 2 ? "---"
                           : gDocTabs[(size_t)ti].leader == 3 ? "___" : "-", ti);
                    if (ImGui::SmallButton(lbl)) {
                        DocPushSnap();
                        gDocTabs[(size_t)ti].leader = (gDocTabs[(size_t)ti].leader + 1) % 4;
                        gDocLayoutDirty = true;
                    }
                }
            }

            ImGui::Spacing();
            if (ImGui::Button("Apply to this page", {150, 30})) {
                DocPushSnap();
                DocSetPageSetup(cp, gDocSetupDraft);
                gDocLayoutDirty = true;
            }
            ImGui::SameLine(0, 8);
            if (ImGui::Button("Apply to all pages", {150, 30})) {
                DocPushSnap();
                // One document-wide setup: every page (including pages the PDF
                // export paginates later) inherits it, so margins can never
                // silently fall back to the factory default.
                gDocPageSetups.clear();
                gDocDefaultSetup    = gDocSetupDraft;
                gDocDefaultSetupSet = true;
                gDocLayoutDirty = true;
            }
            ImGui::SameLine(0, 8);
            if (ImGui::Button("Reset page", {110, 30})) {
                DocPushSnap();
                if (cp < (int)gDocPageSetups.size()) {
                    gDocPageSetups[(size_t)cp] = DocDefaultPageSetup();
                    gDocLayoutDirty = true;
                }
                gDocSetupDraft = DocDefaultPageSetup();
            }
            ImGui::Spacing();
            if (ImGui::Button("OK", {60, 28})) {
                // OK commits the draft to the page being edited (Word's
                // behaviour); a user who drags a margin and presses OK expects
                // the preview and the exports to change.
                DocPushSnap();
                DocSetPageSetup(cp, gDocSetupDraft);
                gDocLayoutDirty = true;
                gDocHFOpen = false;
            }
        }
        ImGui::End();
        if (!gDocHFOpen) gDocSetupLoaded = false;
        ImGui::PopStyleColor();
    }

    if (gDocTableOpen) {
        ImGui::SetNextWindowSize({320, 200}, ImGuiCond_Always);
        ImGui::SetNextWindowPos({W / 2.f - 160.f, cursorY0 + toolH + 12.f}, ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.12f, 0.17f, 0.26f, 0.97f});
        if (ImGui::Begin("Insert table##tbdlg", &gDocTableOpen,
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse)) {
            ImGui::SetNextItemWidth(150.f);
            ImGui::SliderInt("Rows", &gDocTableRows, 1, 12);
            ImGui::SetNextItemWidth(150.f);
            ImGui::SliderInt("Columns", &gDocTableCols, 1, 6);
            ImGui::Checkbox("Header row", &gDocTableHeader);
            ImGui::Spacing();
            if (PrimaryButton("Insert", {100, 30})) {
                DocInsertTable(gDocTableRows, gDocTableCols);
                gDocTableOpen = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {80, 30})) gDocTableOpen = false;
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }

    if (gDocDiagramOpen) {
        ImGui::SetNextWindowSize({560, 420}, ImGuiCond_Always);
        ImGui::SetNextWindowPos({W / 2.f - 280.f, cursorY0 + toolH + 12.f}, ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.12f, 0.17f, 0.26f, 0.97f});
        if (ImGui::Begin("Insert diagram##dgdlg", &gDocDiagramOpen,
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse)) {
            ImGui::RadioButton("Mermaid", &gDocDiagramKind, 0);
            ImGui::SameLine(0, 16);
            ImGui::RadioButton("Graphviz (DOT)", &gDocDiagramKind, 1);
            ImGui::Spacing();
            ImGui::PushItemWidth(-1);
            ImGui::InputTextMultiline("##dgsrc", gDocDiagramSrc, sizeof(gDocDiagramSrc), {-1, 230.f});
            ImGui::PopItemWidth();
            if (ImGui::SmallButton("Load sample")) {
                if (gDocDiagramKind == 0)
                    snprintf(gDocDiagramSrc, sizeof(gDocDiagramSrc),
                             "flowchart TD\n  A[Customer] --> B{Order valid?}\n"
                             "  B -->|yes| C[Ship it]\n  B -->|no| D[Cancel]\n");
                else
                    snprintf(gDocDiagramSrc, sizeof(gDocDiagramSrc),
                             "digraph G {\n  rankdir=LR;\n  node [shape=box];\n"
                             "  A -> B;\n  B -> C;\n  B -> D;\n}\n");
            }
            ImGui::Spacing();
            if (SuccessButton("Insert diagram", {150, 30})) {
                EM_ASM({ if (window.renderDiagram) window.renderDiagram($0, UTF8ToString($1)); },
                       gDocDiagramKind, gDocDiagramSrc);
                snprintf(gDocDiagramStatus, sizeof(gDocDiagramStatus), "rendering…");
                gDocDiagramOpen = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {90, 30})) gDocDiagramOpen = false;
            if (gDocDiagramStatus[0]) {
                ImGui::SameLine(0, 12);
                ImGui::PushStyleColor(ImGuiCol_Text, Col::muted);
                ImGui::TextUnformatted(gDocDiagramStatus);
                ImGui::PopStyleColor();
            }
            ImGui::PushStyleColor(ImGuiCol_Text, Col::muted);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.f);
            ImGui::TextWrapped("The diagram is rendered in the browser (Mermaid / Graphviz wasm), "
                               "converted to a PNG and inserted as an image paragraph.");
            ImGui::PopStyleColor();
        }
        ImGui::End();
        ImGui::PopStyleColor();
    }

    // ── Object bar: table styling / image options + caption ──────────────────
    float tableBarH = 0.f;
    {
        int objPi = (gDocCaret.para >= 0 && gDocCaret.para < (int)gDocParas.size()) ? gDocCaret.para : -1;
        bool objTable = objPi >= 0 && gDocParas[objPi].tableIdx >= 0 &&
                        gDocParas[objPi].tableIdx < (int)gDocTables.size();
        bool objImage = objPi >= 0 && gDocParas[objPi].imageIdx >= 0 &&
                        gDocParas[objPi].imageIdx < (int)gDocImages.size();
        if (objTable || objImage) {
            DocPara& op = gDocParas[objPi];
            tableBarH = 34.f;
            ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.13f, 0.17f, 0.25f, 1.f});
            ImGui::BeginChild("##tblbar", {W, tableBarH}, ImGuiChildFlags_None);
            ImGui::SetCursorPos({8.f, 5.f});
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {3, 3});

            if (objTable) {
                DocTable& tb = gDocTables[op.tableIdx];
                ImGui::TextUnformatted("Table");
                ImGui::SameLine(0, 10);
                ImGui::SetNextItemWidth(104.f);
                ImGui::SliderFloat("##tbw", &tb.borderW, 0.f, 4.f, "border %.1f");
                ImGui::SameLine(0, 6);
                ImGui::ColorEdit4("##tbc", &tb.borderCol.x,
                    ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Border color");
                ImGui::SameLine(0, 6);
                ImGui::Checkbox("Header", &tb.header);
                ImGui::SameLine(0, 4);
                ImGui::ColorEdit4("##tbh", &tb.headerFill.x,
                    ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Header row fill");
                ImGui::SameLine(0, 4);
                ImGui::ColorEdit4("##tbb", &tb.bodyFill.x,
                    ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Body fill");
                ImGui::SameLine(0, 10);
                auto resizeTable = [&](int nr, int nc) {
                    if (nr < 1 || nc < 1 || nr > 20 || nc > 8) return;
                    std::vector<std::string> ncells((size_t)nr * nc);
                    for (int r = 0; r < nr && r < tb.rows; ++r)
                        for (int c = 0; c < nc && c < tb.cols; ++c)
                            ncells[(size_t)r * nc + c] = tb.at(r, c);
                    tb.cells.swap(ncells);
                    tb.rows = nr;
                    tb.cols = nc;
                    gDocLayoutDirty = true;
                };
                if (ImGui::Button("+row", {42, 24})) resizeTable(tb.rows + 1, tb.cols);
                ImGui::SameLine(0, 2);
                if (ImGui::Button("-row", {42, 24})) resizeTable(tb.rows - 1, tb.cols);
                ImGui::SameLine(0, 2);
                if (ImGui::Button("+col", {42, 24})) resizeTable(tb.rows, tb.cols + 1);
                ImGui::SameLine(0, 2);
                if (ImGui::Button("-col", {42, 24})) resizeTable(tb.rows, tb.cols - 1);
                ImGui::SameLine(0, 8);
                if (ImGui::Button("Delete", {60, 24})) {
                    DocPushSnap();
                    gDocParas.erase(gDocParas.begin() + objPi);
                    if (gDocParas.empty()) gDocParas.push_back(DocPara{});
                    gDocTableEditPara = -1;
                    gDocLayoutDirty = true;
                }
            } else {
                ImGui::TextUnformatted("Image");
                ImGui::SameLine(0, 10);
                ImGui::SetNextItemWidth(120.f);
                if (ImGui::SliderFloat("##imgW", &op.imageWidthPct, 25.f, 100.f, "W:%.0f%%"))
                    gDocLayoutDirty = true;
                ImGui::SameLine(0, 8);
                auto LBtn = [&](const char* label, DocImageLayout mode) {
                    bool act = op.imageLayout == mode;
                    ImGui::PushStyleColor(ImGuiCol_Button, act ? ImVec4{0.231f,0.510f,0.965f,1.f} : Col::surface);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.376f,0.647f,0.98f,1.f});
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.149f,0.392f,0.922f,1.f});
                    if (ImGui::Button(label, {54, 24})) { op.imageLayout = mode; gDocLayoutDirty = true; }
                    ImGui::PopStyleColor(3);
                    ImGui::SameLine(0, 2);
                };
                LBtn("Inline", IL_Inline); LBtn("Behind", IL_Behind); LBtn("Front", IL_Front);
                ImGui::SameLine(0, 8);
                if (ImGui::Button("Delete", {60, 24})) {
                    DocPushSnap();
                    gDocParas.erase(gDocParas.begin() + objPi);
                    if (gDocParas.empty()) gDocParas.push_back(DocPara{});
                    gDocLayoutDirty = true;
                }
            }

            ImGui::SameLine(0, 12);
            ImGui::TextUnformatted("Caption");
            ImGui::SameLine(0, 6);
            ImGui::SetNextItemWidth(std::max(120.f, W * 0.32f));
            if (ImGui::InputTextWithHint("##cap", "caption text…", op.caption, sizeof(op.caption)))
                gDocLayoutDirty = true;
            if (ImGui::IsItemActive()) gDocFocus = false;

            ImGui::PopStyleVar();
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
    }

    float rulerH   = 24.f;
    float canvasH  = io.DisplaySize.y - cursorY0 - toolH - tableBarH - actH - rulerH - 14.f;
    float canvasW  = W;
    float fitW     = std::min(canvasW - 48.f, 720.f);

    int numPages = 1;
    for (auto& lo : gDocLayout) numPages = std::max(numPages, lo.page + 1);
    // Scale to the widest sheet in the document: a landscape page is 842pt
    // wide and would otherwise overflow the canvas.
    float docScale = fitW / std::max(kDocPageW, DocMaxPageWPt(numPages)) * gDocZoom;
    for (int pass = 0; pass < 2; ++pass) {
        float want = fitW / std::max(kDocPageW, DocMaxPageWPt(numPages)) * gDocZoom;
        if (gDocLayoutDirty || fabsf(want - gDocPrevDocScale) > 0.01f) {
            docScale = want;
            ComputeDocLayout(docScale);
        } else {
            docScale = want;
        }
        int n2 = 1;
        for (auto& lo : gDocLayout) n2 = std::max(n2, lo.page + 1);
        if (n2 == numPages) break;
        numPages = n2;
    }
    float pageW    = kDocPageW * docScale;
    float pageH    = kDocPageH * docScale;
    float pageLeft = (canvasW - pageW) * 0.5f;
    float contentW = (kDocPageW - kDocML - kDocMR) * docScale;

    float totalCanvasH = kDocGap * docScale;
    for (int pg = 0; pg < numPages; pg++)
        totalCanvasH += DocPageSizeOf(DocPageSetupAt(pg)).y * docScale + kDocGap * docScale;

    // ── Ruler ────────────────────────────────────────────────────────────────
    // The ruler follows the page the caret is on, so a landscape page gets a
    // landscape ruler (long edge, its own margins, its own tab stops).
    int rulerPage = 0;
    if (!gDocLayout.empty()) {
        int ci = std::min(std::max(gDocCaret.para, 0), (int)gDocLayout.size() - 1);
        rulerPage = gDocLayout[(size_t)ci].page;
    }
    rulerPage = std::min(std::max(rulerPage, 0), std::max(0, numPages - 1));
    DocPageSetup  rst    = DocPageSetupAt(rulerPage);
    ImVec2        rpsz   = DocPageSizeOf(rst);
    float         rPageW = rpsz.x * docScale;
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.22f, 0.22f, 0.26f, 1.f});
        ImGui::BeginChild("##ruler", {canvasW, rulerH}, ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImDrawList* rdl = ImGui::GetWindowDrawList();
        ImVec2 rp = ImGui::GetWindowPos();
        float font11 = ImGui::GetFontSize() * 0.75f;
        float cmPx   = 28.346f * docScale;
        float rLeft  = (canvasW - rPageW) * 0.5f;
        float ruleX0 = rp.x + rLeft;
        float marL   = rst.ml * docScale, marR = rst.mr * docScale;
        rdl->AddRectFilled({ruleX0, rp.y}, {ruleX0 + rPageW, rp.y + rulerH}, IM_COL32(240,240,242,255));
        rdl->AddRectFilled({ruleX0, rp.y}, {ruleX0 + marL, rp.y + rulerH}, IM_COL32(200,202,210,255));
        rdl->AddRectFilled({ruleX0 + rPageW - marR, rp.y}, {ruleX0 + rPageW, rp.y + rulerH}, IM_COL32(200,202,210,255));
        float totalCm = rpsz.x / 28.346f;
        for (float cm = 0.f; cm <= totalCm + 0.5f; cm += 0.5f) {
            float x = ruleX0 + cm * cmPx;
            bool fullCm = (fabsf(cm - (int)cm) < 0.01f);
            float tickH = fullCm ? rulerH * 0.55f : rulerH * 0.35f;
            rdl->AddLine({x, rp.y + rulerH - tickH}, {x, rp.y + rulerH}, IM_COL32(100,105,120,200), 0.8f);
            if (fullCm && cm > 0.f) {
                char buf2[8]; snprintf(buf2, sizeof(buf2), "%d", (int)cm);
                rdl->AddText(ImGui::GetFont(), font11, {x + 2.f, rp.y + 2.f}, IM_COL32(80,85,110,255), buf2);
            }
        }

        // ── Tab-stop markers ─────────────────────────────────────────────────
        // Left click empty ruler = add a stop, drag = move it, double click =
        // cycle its leader (... / --- / ___), right click = remove it.
        const float tabY = rp.y + 1.f;
        auto tabXof = [&](float xPt) { return ruleX0 + marL + xPt * docScale; };
        for (const DocTabStop& t : gDocTabs) {
            float x = tabXof(t.x);
            if (x < ruleX0 + marL - 1.f || x > ruleX0 + rPageW - marR + 1.f) continue;
            ImU32 tc = IM_COL32(214,86,52,255);
            if (t.leader == 1) {          // dots
                rdl->AddCircleFilled({x, tabY + 4.f}, 3.4f, tc);
            } else if (t.leader == 2) {   // dashes
                rdl->AddTriangleFilled({x, tabY + 8.f}, {x - 4.5f, tabY}, {x + 4.5f, tabY}, tc);
            } else if (t.leader == 3) {   // solid line
                rdl->AddRectFilled({x - 3.5f, tabY + 0.5f}, {x + 3.5f, tabY + 8.f}, tc, 1.f);
            } else {                      // no leader: Word's corner mark
                rdl->AddLine({x, tabY}, {x, tabY + 8.5f}, tc, 1.6f);
                rdl->AddLine({x, tabY + 8.5f}, {x + 5.f, tabY + 8.5f}, tc, 1.6f);
            }
        }
        ImGui::SetCursorPos({rLeft, 0.f});
        ImGui::InvisibleButton("##tabstrip", {std::max(4.f, rPageW), rulerH});
        bool stripHovered = ImGui::IsItemHovered();
        static int sTabDrag = -1;
        auto hitTabAt = [&](float mx) {
            int hi = -1;
            for (int i = 0; i < (int)gDocTabs.size(); i++)
                if (fabsf(tabXof(gDocTabs[(size_t)i].x) - mx) <= 5.f) hi = i;
            return hi;
        };
        if (stripHovered) {
            int hit = hitTabAt(io.MousePos.x);
            if (hit >= 0) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                char tt[96];
                snprintf(tt, sizeof(tt), "Tab %.1f cm  -  leader %s\ndouble click: change leader   right click: remove",
                         gDocTabs[(size_t)hit].x / 28.346f,
                         gDocTabs[(size_t)hit].leader == 1 ? "dots ..."
                       : gDocTabs[(size_t)hit].leader == 2 ? "dashes ---"
                       : gDocTabs[(size_t)hit].leader == 3 ? "line ___" : "none");
                ImGui::SetTooltip("%s", tt);
            } else {
                ImGui::SetTooltip("Click to add a tab stop");
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && hit >= 0) {
                DocPushSnap();
                gDocTabs.erase(gDocTabs.begin() + hit);
                gDocLayoutDirty = true;
            } else if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && hit >= 0) {
                DocPushSnap();
                gDocTabs[(size_t)hit].leader = (gDocTabs[(size_t)hit].leader + 1) % 4;
                gDocLayoutDirty = true;
            } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                DocPushSnap();
                if (hit < 0) {
                    float xPt = (io.MousePos.x - (ruleX0 + marL)) / docScale;
                    if (xPt > 2.f && xPt < rpsz.x - rst.ml - rst.mr - 4.f) {
                        DocTabStop t; t.x = xPt; t.leader = gDocTabNewLeader;
                        gDocTabs.push_back(t);
                        DocTabsSort();
                        for (int i = 0; i < (int)gDocTabs.size(); i++)
                            if (fabsf(gDocTabs[(size_t)i].x - xPt) < 0.01f) hit = i;
                        gDocLayoutDirty = true;
                    }
                }
                sTabDrag = hit;
            }
        }
        if (sTabDrag >= 0) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && sTabDrag < (int)gDocTabs.size()) {
                float xPt = (io.MousePos.x - (ruleX0 + marL)) / docScale;
                xPt = std::max(2.f, std::min(rpsz.x - rst.ml - rst.mr - 4.f, xPt));
                if (fabsf(gDocTabs[(size_t)sTabDrag].x - xPt) > 0.2f) {
                    gDocTabs[(size_t)sTabDrag].x = xPt;
                    gDocLayoutDirty = true;
                }
            } else {
                sTabDrag = -1;
                DocTabsSort();
                gDocLayoutDirty = true;
            }
        }

        // Margin pins edit *this page's* margins (they used to edit the
        // document-wide defaults, which a landscape page never showed).
        ImU32 pinCol = IM_COL32(80,100,210,230);
        rdl->AddTriangleFilled({ruleX0 + marL, rp.y}, {ruleX0 + marL + 8, rp.y}, {ruleX0 + marL, rp.y + 9}, pinCol);
        ImGui::SetCursorPos({rLeft + marL - 7.f, 0.f});
        ImGui::InvisibleButton("##mlpin", {14.f, rulerH});
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActivated()) DocPushSnap();
        if (ImGui::IsItemActive()) {
            DocPageSetup ps = DocPageSetupAt(rulerPage);
            float newML = ps.ml + io.MouseDelta.x / docScale;
            newML = std::max(20.f, std::min(rpsz.x - ps.mr - 60.f, newML));
            if (fabsf(newML - ps.ml) > 0.01f) {
                ps.ml = newML; DocSetPageSetup(rulerPage, ps); gDocLayoutDirty = true;
            }
        }
        rdl->AddTriangleFilled({ruleX0 + rPageW - marR, rp.y}, {ruleX0 + rPageW - marR - 8, rp.y}, {ruleX0 + rPageW - marR, rp.y + 9}, pinCol);
        ImGui::SetCursorPos({rLeft + rPageW - marR - 7.f, 0.f});
        ImGui::InvisibleButton("##mrpin", {14.f, rulerH});
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActivated()) DocPushSnap();
        if (ImGui::IsItemActive()) {
            DocPageSetup ps = DocPageSetupAt(rulerPage);
            float newMR = ps.mr - io.MouseDelta.x / docScale;
            newMR = std::max(20.f, std::min(rpsz.x - ps.ml - 60.f, newMR));
            if (fabsf(newMR - ps.mr) > 0.01f) {
                ps.mr = newMR; DocSetPageSetup(rulerPage, ps); gDocLayoutDirty = true;
            }
        }
        rdl->AddLine({rp.x, rp.y + rulerH - 0.5f}, {rp.x + canvasW, rp.y + rulerH - 0.5f}, IM_COL32(130,135,150,150), 0.8f);
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImGui::PushStyleColor(ImGuiCol_ChildBg, {0.18f, 0.19f, 0.22f, 1.f});
    ImGui::BeginChild("##canvas", {canvasW, canvasH}, ImGuiChildFlags_None);

    ImDrawList* dl   = ImGui::GetWindowDrawList();
    ImVec2 winPos    = ImGui::GetWindowPos();
    float  scrollY   = ImGui::GetScrollY();

    gDocView.winPos   = winPos;
    gDocView.scrollY  = scrollY;
    gDocView.pageLeft = pageLeft;
    gDocView.pageW    = pageW;
    gDocView.pageH    = pageH;
    gDocView.pageGap  = kDocGap * docScale;
    gDocView.docScale = docScale;
    gDocView.contentW = contentW;
    gDocView.canvasW  = canvasW;
    gDocView.pageCount = numPages;
    gDocView.valid    = true;

    ImGui::Dummy({1.f, totalCanvasH});

    ImFont* uiFont = gDocFonts[0].regular ? gDocFonts[0].regular : ImGui::GetFont();
    for (int pg = 0; pg < numPages; pg++) {
        DocPageSetup st = DocPageSetupAt(pg);
        ImVec2 psz = DocPageSizeOf(st);
        float pw   = psz.x * docScale;
        float phh  = psz.y * docScale;
        float pageCanY = kDocGap * docScale + DocPageTopPx(pg, docScale);
        float px = winPos.x + (canvasW - pw) * 0.5f;
        float py = winPos.y + pageCanY - scrollY;
        dl->AddRectFilled({px + 4, py + 4}, {px + pw + 4, py + phh + 4}, IM_COL32(0,0,0,55));
        dl->AddRectFilled({px, py}, {px + pw, py + phh}, IM_COL32(255,255,255,255));
        dl->AddRect({px, py}, {px + pw, py + phh}, IM_COL32(185,190,205,255));
        float cx1 = px + st.ml * docScale;
        float cx2 = px + (psz.x - st.mr) * docScale;
        const bool hdrEd = (gDocHFEdit == 1 && pg == gDocHFPage);
        const bool ftrEd = (gDocHFEdit == 2 && pg == gDocHFPage);

        // Header / footer: anchored to the paper edge (st.hd / st.fd), not to
        // the text margins, and multi-line.  The header grows downwards from
        // hd; the footer block's bottom line sits fd above the paper edge.
        const float hfs = kDocBaseFS * 0.85f;
        const float hfh = hfs * 1.35f;
        std::vector<std::string> hlines, flines;
        if (st.header && gDocHeader[0]) DocHFTextLines(gDocHeader, pg + 1, hlines);
        if (st.footer && gDocFooter[0]) DocHFTextLines(gDocFooter, pg + 1, flines);
        const float hTop = py + st.hd * docScale;
        const float fBottom = py + phh - st.fd * docScale;
        if (!hdrEd && !hlines.empty()) {
            for (size_t li = 0; li < hlines.size(); ++li)
                dl->AddText(uiFont, hfs, {cx1, hTop + (float)li * hfh},
                            IM_COL32(160,165,185,160), hlines[(size_t)li].c_str());
        }
        if (!ftrEd && !flines.empty()) {
            float fTop = fBottom - (float)flines.size() * hfh;
            for (size_t li = 0; li < flines.size(); ++li)
                dl->AddText(uiFont, hfs, {cx1, fTop + (float)li * hfh},
                            IM_COL32(160,165,185,160), flines[(size_t)li].c_str());
        }
        if (hdrEd || ftrEd) {
            size_t nEd = (hdrEd ? hlines.size() : flines.size());
            if (nEd == 0) nEd = 1;
            float ey = hdrEd ? hTop - 2.f : fBottom - (float)nEd * hfh;
            float eh = (float)nEd * hfh + 4.f;
            dl->AddRectFilled({cx1 - 4, ey - 1}, {cx2 + 4, ey + eh}, IM_COL32(255,255,255,245));
            dl->AddRect({cx1 - 4, ey - 1}, {cx2 + 4, ey + eh}, IM_COL32(70,120,230,220), 2.f, 0, 1.5f);
        }
        float ct = py + st.mt * docScale;
        float cb = py + (psz.y - st.mb) * docScale;
        dl->AddRect({cx1, ct}, {cx2, cb}, IM_COL32(210,212,222,60));
        if (st.landscape) {
            char tag[32]; snprintf(tag, sizeof(tag), "landscape  %dx%d", (int)psz.x, (int)psz.y);
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.7f,
                        {px + pw - 84.f, py + phh - 14.f}, IM_COL32(150,155,175,190), tag);
        }
    }

    // IL_Behind images
    for (int pi = 0; pi < (int)gDocParas.size(); pi++) {
        if (pi >= (int)gDocLayout.size()) continue;
        const DocPara& p = gDocParas[pi];
        if (p.imageIdx < 0 || p.imageIdx >= (int)gDocImages.size()) continue;
        if (p.imageLayout != IL_Behind) continue;
        const DocImage& img = gDocImages[p.imageIdx];
        const DocParaLayout& lo = gDocLayout[pi];
        DocPageSetup pst = DocPageSetupAt(lo.page);
        ImVec2 psz = DocPageSizeOf(pst);
        float pwPg = psz.x * docScale;
        float sx   = winPos.x + (canvasW - pwPg) * 0.5f + pst.ml * docScale;
        float sy   = winPos.y + kDocGap * docScale + DocPageTopPx(lo.page, docScale) + lo.yStart - scrollY;
        float dW   = std::min((psz.x - pst.ml - pst.mr) * docScale * (p.imageWidthPct / 100.f),
                              (float)img.srcW * docScale);
        float dH  = img.srcH > 0 ? dW * (float)img.srcH / (float)img.srcW : dW * 0.5f;
        dl->AddRectFilled({sx, sy}, {sx + dW, sy + dH}, IM_COL32(255,255,255,255));
        dl->AddImage((ImTextureID)(uintptr_t)img.texID, {sx, sy}, {sx + dW, sy + dH});
    }

    // ── Vertical ruler (left edge of the ruler's page) ──────────────────────
    {
        const float vrW = 22.f;
        float vPageTopPx = DocPageCanvasTop(rulerPage, docScale);
        float vLeft = (canvasW - rPageW) * 0.5f;
        float vPageH = rpsz.y * docScale;
        float vrX = winPos.x + vLeft - vrW - 5.f;
        if (vrX > winPos.x + 2.f) {
            float vrY = winPos.y + vPageTopPx - scrollY;
            float mt = rst.mt * docScale, mb = rst.mb * docScale;
            dl->AddRectFilled({vrX, vrY}, {vrX + vrW, vrY + vPageH}, IM_COL32(240,240,242,255));
            dl->AddRectFilled({vrX, vrY}, {vrX + vrW, vrY + mt}, IM_COL32(205,208,216,255));
            dl->AddRectFilled({vrX, vrY + vPageH - mb}, {vrX + vrW, vrY + vPageH}, IM_COL32(205,208,216,255));
            float cm = 28.346f * docScale;
            for (float ycm = 0.f; ycm <= rpsz.y / 28.346f + 0.5f; ycm += 0.5f) {
                float yy = vrY + ycm * cm;
                bool full = (fabsf(ycm - (int)ycm) < 0.01f);
                dl->AddLine({vrX + vrW - (full ? 8.f : 5.f), yy}, {vrX + vrW, yy},
                            IM_COL32(100,105,120,190), 0.8f);
                if (full && ycm > 0.f) {
                    char b[8]; snprintf(b, sizeof(b), "%d", (int)ycm);
                    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.7f,
                                {vrX + 2.f, yy + 1.f}, IM_COL32(95,100,120,220), b);
                }
            }
            dl->AddRect({vrX, vrY}, {vrX + vrW, vrY + vPageH}, IM_COL32(150,155,170,180));
            ImU32 pin = IM_COL32(80,100,210,230);
            dl->AddTriangleFilled({vrX + vrW, vrY + mt}, {vrX + vrW, vrY + mt + 9.f},
                                  {vrX + vrW - 9.f, vrY + mt}, pin);
            dl->AddTriangleFilled({vrX + vrW, vrY + vPageH - mb}, {vrX + vrW, vrY + vPageH - mb - 9.f},
                                  {vrX + vrW - 9.f, vrY + vPageH - mb}, pin);

            ImGui::SetCursorScreenPos({vrX, vrY + mt - 6.f});
            ImGui::InvisibleButton("##vmt", {vrW, 12.f});
            if (ImGui::IsItemHovered() || ImGui::IsItemActive())
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            if (ImGui::IsItemActivated()) DocPushSnap();
            if (ImGui::IsItemActive()) {
                DocPageSetup ps = DocPageSetupAt(rulerPage);
                float nmt = std::max(20.f, std::min(rpsz.y - ps.mb - 120.f,
                                                    ps.mt + io.MouseDelta.y / docScale));
                if (fabsf(nmt - ps.mt) > 0.01f) {
                    ps.mt = nmt; DocSetPageSetup(rulerPage, ps); gDocLayoutDirty = true;
                }
            }
            ImGui::SetCursorScreenPos({vrX, vrY + vPageH - mb - 6.f});
            ImGui::InvisibleButton("##vmb", {vrW, 12.f});
            if (ImGui::IsItemHovered() || ImGui::IsItemActive())
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            if (ImGui::IsItemActivated()) DocPushSnap();
            if (ImGui::IsItemActive()) {
                DocPageSetup ps = DocPageSetupAt(rulerPage);
                float nmb = std::max(20.f, std::min(rpsz.y - ps.mt - 120.f,
                                                    ps.mb - io.MouseDelta.y / docScale));
                if (fabsf(nmb - ps.mb) > 0.01f) {
                    ps.mb = nmb; DocSetPageSetup(rulerPage, ps); gDocLayoutDirty = true;
                }
            }
        }
    }

    // ── One interactive surface for the whole document ───────────────────────
    float bgW = std::max(kDocPageW, DocMaxPageWPt(numPages)) * docScale;
    ImGui::SetCursorPos({(canvasW - bgW) * 0.5f, kDocGap * docScale});
    ImGui::SetNextItemAllowOverlap();      // inline editors submitted later must win hover
    ImGui::InvisibleButton("##docedit",
        {std::max(4.f, bgW), std::max(4.f, totalCanvasH)},
        ImGuiButtonFlags_MouseButtonLeft);
    bool     cvHovered = ImGui::IsItemHovered();
    ImGuiID  cvId      = ImGui::GetItemID();

    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        gDocFocus = cvHovered;

    // in-place header/footer editor widget (submitted after the background button)
    if (gDocHFEdit > 0 && gDocHFPage >= 0 && gDocHFPage < numPages) {
        ++gDocHFFrames;
        const float hfs = kDocBaseFS * 0.85f;
        DocPageSetup est = DocPageSetupAt(gDocHFPage);
        ImVec2 esz = DocPageSizeOf(est);
        float epw = esz.x * docScale, eph = esz.y * docScale;
        float epx = winPos.x + (canvasW - epw) * 0.5f;
        float pyy = winPos.y + kDocGap * docScale + DocPageTopPx(gDocHFPage, docScale) - scrollY;
        float ex1 = epx + est.ml * docScale;
        float ex2 = epx + (esz.x - est.mr) * docScale;
        float hfh = hfs * 1.35f;
        std::vector<std::string> hfl;
        DocHFTextLines(gDocHFEdit == 1 ? gDocHeader : gDocFooter, gDocHFPage + 1, hfl);
        float nLines = (float)std::max<size_t>(hfl.size(), 1);
        float eTop = (gDocHFEdit == 1) ? pyy + est.hd * docScale
                                       : pyy + eph - est.fd * docScale - nLines * hfh;
        ImGui::SetCursorScreenPos({ex1, eTop - 2.f});
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(55,60,80,255));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {2, 2});
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
        if (hfs > 0.f) ImGui::PushFont(gDocFonts[0].regular, hfs);
        if (gDocHFFocus > 0) { ImGui::SetKeyboardFocusHere(); --gDocHFFocus; }
        // Multiline: Enter inserts a line break; Escape or a click elsewhere commits.
        bool edited = (gDocHFEdit == 1)
            ? ImGui::InputTextMultiline("##hf_h", gDocHeader, sizeof(gDocHeader),
                                        {ex2 - ex1, nLines * hfh + 4.f}, ImGuiInputTextFlags_AllowTabInput)
            : ImGui::InputTextMultiline("##hf_f", gDocFooter, sizeof(gDocFooter),
                                        {ex2 - ex1, nLines * hfh + 4.f}, ImGuiInputTextFlags_AllowTabInput);
        const bool edHovered = ImGui::IsItemHovered();
        if (ImGui::IsItemActive()) { gDocFocus = false; gDocHFFocus = 0; }
        if (edited) gDocLayoutDirty = true;
        // Close on a click that lands outside the editor, never on a bare
        // deactivation: a fast click delivers down+up in one frame and would
        // otherwise close the editor on the very click that opened it.
        else if (gDocHFFrames > 2 && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !edHovered) {
            gDocHFEdit = 0;
        }
        if (hfs > 0.f) ImGui::PopFont();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
    }
    if (gDocHFEdit > 0 && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) gDocHFEdit = 0;

    // ══ Keyboard ═════════════════════════════════════════════════════════════
    // Any active widget (table cell, caption field, header editor…) owns the
    // keyboard; this editor must not also consume the typed characters.
    if (ImGui::GetActiveID() != 0 && ImGui::GetActiveID() != cvId)
        gDocFocus = false;
    if (gDocFocus) {
        io.WantTextInput       = true;
        io.WantCaptureKeyboard = true;

        const bool shift = io.KeyShift;
        const bool ctrl  = io.KeyCtrl || io.KeySuper;
        DocPos selA, selB; DocSelRange(selA, selB);
        const bool hasSel = DocHasSel();

        if (io.InputQueueCharacters.Size > 0) {
            std::string ins;
            for (int i = 0; i < io.InputQueueCharacters.Size; i++) {
                unsigned int c = (unsigned int)(ImWchar)io.InputQueueCharacters[i];
                if (c == '\n' || c == '\r' || c < 32 || c == 127) continue;
                char tmp[4]; int k = dt::U8Encode(c, tmp); ins.append(tmp, (size_t)k);
            }
            io.InputQueueCharacters.resize(0);
            if (!ins.empty()) {
                DocAttr at = DocTypingAttrNow();
                DocPushSnap();
                if (hasSel) DocDeleteRange(selA, selB);
                DocInsertRaw(gDocCaret, ins, at);
                gDocTypingAttr    = at;      // keep typing in the same style
                gDocTypingAttrSet = true;
            }
        }

        // ── Tab: jump to the next tab stop, carrying the leader style ───────
        if (ImGui::IsKeyPressed(ImGuiKey_Tab, true) && !ctrl) {
            DocAttr at = DocTypingAttrNow();
            DocPushSnap();
            if (hasSel) DocDeleteRange(selA, selB);
            DocInsertRaw(gDocCaret, std::string(1, '\t'), at);
            gDocTypingAttr    = at;
            gDocTypingAttrSet = true;
            gDocBlinkT = ImGui::GetTime();
        }

        auto moveH = [&](int dir) {
            DocPos p = gDocCaret;
            if (hasSel && !shift) {
                if (dir < 0) DocSetCaret(selA, false); else DocSetCaret(selB, false);
                return;
            }
            const char* t = gDocParas[p.para].text;
            int len = (int)strlen(t);
            if (ctrl) {
                if (dir < 0) p.off = DocWordStart(t, p.off);
                else         p.off = DocWordEnd(t, p.off);
            } else if (dir < 0) {
                if (p.off > 0) p.off = dt::U8Prev(t, p.off);
                else if (p.para > 0) { --p.para; p.off = DocParaLen(p.para); }
            } else {
                if (p.off < len) p.off += dt::U8Seq(t, p.off, len);
                else if (p.para + 1 < (int)gDocParas.size()) { ++p.para; p.off = 0; }
            }
            DocSetCaret(p, shift);
        };

        auto moveV = [&](int dir) {
            int pi = gDocCaret.para;
            if (pi >= (int)gDocLines.size()) return;
            const dt::ParaLayout& pl = gDocLines[pi];
            if (pl.lines.empty()) return;
            int li = 0;
            float x = dt::XForOffset(pl, gDocCaret.off, &li);
            float want = (gDocWantX >= 0.f) ? gDocWantX : x;
            int t = li + dir;
            if (t >= 0 && t < (int)pl.lines.size()) {
                int no = dt::OffsetAtX(pl.lines[t], gDocParas[pi].text, want);
                DocSetCaret(DocPos{pi, no}, shift);
                gDocWantX = want;
            } else if (dir > 0) {
                if (pi + 1 < (int)gDocParas.size()) {
                    const dt::ParaLayout& n2 = gDocLines[pi + 1];
                    int no = n2.lines.empty() ? 0 : dt::OffsetAtX(n2.lines[0], gDocParas[pi + 1].text, want);
                    DocSetCaret(DocPos{pi + 1, no}, shift);
                } else {
                    DocSetCaret(DocPos{pi, DocParaLen(pi)}, shift);
                }
                gDocWantX = want;
            } else {
                if (pi > 0) {
                    const dt::ParaLayout& p2 = gDocLines[pi - 1];
                    int no = p2.lines.empty() ? 0
                           : dt::OffsetAtX(p2.lines.back(), gDocParas[pi - 1].text, want);
                    DocSetCaret(DocPos{pi - 1, no}, shift);
                } else {
                    DocSetCaret(DocPos{pi, 0}, shift);
                }
                gDocWantX = want;
            }
        };

        auto lineEdge = [&](bool end) {
            int pi = gDocCaret.para;
            if (pi >= (int)gDocLines.size() || gDocLines[pi].lines.empty()) return;
            int li = 0;
            dt::XForOffset(gDocLines[pi], gDocCaret.off, &li);
            int off = end ? gDocLines[pi].lines[li].b1 : gDocLines[pi].lines[li].b0;
            DocSetCaret(DocPos{pi, off}, shift);
        };

        // NOTE: everything here must use repeat=false — a held Ctrl+B would
        // otherwise toggle the formatting twice a second (ImGui default).
        if (ctrl) {
            if (ImGui::IsKeyPressed(ImGuiKey_A, false)) {
                gDocAnchor = DocPos{0, 0};
                gDocCaret  = DocPos{(int)gDocParas.size() - 1, DocParaLen((int)gDocParas.size() - 1)};
                gDocSelActive = true;
                gDocBlinkT = ImGui::GetTime();
                gDocWantX  = -1.f;
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_C, false)) DocCopySelection(false);
            else if (ImGui::IsKeyPressed(ImGuiKey_X, false)) DocCopySelection(true);
            else if (ImGui::IsKeyPressed(ImGuiKey_V, false)) DocPasteClipboard();
            else if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) { if (shift) DocRedo(); else DocUndo(); gDocLayoutDirty = true; }
            else if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) { DocRedo(); gDocLayoutDirty = true; }
            else if (ImGui::IsKeyPressed(ImGuiKey_B, false)) DocApplyOp(OP_BOLD);
            else if (ImGui::IsKeyPressed(ImGuiKey_I, false)) DocApplyOp(OP_ITALIC);
            else if (ImGui::IsKeyPressed(ImGuiKey_U, false)) DocApplyOp(OP_UNDERLINE);
            else if (ImGui::IsKeyPressed(ImGuiKey_Period, false)) DocApplyOp(OP_SUP);
            else if (ImGui::IsKeyPressed(ImGuiKey_Comma, false))  DocApplyOp(OP_SUB);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, true) ||
            ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, true)) {
            if (hasSel) { DocPushSnap(); DocDeleteRange(selA, selB); }
            if (DocAutoFormatLine()) {
                // converted to a rule / dot leader: caret already moved past it
            }
            else if (!gDocParas.empty() && gDocParas[gDocCaret.para].list &&
                strlen(gDocParas[gDocCaret.para].text) == 0) {
                // Enter on an empty list item ends the list (Word behaviour)
                DocPushSnap();
                gDocParas[gDocCaret.para].list = 0;
                gDocLayoutDirty = true;
            } else {
                DocPushSnap();
                DocSplitPara(gDocCaret);
            }
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Backspace, true)) {
            if (hasSel) { DocPushSnap(); DocDeleteRange(selA, selB); }
            else if (gDocCaret.off > 0) {
                DocPushSnap();
                int prev = dt::U8Prev(gDocParas[gDocCaret.para].text, gDocCaret.off);
                DocDeleteRange(DocPos{gDocCaret.para, prev}, gDocCaret);
            } else if (gDocCaret.para > 0) {
                DocPushSnap();
                DocDeleteRange(DocPos{gDocCaret.para - 1, DocParaLen(gDocCaret.para - 1)}, gDocCaret);
            }
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Delete, true)) {
            if (hasSel) { DocPushSnap(); DocDeleteRange(selA, selB); }
            else {
                const char* t = gDocParas[gDocCaret.para].text;
                int len = (int)strlen(t);
                if (gDocCaret.off < len) {
                    DocPushSnap();
                    int nx = gDocCaret.off + dt::U8Seq(t, gDocCaret.off, len);
                    DocDeleteRange(gDocCaret, DocPos{gDocCaret.para, nx});
                } else if (gDocCaret.para + 1 < (int)gDocParas.size()) {
                    DocPushSnap();
                    DocDeleteRange(gDocCaret, DocPos{gDocCaret.para + 1, 0});
                }
            }
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true))  moveH(-1);
        else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) moveH(+1);
        else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))    moveV(-1);
        else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))  moveV(+1);
        else if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
            if (ctrl) DocSetCaret(DocPos{0, 0}, shift); else lineEdge(false);
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_End)) {
            if (ctrl) DocSetCaret(DocPos{(int)gDocParas.size() - 1,
                                         DocParaLen((int)gDocParas.size() - 1)}, shift);
            else lineEdge(true);
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            gDocAnchor = gDocCaret; gDocSelActive = false; gDocFocus = false;
        }
    }

    // ══ Mouse ════════════════════════════════════════════════════════════════
    if (!gDocShapeMode) {
        bool hfClick = false;
        if (cvHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            for (int pg = 0; pg < numPages; pg++) {
                DocPageSetup pst = DocPageSetupAt(pg);
                ImVec2 psz = DocPageSizeOf(pst);
                float phh = psz.y * docScale;
                float pyy = winPos.y + kDocGap * docScale + DocPageTopPx(pg, docScale) - scrollY;
                float mt  = pst.mt * docScale, mb = pst.mb * docScale;
                // The header/footer sit at hd/fd from the edge, which can be
                // outside the top/bottom margin band when the margin is small.
                float hz = std::max(mt, (pst.hd + 60.f) * docScale);
                float fz = std::max(mb, (pst.fd + 60.f) * docScale);
                if (io.MousePos.y >= pyy && io.MousePos.y <= pyy + hz) {
                    gDocHFEdit = 1; gDocHFPage = pg; gDocHFFocus = 8; gDocHFFrames = 0;
                    gDocFocus = false; hfClick = true;
                    ImGui::ClearActiveID();
                    break;
                }
                if (io.MousePos.y >= pyy + phh - fz && io.MousePos.y <= pyy + phh) {
                    gDocHFEdit = 2; gDocHFPage = pg; gDocHFFocus = 8; gDocHFFrames = 0;
                    gDocFocus = false; hfClick = true;
                    ImGui::ClearActiveID();
                    break;
                }
            }
        }
        int tblPi = -1, tblR = -1, tblC = -1;
        if (cvHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !hfClick) {
            for (int tpi = 0; tpi < (int)gDocParas.size(); ++tpi) {
                int ti = gDocParas[tpi].tableIdx;
                if (ti < 0 || ti >= (int)gDocTables.size()) continue;
                if (tpi >= (int)gDocLayout.size()) continue;
                ImVec2 o = DocParaOrigin(tpi);
                float th = DocTableHeight(gDocTables[ti]);
                if (io.MousePos.y < o.y || io.MousePos.y > o.y + th) continue;
                float colW = contentW / (float)std::max(1, gDocTables[ti].cols);
                float lx = io.MousePos.x - o.x;
                if (lx < 0.f || lx >= colW * gDocTables[ti].cols) continue;
                tblPi = tpi;
                tblC  = (int)(lx / colW);
                tblR  = (int)((io.MousePos.y - o.y) / DocTableRowH());
                break;
            }
        }
        if (cvHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !hfClick && tblPi < 0) {
            gDocHFEdit = 0;
            DocPos hit = DocHitTest(io.MousePos);
            double now = ImGui::GetTime();
            if (now - gDocLastClickT < 0.45 && DocPosEq(hit, gDocLastClickPos)) ++gDocClickCnt;
            else gDocClickCnt = 1;
            gDocLastClickT   = now;
            gDocLastClickPos = hit;

            int pi = hit.para;
            const char* t = gDocParas[pi].text;
            if (gDocClickCnt >= 3) {
                gDocAnchor = DocPos{pi, 0};
                gDocCaret  = DocPos{pi, (int)strlen(t)};
                gDocSelActive = true;
                gDocMouseSel  = false;
            } else if (gDocClickCnt == 2) {
                int s = DocWordStart(t, hit.off);
                int e = DocWordEnd(t, hit.off);
                if (s == e) { s = hit.off; e = std::min((int)strlen(t), hit.off + 1); }
                gDocAnchor = DocPos{pi, s};
                gDocCaret  = DocPos{pi, e};
                gDocSelActive = true;
                gDocMouseSel  = false;
            } else if (io.KeyShift) {
                DocSetCaret(hit, true);
                gDocMouseSel = true;
            } else {
                DocSetCaret(hit, false);
                gDocMouseSel = true;
            }
            gDocWantX  = -1.f;
            gDocBlinkT = ImGui::GetTime();
            gDocFocus  = true;
            gDocSel    = pi;
            gDocTableEditPara = -1;
        } else if (tblPi >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            gDocTableEditPara = tblPi;
            gDocTableEditR    = tblR;
            gDocTableEditC    = tblC;
            gDocTableFocus    = 8;          // keep asking for focus for a few frames
            gDocFocus         = false;
            gDocHFEdit        = 0;
            ImGui::ClearActiveID();          // let the cell InputText take over
        }

        if (gDocMouseSel) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (ImGui::GetDragDropPayload() == nullptr) {
                    float topEdge = winPos.y + 12.f, botEdge = winPos.y + canvasH - 12.f;
                    if (io.MousePos.y < topEdge)      ImGui::SetScrollY(std::max(0.f, scrollY - 10.f));
                    else if (io.MousePos.y > botEdge) ImGui::SetScrollY(scrollY + 10.f);
                    DocPos hit = DocHitTest(io.MousePos);
                    if (!DocPosEq(hit, gDocCaret)) { DocSetCaret(hit, true); gDocFocus = true; }
                }
            } else {
                gDocMouseSel = false;
            }
        }
        if (cvHovered) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
    }

    // ══ Draw text ════════════════════════════════════════════════════════════
    DocPos drawA, drawB; DocSelRange(drawA, drawB);
    const bool haveSel = DocHasSel();

    for (int pi = 0; pi < (int)gDocParas.size(); ++pi) {
        if (pi >= (int)gDocLayout.size() || pi >= (int)gDocLines.size()) continue;
        const DocPara& para = gDocParas[pi];
        const DocParaLayout& lo = gDocLayout[pi];
        const dt::ParaLayout& pl = gDocLines[pi];
        ImVec2 o = DocParaOrigin(pi);
        float cx1 = o.x;
        float paraScreenY = o.y;
        // text column of *this* page (landscape pages are wider than portrait)
        const float contentW = DocContentWAt(lo.page, docScale);

        // bullet / number marker, right-aligned in the hanging indent
        if (para.list > 0 && para.imageIdx < 0) {
            char mk[16];
            if (para.list == 1) snprintf(mk, sizeof(mk), "%s", "\xE2\x80\xA2");
            else                snprintf(mk, sizeof(mk), "%d.", gDocListNum[pi]);
            ImFont* mf = gDocFonts[0].regular ? gDocFonts[0].regular : ImGui::GetFont();
            ImVec2 ms = mf->CalcTextSizeA(kDocBaseFS, FLT_MAX, 0.f, mk);
            dl->AddText(mf, kDocBaseFS, {cx1 - 9.f - ms.x, paraScreenY},
                        DocSpanColor(para, DocBaseAttr(para)), mk);
        }

        if (para.imageIdx < 0) {
            if (para.style == DS_Quote && !pl.lines.empty())
                dl->AddRectFilled({cx1 - 10.f, paraScreenY - 2.f},
                                  {cx1 - 4.f,  paraScreenY + pl.h + 2.f},
                                  IM_COL32(80,120,220,200));
            if (para.style == DS_Code && !pl.lines.empty())
                dl->AddRectFilled({cx1 - 8.f, paraScreenY - 4.f},
                                  {cx1 + contentW - lo.indent + 8.f, paraScreenY + pl.h + 4.f},
                                  IM_COL32(238,240,248,255));
        }

        if (para.imageIdx >= 0 && para.imageIdx < (int)gDocImages.size()) {
            const DocImage& img = gDocImages[para.imageIdx];
            float dW = std::min(contentW * (para.imageWidthPct / 100.f), (float)img.srcW * docScale);
            float dH = img.srcH > 0 ? dW * (float)img.srcH / (float)img.srcW : dW * 0.5f;
            if (para.imageLayout == IL_Inline) {
                // figures are centred in the text column, like Word's inline pictures
                float imgX = cx1 + std::max(0.f, (contentW - dW) * 0.5f);
                dl->AddRectFilled({imgX, paraScreenY}, {imgX + dW, paraScreenY + dH}, IM_COL32(255,255,255,255));
                ImGui::SetCursorScreenPos({imgX, paraScreenY});
                char iid[24]; snprintf(iid, sizeof(iid), "##img%d", pi);
                ImGui::InvisibleButton(iid, {std::max(4.f, dW), std::max(4.f, dH)});
                if (ImGui::IsItemClicked()) { DocSetCaret(DocPos{pi, 0}, false); gDocFocus = true; }
                dl->AddImage((ImTextureID)(uintptr_t)img.texID, {imgX, paraScreenY}, {imgX + dW, paraScreenY + dH});
                dl->AddRect({cx1, paraScreenY}, {cx1 + dW, paraScreenY + dH}, IM_COL32(150,160,185,180));
                if (para.caption[0]) {
                    float capFs = kDocBaseFS * 0.85f;
                    ImFont* cf = gDocFonts[0].italic ? gDocFonts[0].italic
                               : (gDocFonts[0].regular ? gDocFonts[0].regular : ImGui::GetFont());
                    ImVec2 cs = cf->CalcTextSizeA(capFs, contentW, 0.f, para.caption);
                    dl->AddText(cf, capFs, {cx1 + (contentW - cs.x) * 0.5f, paraScreenY + dH + 6.f},
                                IM_COL32(90,96,115,255), para.caption, nullptr, contentW);
                }
            } else {
                dl->AddRect({cx1, paraScreenY}, {cx1 + dW, paraScreenY + dH}, IM_COL32(150,160,185,120));
            }
            continue;
        }

        // ── horizontal rule / dot leader ─────────────────────────────────────
        if (para.divider) {
            float midY = paraScreenY + (lo.yEnd - lo.yStart) * 0.5f;
            if (para.divider == 1) {
                dl->AddLine({cx1, midY}, {cx1 + contentW, midY}, IM_COL32(120,126,140,255), 1.2f);
            } else {
                ImFont* f = gDocFonts[0].regular ? gDocFonts[0].regular : ImGui::GetFont();
                static std::string dots;
                float fs = kDocBaseFS * 0.9f;
                float dw = std::max(1.f, f->CalcTextSizeA(fs, FLT_MAX, 0.f, ".").x);
                int n = (int)(contentW / dw);
                if (n < 3) n = 3;
                if ((int)dots.size() != n) dots.assign((size_t)n, '.');
                dl->AddText(f, fs, {cx1, midY - fs * 0.55f}, IM_COL32(120,126,140,255), dots.c_str());
            }
            continue;
        }

        // ── table ────────────────────────────────────────────────────────────
        if (para.tableIdx >= 0 && para.tableIdx < (int)gDocTables.size()) {
            DocTable& tb = gDocTables[para.tableIdx];
            const float rowH = DocTableRowH();
            // Authored column widths + table indent (Word stores both), falling
            // back to equal columns for tables created in the app.
            const float tIndent = tb.styled ? tb.indent * docScale : 0.f;
            const float tAvail  = std::max(40.f, contentW - tIndent);
            std::vector<float> cw((size_t)tb.cols), cx((size_t)tb.cols + 1, 0.f);
            for (int c = 0; c < tb.cols; ++c) cw[(size_t)c] = tb.ColW(c, tAvail);
            cx[0] = cx1 + tIndent;
            for (int c = 0; c < tb.cols; ++c) cx[(size_t)c + 1] = cx[(size_t)c] + cw[(size_t)c];
            ImFont* tf  = gDocFonts[0].regular ? gDocFonts[0].regular : ImGui::GetFont();
            const float tfs = 14.f;

            ImGui::PushFont(tf, tfs);
            ImGui::PushStyleColor(ImGuiCol_FrameBg,        IM_COL32(255,255,255,0));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(90,140,240,26));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  IM_COL32(90,140,240,42));
            ImGui::PushStyleColor(ImGuiCol_Text,           IM_COL32(25,30,45,255));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {3, 3});
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);

            // Cell fills: authored shading wins, otherwise header/body tint.
            for (int r = 0; r < tb.rows; r++) {
                for (int c = 0; c < tb.cols; c++) {
                    ImVec2 a{cx[(size_t)c], paraScreenY + r * rowH};
                    ImVec2 b{cx[(size_t)c + 1], a.y + rowH};
                    ImVec4 fill = (tb.header && r == 0) ? tb.headerFill : tb.bodyFill;
                    if ((size_t)r * tb.cols + c < tb.cellFill.size()) {
                        ImVec4 cf = tb.cellFill[(size_t)r * tb.cols + (size_t)c];
                        if (cf.w > 0.01f) fill = cf;
                    }
                    if (fill.w > 0.01f)
                        dl->AddRectFilled(a, b, ImGui::ColorConvertFloat4ToU32(fill));
                }
            }
            // Borders, side by side the way Word stores them: a table with all six
            // sides set to "none" is borderless, not a full grid.
            if (tb.styled) {
                auto hline = [&](float y, float xa, float xb, const DocBorder& b) {
                    if (b.on && b.w > 0.f)
                        dl->AddLine({xa, y}, {xb, y}, ImGui::ColorConvertFloat4ToU32(b.col), b.w);
                };
                auto vline = [&](float x, float ya, float yb, const DocBorder& b) {
                    if (b.on && b.w > 0.f)
                        dl->AddLine({x, ya}, {x, yb}, ImGui::ColorConvertFloat4ToU32(b.col), b.w);
                };
                const float y0 = paraScreenY, y1 = paraScreenY + tb.rows * rowH;
                hline(y0, cx[0], cx[(size_t)tb.cols], tb.bTop);
                hline(y1, cx[0], cx[(size_t)tb.cols], tb.bBottom);
                vline(cx[0], y0, y1, tb.bLeft);
                vline(cx[(size_t)tb.cols], y0, y1, tb.bRight);
                for (int r = 1; r < tb.rows; ++r) hline(paraScreenY + r * rowH, cx[0], cx[(size_t)tb.cols], tb.bInsideH);
                for (int c = 1; c < tb.cols; ++c) vline(cx[(size_t)c], y0, y1, tb.bInsideV);
            } else if (tb.borderW > 0.01f) {
                for (int r = 0; r < tb.rows; r++)
                    for (int c = 0; c < tb.cols; c++)
                        dl->AddRect({cx[(size_t)c], paraScreenY + r * rowH},
                                    {cx[(size_t)c + 1], paraScreenY + (r + 1) * rowH},
                                    ImGui::ColorConvertFloat4ToU32(tb.borderCol), 0.f, 0, tb.borderW);
            }

            for (int r = 0; r < tb.rows; r++) {
                for (int c = 0; c < tb.cols; c++) {
                    ImVec2 a{cx[(size_t)c], paraScreenY + r * rowH};
                    ImGui::SetCursorScreenPos({a.x + 1.f, a.y + (rowH - tfs - 6.f) * 0.5f});
                    ImGui::SetNextItemWidth(cw[(size_t)c] - 2.f);
                    char cid[48]; snprintf(cid, sizeof(cid), "##tce%d_%d_%d", pi, r, c);
                    char buf[160];
                    snprintf(buf, sizeof(buf), "%s", tb.at(r, c).c_str());
                    bool changed = ImGui::InputText(cid, buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue);
                    if (ImGui::IsItemActive() || ImGui::IsItemFocused()) {
                        gDocFocus = false;                 // the cell owns the keyboard
                        gDocTableEditPara = pi;
                        gDocTableEditR = r;
                        gDocTableEditC = c;
                        gDocCaret = DocPos{pi, 0};
                        gDocSel = pi;
                    }
                    if (changed || ImGui::IsItemDeactivatedAfterEdit()) {
                        if (tb.at(r, c) != buf) {
                            tb.at(r, c) = buf;
                            gDocLayoutDirty = true;
                        }
                    }
                }
            }
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(4);
            ImGui::PopFont();

            // caption under the table
            if (para.caption[0]) {
                float capFs = kDocBaseFS * 0.85f;
                ImFont* cf = gDocFonts[0].italic ? gDocFonts[0].italic : tf;
                ImVec2 cs = cf->CalcTextSizeA(capFs, contentW, 0.f, para.caption);
                dl->AddText(cf, capFs, {cx1 + (contentW - cs.x) * 0.5f,
                                        paraScreenY + DocTableHeight(tb) + 4.f},
                            IM_COL32(90,96,115,255), para.caption, nullptr, contentW);
            }
            continue;
        }

        if (haveSel && pi >= drawA.para && pi <= drawB.para) {
            int s = (pi == drawA.para) ? drawA.off : 0;
            int e = (pi == drawB.para) ? drawB.off : DocParaLen(pi);
            dt::DrawSelectionRects(dl, pl, o, para.text, s, e, IM_COL32(60,120,235,90),
                                   (int)para.align, contentW);
        }

        dt::DrawParagraph(dl, pl, o, para.text, (int)para.align, contentW);
        DocDrawTabLeaders(dl, pl, o, docScale, IM_COL32(110,116,132,255), (int)para.align, contentW);

        if (para.style == DS_H1 || para.style == DS_H2) {
            float sepY = paraScreenY + pl.h + 1.f;
            float sepW = (para.style == DS_H1) ? contentW : contentW * 0.6f;
            ImU32 sc = (para.style == DS_H1) ? IM_COL32(60,100,220,150) : IM_COL32(80,120,230,100);
            dl->AddLine({cx1, sepY}, {cx1 + sepW, sepY}, sc, 1.3f);
        }
    }

    // ── Caret ────────────────────────────────────────────────────────────────
    if (gDocFocus && !gDocParas.empty() &&
        gDocCaret.para < (int)gDocLines.size() &&
        gDocParas[gDocCaret.para].imageIdx < 0) {
        const dt::ParaLayout& pl = gDocLines[gDocCaret.para];
        if (!pl.lines.empty()) {
            float cx, cy, ch;
            const DocPara& cp = gDocParas[gDocCaret.para];
            float cWc = DocContentWAt(gDocLayout[gDocCaret.para].page, docScale);
            dt::CaretMetrics(pl, gDocCaret.off, DocParaOrigin(gDocCaret.para), cx, cy, ch,
                             (int)cp.align, cWc);
            double t = ImGui::GetTime() - gDocBlinkT;
            if (fmod(t, 1.05) < 0.65)
                dl->AddLine({cx, cy + 2.f}, {cx, cy + ch - 2.f}, IM_COL32(20,24,40,235), 1.6f);
        }
    }

    // ── IL_Front images ──────────────────────────────────────────────────────
    for (int pi = 0; pi < (int)gDocParas.size(); pi++) {
        if (pi >= (int)gDocLayout.size()) continue;
        const DocPara& p = gDocParas[pi];
        if (p.imageIdx < 0 || p.imageIdx >= (int)gDocImages.size()) continue;
        if (p.imageLayout != IL_Front) continue;
        const DocImage& img = gDocImages[p.imageIdx];
        const DocParaLayout& lo = gDocLayout[pi];
        DocPageSetup fst = DocPageSetupAt(lo.page);
        float pcY = DocPageCanvasTop(lo.page, docScale);
        float sx  = winPos.x + DocPageLeftIn(canvasW, lo.page, docScale) + fst.ml * docScale;
        float sy  = winPos.y + pcY + lo.yStart - scrollY;
        float dW  = std::min(DocContentWAt(lo.page, docScale) * (p.imageWidthPct / 100.f), (float)img.srcW * docScale);
        float dH  = img.srcH > 0 ? dW * (float)img.srcH / (float)img.srcW : dW * 0.5f;
        dl->AddRectFilled({sx, sy}, {sx + dW, sy + dH}, IM_COL32(255,255,255,255));
        dl->AddImage((ImTextureID)(uintptr_t)img.texID, {sx, sy}, {sx + dW, sy + dH});
        dl->AddRect({sx, sy}, {sx + dW, sy + dH}, IM_COL32(150,160,185,180));
    }

    // ── Paragraph drag handles + drop targets ────────────────────────────────
    bool dragActive = ImGui::GetDragDropPayload() != nullptr;
    for (int pi = 0; pi < (int)gDocParas.size(); ++pi) {
        if (pi >= (int)gDocLayout.size()) continue;
        const DocPara& para = gDocParas[pi];
        const DocParaLayout& lo = gDocLayout[pi];
        float pageCanY = DocPageCanvasTop(lo.page, docScale);
        float pageLeftPi = DocPageLeftIn(canvasW, lo.page, docScale);
        float mlPi = DocPageSetupAt(lo.page).ml;
        {
            float dhW = 18.f, dhH = std::max(lo.yEnd - lo.yStart, 16.f);
            ImGui::SetCursorPos({pageLeftPi + mlPi * docScale - 22.f, pageCanY + lo.yStart});
            char dhid[24]; snprintf(dhid, sizeof(dhid), "##dh%d", pi);
            ImGui::InvisibleButton(dhid, {dhW, dhH});
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                ImGui::SetDragDropPayload("DOC_PARA_MOVE", &pi, sizeof(int));
                ImGui::TextUnformatted(para.imageIdx >= 0 ? "[Image]" : (para.text[0] ? para.text : "(empty)"));
                ImGui::EndDragDropSource();
            }
            if (ImGui::IsItemHovered()) {
                float hx = winPos.x + pageLeftPi + mlPi * docScale - 20.f;
                float hy = winPos.y + pageCanY + lo.yStart - scrollY;
                dl->AddRectFilled({hx, hy}, {hx + 4.f, hy + dhH}, IM_COL32(100,140,230,160), 2.f);
            }
        }
        if (dragActive) {
            float dropY = pageCanY + lo.yEnd;
            ImGui::SetCursorPos({pageLeftPi, dropY - 4.f});
            char dtid[24]; snprintf(dtid, sizeof(dtid), "##dt%d", pi);
            ImGui::InvisibleButton(dtid, {std::max(8.f, DocContentWAt(lo.page, docScale) + mlPi * docScale), 8.f});
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("DOC_PARA_MOVE")) {
                    int from = *(const int*)pl->Data;
                    int to = pi + 1;
                    if (from >= 0 && from < (int)gDocParas.size() && from != to && from + 1 != to) {
                        DocPushSnap();
                        DocPara moved = gDocParas[from];
                        gDocParas.erase(gDocParas.begin() + from);
                        int ins = (from < to) ? to - 1 : to;
                        if (ins > (int)gDocParas.size()) ins = (int)gDocParas.size();
                        gDocParas.insert(gDocParas.begin() + ins, moved);
                        DocSetCaret(DocPos{ins, 0}, false);
                        gDocLayoutDirty = true;
                    }
                }
                dl->AddLine({winPos.x + pageLeftPi + mlPi * docScale, winPos.y + dropY - scrollY},
                            {winPos.x + pageLeftPi + mlPi * docScale + DocContentWAt(lo.page, docScale),
                             winPos.y + dropY - scrollY},
                            IM_COL32(80,140,255,220), 2.f);
                ImGui::EndDragDropTarget();
            }
        }
    }

    // ── Shapes ───────────────────────────────────────────────────────────────
    {
        ImVec2 canvasPos = ImGui::GetWindowPos();
        auto ptToScreen = [&](int pg, float ptx, float pty) -> ImVec2 {
            float pcY = DocPageCanvasTop(pg, docScale);
            return ImVec2(canvasPos.x + DocPageLeftIn(canvasW, pg, docScale) + ptx * docScale,
                          canvasPos.y + pcY + pty * docScale - scrollY);
        };
        auto screenToPt = [&](float sx, float sy, int& pgOut) -> ImVec2 {
            float relY = sy - canvasPos.y + scrollY;
            for (int pg = 0; pg < numPages; pg++) {
                float pcY  = DocPageCanvasTop(pg, docScale);
                float phPg = DocPageSizeOf(DocPageSetupAt(pg)).y * docScale;
                if (relY >= pcY && relY <= pcY + phPg) {
                    pgOut = pg;
                    float relX = sx - canvasPos.x - DocPageLeftIn(canvasW, pg, docScale);
                    return ImVec2(relX / docScale, (relY - pcY) / docScale);
                }
            }
            pgOut = -1; return ImVec2(0, 0);
        };
        for (int si = 0; si < (int)gDocShapes.size(); si++) {
            const DocShape& sh = gDocShapes[si];
            ImVec2 p0 = ptToScreen(sh.page, sh.x, sh.y);
            ImVec2 p1 = ptToScreen(sh.page, sh.x + sh.w, sh.y + sh.h);
            ImU32 fc = ImGui::ColorConvertFloat4ToU32(sh.fill);
            ImU32 sc = ImGui::ColorConvertFloat4ToU32(sh.stroke);
            float lw = sh.thick * docScale;
            switch (sh.type) {
                case SH_Rect:
                    dl->AddRectFilled(p0, p1, fc, 4.f);
                    dl->AddRect(p0, p1, sc, 4.f, 0, lw);
                    break;
                case SH_Oval: {
                    ImVec2 cen = {(p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f};
                    ImVec2 rad = {fabsf(p1.x - p0.x) * 0.5f, fabsf(p1.y - p0.y) * 0.5f};
                    dl->AddEllipseFilled(cen, rad, fc);
                    dl->AddEllipse(cen, rad, sc, 0.f, 0, lw);
                    break; }
                case SH_Line:
                    dl->AddLine(p0, p1, sc, lw);
                    break;
                case SH_Triangle:
                    dl->AddTriangleFilled({(p0.x + p1.x) * 0.5f, p0.y}, {p1.x, p1.y}, {p0.x, p1.y}, fc);
                    dl->AddTriangle({(p0.x + p1.x) * 0.5f, p0.y}, {p1.x, p1.y}, {p0.x, p1.y}, sc, lw);
                    break;
            }
            if (si == gDocSelShape)
                dl->AddRect({p0.x - 2, p0.y - 2}, {p1.x + 2, p1.y + 2}, IM_COL32(80,140,255,200), 4.f, 0, 1.5f);
        }
        if (gDocShapeMode) {
            ImVec2 mp = io.MousePos;
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !gShapeDragging) {
                if (ImGui::IsMouseClicked(0)) {
                    bool hitShape = false;
                    for (int si = (int)gDocShapes.size() - 1; si >= 0; si--) {
                        const DocShape& sh = gDocShapes[si];
                        ImVec2 p0 = ptToScreen(sh.page, sh.x, sh.y);
                        ImVec2 p1 = ptToScreen(sh.page, sh.x + sh.w, sh.y + sh.h);
                        if (mp.x >= p0.x && mp.x <= p1.x && mp.y >= p0.y && mp.y <= p1.y) {
                            gDocSelShape = si; hitShape = true; break;
                        }
                    }
                    if (!hitShape) {
                        int pg = -1;
                        ImVec2 pt = screenToPt(mp.x, mp.y, pg);
                        if (pg >= 0) {
                            gShapeDragging = true;
                            gShapeDragX0 = pt.x; gShapeDragY0 = pt.y;
                            gShapeDragPage = pg;
                            DocShape ns; ns.type = gDocShapeType; ns.page = pg;
                            ns.x = pt.x; ns.y = pt.y; ns.w = 1.f; ns.h = 1.f;
                            gDocShapes.push_back(ns);
                            gDocSelShape = (int)gDocShapes.size() - 1;
                        }
                    }
                }
            }
            if (gShapeDragging && gDocSelShape >= 0 && gDocSelShape < (int)gDocShapes.size()) {
                if (ImGui::IsMouseDown(0)) {
                    int pg = -1;
                    ImVec2 cur = screenToPt(mp.x, mp.y, pg);
                    DocShape& sh = gDocShapes[gDocSelShape];
                    if (pg == gShapeDragPage || pg < 0) {
                        sh.w = std::max(4.f, cur.x - gShapeDragX0);
                        sh.h = std::max(4.f, cur.y - gShapeDragY0);
                    }
                } else {
                    gShapeDragging = false;
                }
            }
            if (gDocSelShape >= 0 && gDocSelShape < (int)gDocShapes.size()) {
                float step = io.KeyShift ? 10.f : 1.f;
                DocShape& sh = gDocShapes[gDocSelShape];
                if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
                    gDocShapes.erase(gDocShapes.begin() + gDocSelShape); gDocSelShape = -1;
                }
                else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))  sh.x -= step;
                else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) sh.x += step;
                else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))    sh.y -= step;
                else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))  sh.y += step;
            }
            if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows))
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    // ── Action bar (status only; file actions live in the top bar) ───────────
    if (!gDocStatus.empty()) {
        ImGui::SameLine(0, 16);
        bool isErr = gDocStatus.rfind("Error", 0) == 0;
        ImGui::PushStyleColor(ImGuiCol_Text, isErr ? Col::danger : Col::success);
        ImGui::TextUnformatted(gDocStatus.c_str());
        ImGui::PopStyleColor();
    }
}

// ─── Document fonts + JS-facing helpers ──────────────────────────────────────
// The document canvas needs four real faces (regular / bold / oblique /
// bold-oblique) at arbitrary sizes; they are embedded in fonts_data.h so no
// extra fetch/preload is required.  The UI font stays the default one.
static void LoadDocFonts()
{
    ImGuiIO& io = ImGui::GetIO();
    if (gDocFonts[0].Valid()) return;
    io.Fonts->AddFontDefault();                 // keep the existing UI look
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;           // data lives in fonts_data.h
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.PixelSnapH  = false;
    sprintf(cfg.Name, "doc-regular");
    auto add = [&](const unsigned char* data, unsigned int len) {
        return io.Fonts->AddFontFromMemoryTTF((void*)data, (int)len, 16.f, &cfg);
    };
    // Every family in fonts_data.h: 4 faces each, loaded from memory. The atlas
    // holds 9 x 4 = 36 faces, all sized at 16 px and scaled at draw time.
    for (int f = 0; f < kDocFamilyCount; ++f) {
        gDocFonts[f].regular    = add(kFontFaces[f][0].data, kFontFaces[f][0].len);
        gDocFonts[f].bold       = add(kFontFaces[f][1].data, kFontFaces[f][1].len);
        gDocFonts[f].italic     = add(kFontFaces[f][2].data, kFontFaces[f][2].len);
        gDocFonts[f].boldItalic = add(kFontFaces[f][3].data, kFontFaces[f][3].len);
    }
}

extern "C" {

// ── export accessors (used by the GUI buttons and by automated tests) ───────
static std::vector<uint8_t> gDocExportBuf;

EMSCRIPTEN_KEEPALIVE int doc_export_docx_size()
{
    gDocExportBuf = DocBuildDocx();
    return (int)gDocExportBuf.size();
}
EMSCRIPTEN_KEEPALIVE const uint8_t* doc_export_docx_data()
{
    return gDocExportBuf.empty() ? nullptr : gDocExportBuf.data();
}
EMSCRIPTEN_KEEPALIVE int doc_export_pdf_size()
{
    gDocExportBuf = BuildDocxPdf(gDocParas);
    return (int)gDocExportBuf.size();
}
EMSCRIPTEN_KEEPALIVE const uint8_t* doc_export_pdf_data()
{
    return gDocExportBuf.empty() ? nullptr : gDocExportBuf.data();
}
// Test hook: how the export pass paginated (pages + element count per page).
EMSCRIPTEN_KEEPALIVE const char* doc_pdf_debug()
{
    static std::string s;
    s = "deffs=" + std::to_string((int)gDocxDefaultFs) +
        " deffam=" + std::to_string(gDocxDefaultFam) +
        " p0fs=" + std::to_string(gDocParas.empty() ? 0 : (int)gDocParas[0].baseFs) +
        " p0fam=" + std::to_string(gDocParas.empty() ? -1 : gDocParas[0].baseFamily) +
        " p1fs=" + std::to_string(gDocParas.size() < 2 ? 0 : (int)gDocParas[1].baseFs) +
        " p1fam=" + std::to_string(gDocParas.size() < 2 ? -1 : gDocParas[1].baseFamily);
    s += " sec=" + std::to_string((int)gDocxSections.size());
    for (size_t i = 0; i < gDocxSections.size(); ++i)
        s += "/" + std::to_string(gDocxSections[i].cols);
    s += " pages=" + std::to_string(gPdfDbgPages) + " paras=" + std::to_string(gPdfDbgParas) +
        " flush=" + std::to_string(gPdfDbgFlush) + " y=" + std::to_string((int)gPdfDbgY) + " elems=";
    for (size_t i = 0; i < gPdfDbgElems.size(); ++i) {
        if (i) s += ',';
        s += std::to_string(gPdfDbgElems[i]);
    }
    return s.c_str();
}

// Test hook: run the same formatting path the toolbar buttons use.
EMSCRIPTEN_KEEPALIVE void doc_debug_apply(int op, float r, float g, float b, float a)
{
    if (op == 20) { DocApplyList(1); return; }
    if (op == 21) { DocApplyList(2); return; }
    if (op == 22) { DocApplyList(0); return; }
    DocApplyOp(op, ImVec4(r, g, b, a));
}

// Paste from the DOM clipboard (shell.html forwards the paste event).
EMSCRIPTEN_KEEPALIVE void doc_paste_text(const char* txt)
{
    if (!txt) return;
    std::string in(txt);
    if (in.empty()) return;
    std::string s;                              // normalise newlines
    s.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c == '\r') {
            if (i + 1 < in.size() && in[i + 1] == '\n') continue;
            s.push_back('\n');
        } else {
            s.push_back(c);
        }
    }
    DocPushSnap();
    if (DocHasSel()) { DocPos a, b; DocSelRange(a, b); DocDeleteRange(a, b); }
    gDocClipParas.clear();                      // plain text only
    gDocClipText = s;
    DocInsertRaw(gDocCaret, s, DocTypingAttrNow());
    gDocFocus = true;
}

// Introspection for automated tests / debugging (returns a JSON string).
// test hook: set the column count of one page
EMSCRIPTEN_KEEPALIVE void doc_debug_page_columns(int pg, int cols, float gap)
{
    DocPageSetup st = DocPageSetupAt(pg);
    st.cols = cols < 1 ? 1 : (cols > 3 ? 3 : cols);
    st.colGap = gap;
    DocSetPageSetup(pg, st);
    gDocLayoutDirty = true;
}

// test hook: configure one page's layout without driving the UI
EMSCRIPTEN_KEEPALIVE void doc_debug_page_setup(int pg, int landscape, float ml, float mr,
                                               float mt, float mb, int header, int footer)
{
    DocPageSetup st;
    st.landscape = landscape != 0;
    st.ml = ml; st.mr = mr; st.mt = mt; st.mb = mb;
    st.header = header != 0;
    st.footer = footer != 0;
    DocSetPageSetup(pg, st);
    gDocLayoutDirty = true;
}

// test hook: header/footer text (multi-line, '\n' separated) + their distances
EMSCRIPTEN_KEEPALIVE void doc_debug_hf(const char* header, const char* footer, float hd, float fd)
{
    if (header) { strncpy(gDocHeader, header, sizeof(gDocHeader) - 1); gDocHeader[sizeof(gDocHeader)-1] = 0; }
    if (footer) { strncpy(gDocFooter, footer, sizeof(gDocFooter) - 1); gDocFooter[sizeof(gDocFooter)-1] = 0; }
    for (int i = 0; i < std::max(1, (int)gDocPageTops.size()); ++i) {
        DocPageSetup ps = DocPageSetupAt(i);
        if (hd >= 0.f) ps.hd = hd;
        if (fd >= 0.f) ps.fd = fd;
        DocSetPageSetup(i, ps);
    }
    gDocLayoutDirty = true;
}

// test hook: paragraph alignment + spacing on the current selection
EMSCRIPTEN_KEEPALIVE void doc_debug_para_fmt(int align, float before, float after, float line)
{
    DocPos a, b;
    DocSelRange(a, b);
    DocPushSnap();
    for (int i = a.para; i <= b.para && i < (int)gDocParas.size(); ++i) {
        if (align >= 0) gDocParas[i].align = (DocAlign)align;
        if (before >= -1.f) gDocParas[i].spaceBefore = before;
        if (after >= -1.f)  gDocParas[i].spaceAfter  = after;
        if (line >= 0.f)    gDocParas[i].lineSpacing = line;
    }
    gDocLayoutDirty = true;
}

// test hook: set one tab stop (or clear all with x < 0) without driving the UI
EMSCRIPTEN_KEEPALIVE void doc_debug_tab(int idx, float xPt, int leader)
{
    if (xPt < 0.f) { gDocTabs.clear(); gDocLayoutDirty = true; return; }
    if (idx < 0) { DocTabStop t; t.x = xPt; t.leader = leader; gDocTabs.push_back(t); }
    else if (idx < (int)gDocTabs.size()) { gDocTabs[(size_t)idx].x = xPt; gDocTabs[(size_t)idx].leader = leader; }
    else { DocTabStop t; t.x = xPt; t.leader = leader; gDocTabs.push_back(t); }
    DocTabsSort();
    gDocLayoutDirty = true;
}
// test hook: insert a tab at the caret (same path as the Tab key)
EMSCRIPTEN_KEEPALIVE void doc_debug_insert_tab()
{
    DocPushSnap();
    DocInsertRaw(gDocCaret, std::string(1, '\t'), DocTypingAttrNow());
    gDocLayoutDirty = true;
}

EMSCRIPTEN_KEEPALIVE int doc_imported_math_count() { return (int)gImportedMathPara.size(); }
EMSCRIPTEN_KEEPALIVE int doc_imported_math_para(int i)
{
    if (i < 0 || i >= (int)gImportedMathPara.size()) return -1;
    return gImportedMathPara[(size_t)i];
}
EMSCRIPTEN_KEEPALIVE const char* doc_imported_math_tex(int i)
{
    if (i < 0 || i >= (int)gImportedMathTex.size()) return "";
    return gImportedMathTex[(size_t)i].c_str();
}

EMSCRIPTEN_KEEPALIVE int doc_imported_image_count() { return (int)gImportedImgPara.size(); }
EMSCRIPTEN_KEEPALIVE int doc_imported_image_para(int i)
{
    if (i < 0 || i >= (int)gImportedImgPara.size()) return -1;
    return gImportedImgPara[(size_t)i];
}
EMSCRIPTEN_KEEPALIVE const char* doc_imported_image_path(int i)
{
    if (i < 0 || i >= (int)gImportedImgPath.size()) return "";
    return gImportedImgPath[(size_t)i].c_str();
}

// Attach decoded RGBA pixels to an imported image placeholder paragraph.
EMSCRIPTEN_KEEPALIVE void doc_attach_image(int paraIdx, const uint8_t* rgba, int w, int h)
{
    if (paraIdx < 0 || paraIdx >= (int)gDocParas.size() || !rgba || w <= 0 || h <= 0) return;
    DocImage img; img.srcW = w; img.srcH = h;
    img.pixels.assign(rgba, rgba + (size_t)w * h * 4);
    GLuint tex; glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    img.texID = (unsigned int)tex;

    DocPara& p = gDocParas[paraIdx];
    p.imageIdx = (int)gDocImages.size();
    gDocImages.push_back(img);
    if (p.imgWpt > 1.f) {
        float colW = 452.f;                       // A4 text width, points
        p.imageWidthPct = std::max(10.f, std::min(100.f, p.imgWpt / colW * 100.f));
    }
    p.imageLayout = IL_Inline;
    gDocLayoutDirty = true;
}

EMSCRIPTEN_KEEPALIVE const char* doc_debug_state()
{
    static std::string out;
    auto esc = [](const std::string& s) {
        std::string r;
        for (char c : s) {
            if (c == '"')       r += "\\\"";
            else if (c == '\\') r += "\\\\";
            else if (c == '\n') r += "\\n";
            else if (c == '\t') r += "\\t";
            else if ((unsigned char)c < 0x20) r += ' ';
            else r += c;
        }
        return r;
    };
    auto attrFlags = [](const DocAttr& a) {
        std::string f;
        if (a.bold) f += 'B';
        if (a.italic) f += 'I';
        if (a.underline) f += 'U';
        if (a.strike) f += 'S';
        if (a.vert > 0) f += '+';
        else if (a.vert < 0) f += '-';
        if (a.hasColor) f += 'c';
        if (a.hasHL) f += 'h';
        return f;
    };
    DocPos a, b;
    DocSelRange(a, b);
    out  = "{";
    out += "\"paras\":"   + std::to_string(gDocParas.size());
    out += ",\"caret\":[" + std::to_string(gDocCaret.para) + "," + std::to_string(gDocCaret.off) + "]";
    out += ",\"anchor\":["+ std::to_string(gDocAnchor.para)+ "," + std::to_string(gDocAnchor.off)+ "]";
    out += ",\"sel\":"    + std::to_string(DocHasSel() ? 1 : 0);
    out += ",\"lo\":"     + std::to_string(a.para);
    out += ",\"hi\":"     + std::to_string(b.para);
    out += ",\"focus\":"  + std::to_string(gDocFocus ? 1 : 0);
    out += ",\"hf\":" + std::to_string(gDocHFEdit) + ",\"hfpage\":" + std::to_string(gDocHFPage);
    out += ",\"hdr\":\"" + esc(gDocHeader) + "\",\"ftr\":\"" + esc(gDocFooter) + "\"";
    out += ",\"pages\":" + std::to_string((int)gDocPageTops.size());
    out += ",\"cols\":[";
    for (size_t i = 0; i < gDocPageSetups.size(); ++i) {
        if (i) out += ",";
        out += std::to_string(gDocPageSetups[i].cols);
    }
    out += "]";
    out += ",\"selText\":\"" + esc(DocSelectedText()) + "\"";
    out += ",\"lines\":[";
    for (int i = 0; i < (int)gDocLines.size(); ++i) {
        if (i) out += ",";
        out += std::to_string(gDocLines[i].lines.size());
    }
    out += "],\"lists\":[";
    for (size_t i = 0; i < gDocParas.size(); ++i) { if (i) out += ","; out += std::to_string(gDocParas[i].list); }
    out += "],\"dump\":[";
    for (size_t i = 0; i < gDocParas.size(); ++i) {
        if (i) out += ",";
        const DocPara& p = gDocParas[i];
        out += "{\"n\":" + std::to_string((int)strlen(p.text)) +
               ",\"r\":" + std::to_string((int)p.runs.size()) +
               ",\"s\":" + std::to_string((int)p.style) +
               ",\"al\":" + std::to_string((int)p.align) +
               ",\"sb\":" + std::to_string((int)p.spaceBefore) +
               ",\"sa\":" + std::to_string((int)p.spaceAfter) +
               ",\"ls\":" + std::to_string((double)p.lineSpacing) +
               ",\"tb\":" + std::to_string(p.tableIdx) +
               ",\"t\":\"" + esc(std::string(p.text, strnlen(p.text, 60))) + "\"}";
    }
    // per-line x offset inside the text column (proves alignment is applied)
    out += "],\"lineoffs\":[";
    for (size_t i = 0; i < gDocLines.size(); ++i) {
        if (i) out += ",";
        out += "[";
        int pg = (i < gDocLayout.size()) ? gDocLayout[i].page : 0;
        float cw = DocContentWAt(pg, gDocView.docScale);
        int al = (i < gDocParas.size()) ? (int)gDocParas[i].align : 0;
        for (size_t li = 0; li < gDocLines[i].lines.size(); ++li) {
            if (li) out += ",";
            char tmp[32];
            snprintf(tmp, sizeof(tmp), "%.1f", dt::LineOffX(gDocLines[i].lines[li], al, cw));
            out += tmp;
        }
        out += "]";
    }
    out += "],\"flags\":[";
    for (size_t i = 0; i < gDocParas.size(); ++i) {
        if (i) out += ",";
        const DocPara& p = gDocParas[i];
        std::string f;
        if (p.bold) f += 'B';
        if (p.italic) f += 'I';
        if (p.underline) f += 'U';
        if (p.strike) f += 'S';
        if (p.vertAlign > 0) f += '+';
        else if (p.vertAlign < 0) f += '-';
        if (p.imageIdx >= 0) f += 'M';
        out += "\"" + f + "\"";
    }
    out += "],\"fonts\":";
    out += std::to_string((gDocFonts[0].regular?1:0) | (gDocFonts[0].bold?2:0) |
                          (gDocFonts[0].italic?4:0) | (gDocFonts[0].boldItalic?8:0) |
                          (gDocFonts[1].regular?16:0) | (gDocFonts[2].regular?32:0));
    // screen-space origin + height of every paragraph (lets tests click text)
    out += ",\"origins\":[";
    for (int i = 0; i < (int)gDocParas.size() && i < (int)gDocLayout.size(); ++i) {
        if (i) out += ",";
        ImVec2 o = DocParaOrigin(i);
        char tmp[96];
        snprintf(tmp, sizeof(tmp), "[%.1f,%.1f,%.1f]", o.x, o.y,
                 gDocLayout[i].yEnd - gDocLayout[i].yStart);
        out += tmp;
    }
    out += "],\"docScale\":";
    { char tmp[32]; snprintf(tmp, sizeof(tmp), "%.4f", gDocView.docScale); out += tmp; }
    // tab stops + the gaps the layout actually produced (test hook)
    out += ",\"tabs\":[";
    for (size_t i = 0; i < gDocTabs.size(); ++i) {
        if (i) out += ",";
        char tmp[48]; snprintf(tmp, sizeof(tmp), "[%.1f,%d]", gDocTabs[(size_t)i].x, gDocTabs[(size_t)i].leader);
        out += tmp;
    }
    out += "],\"tabgaps\":[";
    {
        bool first = true;
        for (size_t pi = 0; pi < gDocLines.size(); ++pi) {
            for (const dt::Line& ln : gDocLines[pi].lines) {
                for (const dt::Piece& pc : ln.pieces) {
                    if (!pc.tab) continue;
                    if (!first) out += ",";
                    first = false;
                    char tmp[64];
                    snprintf(tmp, sizeof(tmp), "[%d,%.1f,%.1f]", (int)pi, pc.x, pc.w);
                    out += tmp;
                }
            }
        }
    }
    out += "],\"pagesetup\":[";
    {
        int np = std::max(1, (int)gDocPageTops.size());
        for (int i = 0; i < np; ++i) {
            if (i) out += ",";
            DocPageSetup ps = DocPageSetupAt(i);
            ImVec2 psz = DocPageSizeOf(ps);
            char tmp[128];
            snprintf(tmp, sizeof(tmp), "{\"ls\":%d,\"ml\":%.0f,\"mr\":%.0f,\"mt\":%.0f,\"mb\":%.0f,\"hd\":%.0f,\"fd\":%.0f,\"w\":%.0f,\"h\":%.0f}",
                     ps.landscape ? 1 : 0, ps.ml, ps.mr, ps.mt, ps.mb, ps.hd, ps.fd, psz.x, psz.y);
            out += tmp;
        }
    }
    out += "]";
    // per-run formatting: [start,end,"flags"] for every paragraph
    out += ",\"runs\":[";
    for (size_t i = 0; i < gDocParas.size(); ++i) {
        if (i) out += ",";
        out += "[";
        int acc = 0;
        const DocPara& p = gDocParas[i];
        for (size_t r = 0; r < p.runs.size(); ++r) {
            if (r) out += ",";
            out += "[" + std::to_string(acc) + "," + std::to_string(acc + p.runs[r].len)
                 + ",\"" + attrFlags(p.runs[r].a) + "\"]";
            acc += p.runs[r].len;
        }
        out += "]";
    }
    out += "]";
    out += ",\"key\":[";
    { char tmp[96]; snprintf(tmp, sizeof(tmp), "%d,%d,%d,%d,%d",
        gDbgKeyCode, gDbgKeyDown, gDbgKeyCount, gDbgCtrl, gDbgShift); out += tmp; }
    out += "]";
    // caret rectangle in CSS pixels relative to the canvas element
    out += ",\"caretScreen\":[";
    if (!gDocLines.empty() && gDocCaret.para < (int)gDocLines.size() &&
        !gDocLines[gDocCaret.para].lines.empty()) {
        float cx, cy, ch;
        {
            const DocPara& cp = gDocParas[gDocCaret.para];
            float cWc = DocContentWAt(gDocLayout[gDocCaret.para].page, gDocView.docScale);
            dt::CaretMetrics(gDocLines[gDocCaret.para], gDocCaret.off,
                             DocParaOrigin(gDocCaret.para), cx, cy, ch, (int)cp.align, cWc);
        }
        char tmp[96];
        snprintf(tmp, sizeof(tmp), "%.1f,%.1f,%.1f", cx, cy, ch);
        out += tmp;
    }
    out += "]";
    out += ",\"lineH\":";
    { char tmp[32]; snprintf(tmp, sizeof(tmp), "%.2f",
        gDocLines.empty() || gDocLines[0].lines.empty() ? 0.f : gDocLines[0].lineH); out += tmp; }
    out += "}";
    return out.c_str();
}

} // extern "C"

// ─── Main loop and entry point ────────────────────────────────────────────────
static void main_loop()
{
    // vsync is implicit: the main loop is requestAnimationFrame driven
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);
        if (event.type == SDL_QUIT) emscripten_cancel_main_loop();
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    gDbgCtrl = io.KeyCtrl ? 1 : 0;
    gDbgShift = io.KeyShift ? 1 : 0;

    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {16, 12});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
    ImGui::Begin("##main", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar(2);

    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float  W = io.DisplaySize.x - 32;
        ImGui::GetWindowDrawList()->AddRectFilled(
            {p.x - 16, p.y - 12},
            {p.x + W + 16, p.y + 36},
            ToU32(Col::surface));
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
        ImGui::PushStyleColor(ImGuiCol_Text, Col::primary);
        ImGui::Text("  WASM App");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, Col::muted);
        ImGui::Text("  C++ running in your browser");
        ImGui::PopStyleColor();
        ImGui::SameLine(io.DisplaySize.x - 120);
        Badge("  LIVE  ", {0.133f,0.773f,0.369f,0.2f}, Col::success);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
    }

    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {14, 8});
    if (ImGui::BeginTabBar("##tabs", ImGuiTabBarFlags_None))
    {
        if (ImGui::BeginTabItem("  Dashboard  ")) { ImGui::Spacing(); TabDashboard(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("  Profile  "))   { ImGui::Spacing(); TabProfile();   ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("  Settings  "))  { ImGui::Spacing(); TabSettings();  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("  Spreadsheet  ")){ ImGui::Spacing(); TabSpreadsheet(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("  Feed  "))      { ImGui::Spacing(); TabFeed();      ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("  PDF Editor  ")){ ImGui::Spacing(); TabPdfEditor(); ImGui::EndTabItem(); }
        {
            ImGuiTabItemFlags dflags = gDocTabPending ? ImGuiTabItemFlags_SetSelected : 0;
            if (ImGui::BeginTabItem("  Docx  ", nullptr, dflags)) {
                gDocTabPending = false;
                ImGui::Spacing(); TabDocx();      ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    ImGui::PopStyleVar();

    ImGui::End();
    DrawModals();

    ImGui::Render();
    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    glClearColor(Col::bg.x, Col::bg.y, Col::bg.z, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(g.window);
}

int main(int, char**)
{
    g.init();
    start_rss_fetch();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { printf("SDL: %s\n", SDL_GetError()); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    g.window = SDL_CreateWindow("WASM App",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!g.window) { printf("SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    g.gl_context = SDL_GL_CreateContext(g.window);
    if (!g.gl_context) { printf("SDL_GL_CreateContext: %s\n", SDL_GetError()); return 1; }
    SDL_GL_MakeCurrent(g.window, g.gl_context);
    // NOTE: SDL_GL_SetSwapInterval() maps to emscripten_set_main_loop_timing()
    // which warns (and does nothing) until emscripten_set_main_loop() has been
    // called — so it is applied from the first main_loop iteration instead.
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    SetupModernStyle();
    LoadDocFonts();                 // document family: regular / bold / oblique / bold-oblique
#ifdef __EMSCRIPTEN__
    // "?tab=docx" opens the document editor directly (deep link / tests)
    gDocTabPending = EM_ASM_INT({
        return (window.location.search || "").indexOf("tab=docx") >= 0 ? 1 : 0;
    }) != 0;
#endif
    ImGui_ImplSDL2_InitForOpenGL(g.window, g.gl_context);
    ImGui_ImplOpenGL3_Init("#version 100");
    emscripten_set_main_loop(main_loop, 0, true);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(g.gl_context);
    SDL_DestroyWindow(g.window);
    SDL_Quit();
    return 0;
}