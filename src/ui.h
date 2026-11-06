// ui.h : immediate-mode UI toolkit and theme
#pragma once
#include "common.h"

namespace ax {

namespace theme {
inline const Color bg = {9, 12, 18, 235};
inline const Color panel = {16, 20, 30, 230};
inline const Color panel2 = {24, 30, 44, 235};
inline const Color line = {255, 255, 255, 24};
inline const Color text = {232, 238, 248, 255};
inline const Color dim = {140, 152, 172, 255};
inline const Color accent = {41, 227, 255, 255};
inline const Color accent2 = {255, 70, 110, 255};
inline const Color gold = {255, 196, 60, 255};
inline const Color good = {80, 230, 140, 255};
inline const Color bad = {255, 84, 84, 255};
inline const Color atk = {232, 128, 62, 255};
inline const Color def = {72, 152, 236, 255};
}

enum Align { AL_LEFT = 0, AL_CENTER = 1, AL_RIGHT = 2 };

namespace ui {
void init();
void shutdown();
void beginFrame();          // call once per frame before widgets
float scale();              // UI scale (screen height / 1080)
float S(float v);           // scaled value

void text(const char* s, float x, float y, float size, Color c, bool bold = false, int align = AL_LEFT);
void textShadow(const char* s, float x, float y, float size, Color c, bool bold = false, int align = AL_LEFT);
float textWidth(const char* s, float size, bool bold = false);
const char* fmt(const char* f, ...);

void panel(Rectangle r, Color c = theme::panel, float round = 0.0f);
void frame(Rectangle r, Color c, float thick = 1);
bool hovered(Rectangle r);

bool button(Rectangle r, const char* label, bool primary = false, bool enabled = true);
bool tab(Rectangle r, const char* label, bool active);
bool slider(Rectangle r, const char* label, float* v, float mn, float mx, const char* valueFmt = "%.2f", float step = 0);
bool sliderInt(Rectangle r, const char* label, int* v, int mn, int mx);
bool toggle(Rectangle r, const char* label, bool* v);
bool choice(Rectangle r, const char* label, int* idx, const char* const* opts, int n);
bool keybind(Rectangle r, int* code, int id);
bool textField(Rectangle r, char* buf, int cap, int id, const char* placeholder = "");
void tooltip(const char* s);
bool capturingKey();         // true while waiting for a key bind
void cancelCapture();
bool anyTextFocused();
void clearFocus();

// scrolling region
void beginScroll(Rectangle r, float* scroll, float contentH);
void endScroll();
Rectangle scrollRect();
}

} // namespace ax
