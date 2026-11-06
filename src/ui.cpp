#include "ui.h"
#include "config.h"
#include "audio.h"
#include "font_data.h"
#include <cstdarg>

namespace ax {
namespace ui {

struct FontSet { Font f; int size; };
static std::vector<FontSet> g_med, g_bold;
static int g_captureId = -1;
static int g_focusId = -1;
static int g_lastHover = -1;
static float g_scale = 1;
static Rectangle g_scrollR{};
static bool g_inScroll = false;
static float* g_scrollV = nullptr;
static float g_scrollContent = 0;
static const char* g_tooltip = nullptr;
static int g_sliderDrag = -1;
static int g_widgetIdx = 0;

static std::vector<int> codepoints() {
    std::vector<int> cps;
    for (int c = 32; c < 127; c++) cps.push_back(c);
    for (int c = 160; c < 256; c++) cps.push_back(c);
    int extra[] = {0x152, 0x153, 0x2019, 0x2026, 0x2013, 0x2014, 0x20AC, 0x25B6, 0x25C0, 0x2022, 0x00D7, 0x2191, 0x2193};
    for (int c : extra) cps.push_back(c);
    return cps;
}

void init() {
    auto cps = codepoints();
    int sizes[] = {18, 24, 32, 48, 80};
    for (int s : sizes) {
        Font m = LoadFontFromMemory(".ttf", kFontMedium, (int)kFontMediumSize, s, cps.data(), (int)cps.size());
        Font b = LoadFontFromMemory(".ttf", kFontBold, (int)kFontBoldSize, s, cps.data(), (int)cps.size());
        GenTextureMipmaps(&m.texture); GenTextureMipmaps(&b.texture);
        SetTextureFilter(m.texture, TEXTURE_FILTER_TRILINEAR);
        SetTextureFilter(b.texture, TEXTURE_FILTER_TRILINEAR);
        g_med.push_back({m, s});
        g_bold.push_back({b, s});
    }
}
void shutdown() {
    for (auto& f : g_med) UnloadFont(f.f);
    for (auto& f : g_bold) UnloadFont(f.f);
    g_med.clear(); g_bold.clear();
}

void beginFrame() {
    g_scale = GetScreenHeight() / 1080.0f;
    g_tooltip = nullptr;
    g_widgetIdx = 0;
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) g_sliderDrag = -1;
}
float scale() { return g_scale; }
float S(float v) { return v * g_scale; }

static const Font& pick(float size, bool bold) {
    auto& set = bold ? g_bold : g_med;
    for (auto& f : set) if (f.size >= size * 0.92f) return f.f;
    return set.back().f;
}

const char* fmt(const char* f, ...) {
    static char bufs[8][512];
    static int idx = 0;
    char* b = bufs[idx++ & 7];
    va_list ap; va_start(ap, f);
    vsnprintf(b, 512, f, ap);
    va_end(ap);
    return b;
}

float textWidth(const char* s, float size, bool bold) {
    if (g_med.empty()) return MeasureText(s, (int)size);
    return MeasureTextEx(pick(size, bold), s, size, size * 0.02f).x;
}

void text(const char* s, float x, float y, float size, Color c, bool bold, int align) {
    if (g_med.empty()) { DrawText(s, (int)x, (int)y, (int)size, c); return; }
    const Font& f = pick(size, bold);
    float w = (align != AL_LEFT) ? MeasureTextEx(f, s, size, size * 0.02f).x : 0;
    if (align == AL_CENTER) x -= w * 0.5f;
    else if (align == AL_RIGHT) x -= w;
    DrawTextEx(f, s, {floorf(x), floorf(y)}, size, size * 0.02f, c);
}
void textShadow(const char* s, float x, float y, float size, Color c, bool bold, int align) {
    text(s, x + std::max(1.f, size * 0.06f), y + std::max(1.f, size * 0.06f), size, Color{0, 0, 0, (unsigned char)(c.a * 0.7f)}, bold, align);
    text(s, x, y, size, c, bold, align);
}

void panel(Rectangle r, Color c, float round) {
    if (round > 0) DrawRectangleRounded(r, round, 6, c);
    else DrawRectangleRec(r, c);
}
void frame(Rectangle r, Color c, float thick) { DrawRectangleLinesEx(r, thick, c); }

bool hovered(Rectangle r) {
    Vector2 m = GetMousePosition();
    if (g_inScroll && !CheckCollisionPointRec(m, g_scrollR)) return false;
    return CheckCollisionPointRec(m, r);
}

static void hoverSound(int id) {
    if (g_lastHover != id) { g_lastHover = id; g_audio.play2D(SFX_UI_HOVER, 0.5f, 1.0f, BUS_UI); }
}

bool button(Rectangle r, const char* label, bool primary, bool enabled) {
    int id = ++g_widgetIdx;
    bool h = enabled && hovered(r);
    if (h) hoverSound(id);
    Color bgc = primary ? theme::accent : theme::panel2;
    if (!enabled) bgc = Color{30, 34, 44, 200};
    else if (h) bgc = primary ? Color{120, 240, 255, 255} : Color{40, 50, 70, 245};
    panel(r, bgc);
    if (h && !primary) DrawRectangle((int)r.x, (int)r.y, (int)S(4), (int)r.height, theme::accent);
    Color tc = primary ? Color{5, 12, 20, 255} : (enabled ? theme::text : theme::dim);
    float fs = std::min(r.height * 0.42f, S(26));
    text(label, r.x + r.width / 2, r.y + (r.height - fs) / 2 - S(1), fs, tc, true, AL_CENTER);
    bool clicked = h && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (clicked) g_audio.play2D(SFX_UI_CLICK, 0.7f, 1.0f, BUS_UI);
    return clicked;
}

bool tab(Rectangle r, const char* label, bool active) {
    int id = ++g_widgetIdx;
    bool h = hovered(r);
    if (h) hoverSound(id);
    if (active) DrawRectangle((int)r.x, (int)(r.y + r.height - S(3)), (int)r.width, (int)S(3), theme::accent);
    float fs = S(20);
    text(label, r.x + r.width / 2, r.y + (r.height - fs) / 2, fs, active ? theme::text : (h ? Color{200, 210, 225, 255} : theme::dim), true, AL_CENTER);
    bool c = h && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (c) g_audio.play2D(SFX_UI_CLICK, 0.6f, 1.1f, BUS_UI);
    return c;
}

static void rowBg(Rectangle r, bool h) {
    panel(r, h ? Color{30, 38, 56, 220} : Color{18, 23, 34, 200});
}

bool slider(Rectangle r, const char* label, float* v, float mn, float mx, const char* valueFmt, float step) {
    int id = ++g_widgetIdx;
    bool h = hovered(r);
    rowBg(r, h || g_sliderDrag == id);
    float fs = S(19);
    text(label, r.x + S(14), r.y + (r.height - fs) / 2, fs, theme::text);
    Rectangle bar = {r.x + r.width * 0.52f, r.y + r.height / 2 - S(3), r.width * 0.33f, S(6)};
    Rectangle hit = {bar.x - S(6), r.y, bar.width + S(12), r.height};
    if (hovered(hit) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) g_sliderDrag = id;
    bool changed = false;
    if (g_sliderDrag == id) {
        float t = clampf((GetMousePosition().x - bar.x) / bar.width, 0, 1);
        float nv = mn + (mx - mn) * t;
        if (step > 0) nv = roundf(nv / step) * step;
        if (nv != *v) { *v = nv; changed = true; }
    }
    float t = clampf((*v - mn) / (mx - mn), 0, 1);
    DrawRectangleRec(bar, Color{50, 58, 76, 255});
    DrawRectangle((int)bar.x, (int)bar.y, (int)(bar.width * t), (int)bar.height, theme::accent);
    DrawCircle((int)(bar.x + bar.width * t), (int)(bar.y + bar.height / 2), S(8), WHITE);
    text(fmt(valueFmt, *v), r.x + r.width - S(14), r.y + (r.height - fs) / 2, fs, theme::accent, true, AL_RIGHT);
    return changed;
}

bool sliderInt(Rectangle r, const char* label, int* v, int mn, int mx) {
    float f = (float)*v;
    bool c = slider(r, label, &f, (float)mn, (float)mx, "%.0f", 1);
    *v = (int)roundf(f);
    return c;
}

bool toggle(Rectangle r, const char* label, bool* v) {
    int id = ++g_widgetIdx;
    bool h = hovered(r);
    if (h) hoverSound(id);
    rowBg(r, h);
    float fs = S(19);
    text(label, r.x + S(14), r.y + (r.height - fs) / 2, fs, theme::text);
    Rectangle sw = {r.x + r.width - S(70), r.y + r.height / 2 - S(12), S(52), S(24)};
    DrawRectangleRounded(sw, 1.0f, 8, *v ? theme::accent : Color{60, 66, 82, 255});
    DrawCircle((int)(*v ? sw.x + sw.width - S(12) : sw.x + S(12)), (int)(sw.y + S(12)), S(9), WHITE);
    if (h && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { *v = !*v; g_audio.play2D(SFX_UI_CLICK, 0.6f, 1.0f, BUS_UI); return true; }
    return false;
}

bool choice(Rectangle r, const char* label, int* idx, const char* const* opts, int n) {
    int id = ++g_widgetIdx;
    bool h = hovered(r);
    if (h) hoverSound(id);
    rowBg(r, h);
    float fs = S(19);
    text(label, r.x + S(14), r.y + (r.height - fs) / 2, fs, theme::text);
    float w = r.width * 0.4f;
    Rectangle L = {r.x + r.width - w - S(10), r.y + S(6), S(30), r.height - S(12)};
    Rectangle R = {r.x + r.width - S(40), r.y + S(6), S(30), r.height - S(12)};
    bool changed = false;
    *idx = std::clamp(*idx, 0, n - 1);
    bool hl = hovered(L), hr = hovered(R);
    panel(L, hl ? theme::accent : Color{40, 48, 66, 255});
    panel(R, hr ? theme::accent : Color{40, 48, 66, 255});
    text("<", L.x + L.width / 2, L.y + (L.height - fs) / 2 - S(1), fs, hl ? BLACK : theme::text, true, AL_CENTER);
    text(">", R.x + R.width / 2, R.y + (R.height - fs) / 2 - S(1), fs, hr ? BLACK : theme::text, true, AL_CENTER);
    text(opts[*idx], (L.x + L.width + R.x) / 2, r.y + (r.height - fs) / 2, fs, theme::accent, true, AL_CENTER);
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (hl) { *idx = (*idx + n - 1) % n; changed = true; }
        else if (hr || (h && !hl && !hr && GetMousePosition().x > L.x)) { *idx = (*idx + 1) % n; changed = true; }
    }
    if (changed) g_audio.play2D(SFX_UI_CLICK, 0.6f, 1.0f, BUS_UI);
    return changed;
}

bool keybind(Rectangle r, int* code, int id) {
    bool h = hovered(r);
    bool capturing = g_captureId == id;
    panel(r, capturing ? Color{255, 196, 60, 255} : (h ? Color{40, 50, 70, 255} : Color{30, 36, 50, 255}));
    float fs = S(18);
    text(capturing ? "..." : bindName(*code), r.x + r.width / 2, r.y + (r.height - fs) / 2, fs, capturing ? BLACK : theme::text, true, AL_CENTER);
    if (capturing) {
        int k = pollAnyBindInput();
        if (k == KEY_ESCAPE) { g_captureId = -1; return false; }
        if (k == KEY_BACKSPACE || k == KEY_DELETE) { *code = BIND_NONE; g_captureId = -1; return true; }
        if (k != 0) {
            *code = k; g_captureId = -1;
            g_audio.play2D(SFX_UI_CLICK, 0.7f, 1.2f, BUS_UI);
            return true;
        }
        return false;
    }
    if (h && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { g_captureId = id; g_audio.play2D(SFX_UI_CLICK, 0.6f, 1.0f, BUS_UI); }
    return false;
}

bool capturingKey() { return g_captureId >= 0; }
void cancelCapture() { g_captureId = -1; }
bool anyTextFocused() { return g_focusId >= 0; }
void clearFocus() { g_focusId = -1; }

bool textField(Rectangle r, char* buf, int cap, int id, const char* placeholder) {
    bool h = hovered(r);
    bool focus = g_focusId == id;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (h) g_focusId = id;
        else if (focus) g_focusId = -1;
        focus = g_focusId == id;
    }
    panel(r, focus ? Color{30, 40, 60, 255} : Color{20, 26, 38, 255});
    frame(r, focus ? theme::accent : Color{60, 70, 90, 255}, focus ? 2.f : 1.f);
    float fs = S(19);
    bool changed = false;
    if (focus) {
        int c;
        while ((c = GetCharPressed()) > 0) {
            int len = (int)strlen(buf);
            if (c >= 32 && c < 127 && len < cap - 1) { buf[len] = (char)c; buf[len + 1] = 0; changed = true; }
        }
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && strlen(buf) > 0) { buf[strlen(buf) - 1] = 0; changed = true; }
        if (IsKeyPressed(KEY_V) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) {
            const char* clip = GetClipboardText();
            if (clip) { strncat(buf, clip, cap - strlen(buf) - 1); changed = true; }
        }
        if (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_ESCAPE)) g_focusId = -1;
    }
    bool empty = buf[0] == 0;
    text(empty && !focus ? placeholder : buf, r.x + S(10), r.y + (r.height - fs) / 2, fs, empty ? theme::dim : theme::text);
    if (focus && fmodf((float)GetTime(), 1.0f) < 0.5f) {
        float w = textWidth(buf, fs);
        DrawRectangle((int)(r.x + S(11) + w), (int)(r.y + S(8)), (int)S(2), (int)(r.height - S(16)), theme::accent);
    }
    return changed;
}

void tooltip(const char* s) { g_tooltip = s; }

void beginScroll(Rectangle r, float* scroll, float contentH) {
    g_scrollR = r; g_scrollV = scroll; g_scrollContent = contentH;
    if (CheckCollisionPointRec(GetMousePosition(), r) && g_sliderDrag < 0) {
        float wheel = GetMouseWheelMove();
        bool overSlider = false;
        (void)overSlider;
        *scroll -= wheel * S(60);
    }
    float maxS = std::max(0.f, contentH - r.height);
    *scroll = clampf(*scroll, 0, maxS);
    BeginScissorMode((int)r.x, (int)r.y, (int)r.width, (int)r.height);
    g_inScroll = true;
}
void endScroll() {
    EndScissorMode();
    g_inScroll = false;
    float maxS = std::max(0.f, g_scrollContent - g_scrollR.height);
    if (maxS > 0) {
        float h = g_scrollR.height * g_scrollR.height / g_scrollContent;
        float y = g_scrollR.y + (*g_scrollV / maxS) * (g_scrollR.height - h);
        DrawRectangle((int)(g_scrollR.x + g_scrollR.width - S(4)), (int)y, (int)S(4), (int)h, Color{255, 255, 255, 60});
    }
}
Rectangle scrollRect() { return g_scrollR; }

} // namespace ui
} // namespace ax
