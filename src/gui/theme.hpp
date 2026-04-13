#pragma once

// ── The Grid — color system ───────────────────────────────────────────────────
//
// Every value in this file comes directly from UIDirect.md.
// Nothing is invented here. The Grid is exact.

#include <imgui.h>
#include <cmath>

namespace gui::theme {

// ── Raw palette — IM_COL32 (for ImDrawList calls) ────────────────────────────

constexpr ImU32 VOID         = IM_COL32(0x00, 0x00, 0x00, 0xFF); // absolute black
constexpr ImU32 GRID_DARK    = IM_COL32(0x05, 0x0A, 0x0F, 0xFF); // panel fill
constexpr ImU32 GRID_MID     = IM_COL32(0x0A, 0x15, 0x20, 0xFF); // elevated surfaces
constexpr ImU32 LINE_DIM     = IM_COL32(0x0D, 0x20, 0x35, 0xFF); // structural lines
constexpr ImU32 LINE_MID     = IM_COL32(0x1A, 0x40, 0x60, 0xFF); // secondary borders
constexpr ImU32 LINE_ACTIVE  = IM_COL32(0x00, 0xC8, 0xFF, 0xFF); // primary cyan
constexpr ImU32 LINE_BRIGHT  = IM_COL32(0x40, 0xDF, 0xFF, 0xFF); // highlighted borders
constexpr ImU32 TEXT_BRIGHT  = IM_COL32(0xFF, 0xFF, 0xFF, 0xFF); // critical values
constexpr ImU32 TEXT_PRIMARY = IM_COL32(0xC0, 0xE8, 0xF8, 0xFF); // main text
constexpr ImU32 TEXT_SECONDARY=IM_COL32(0x50, 0x90, 0xB0, 0xFF); // labels
constexpr ImU32 TEXT_GHOST   = IM_COL32(0x1A, 0x40, 0x60, 0xFF); // disabled
constexpr ImU32 STATUS_VALID = IM_COL32(0x00, 0xFF, 0x9F, 0xFF); // VALID — system running
constexpr ImU32 STATUS_STALE = IM_COL32(0xFF, 0x8C, 0x00, 0xFF); // STALE — needs recompile
constexpr ImU32 STATUS_DEAD  = IM_COL32(0xFF, 0x22, 0x44, 0xFF); // ERROR / UNCOMPUTED
constexpr ImU32 AXIS_X       = IM_COL32(0x00, 0xC8, 0xFF, 0xFF); // X — primary cyan
constexpr ImU32 AXIS_Y       = IM_COL32(0xFF, 0x8C, 0x00, 0xFF); // Y — amber
constexpr ImU32 AXIS_Z       = IM_COL32(0xFF, 0xFF, 0xFF, 0xFF); // Z — pure white

// ── ImVec4 versions (for ImGui::PushStyleColor) ───────────────────────────────

inline ImVec4 v4(ImU32 c) {
    return {
        ((c >> IM_COL32_R_SHIFT) & 0xFF) / 255.f,
        ((c >> IM_COL32_G_SHIFT) & 0xFF) / 255.f,
        ((c >> IM_COL32_B_SHIFT) & 0xFF) / 255.f,
        ((c >> IM_COL32_A_SHIFT) & 0xFF) / 255.f
    };
}

inline ImU32 with_alpha(ImU32 c, uint8_t a) {
    return (c & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT);
}

// ── Glow system ───────────────────────────────────────────────────────────────
//
// Every light-emitting element has a glow. This is implemented as layered
// draw calls with decreasing alpha and increasing size/thickness.
// The order is: wide ambient first, then mid, then tight, then core line.

namespace glow {

// Glowing line — 4 passes (back to front)
inline void line(ImDrawList* dl, ImVec2 p1, ImVec2 p2,
                 ImU32 color = LINE_ACTIVE, float base_w = 1.0f) {
    ImU32 c0 = with_alpha(color, 20);
    ImU32 c1 = with_alpha(color, 50);
    ImU32 c2 = with_alpha(color, 110);
    dl->AddLine(p1, p2, c0, base_w * 6.0f);
    dl->AddLine(p1, p2, c1, base_w * 3.0f);
    dl->AddLine(p1, p2, c2, base_w * 1.8f);
    dl->AddLine(p1, p2, color, base_w);
}

// Glowing rectangle border — 4 passes
inline void rect(ImDrawList* dl, ImVec2 mn, ImVec2 mx,
                 ImU32 color = LINE_ACTIVE, float rounding = 0.f) {
    ImVec2 e1 = {mn.x-6, mn.y-6}, e2 = {mx.x+6, mx.y+6};
    ImVec2 e3 = {mn.x-3, mn.y-3}, e4 = {mx.x+3, mx.y+3};
    ImVec2 e5 = {mn.x-1, mn.y-1}, e6 = {mx.x+1, mx.y+1};
    dl->AddRect(e1, e2, with_alpha(color, 15), rounding+6, 0, 4.0f);
    dl->AddRect(e3, e4, with_alpha(color, 40), rounding+3, 0, 2.0f);
    dl->AddRect(e5, e6, with_alpha(color, 90), rounding+1, 0, 1.5f);
    dl->AddRect(mn, mx, color, rounding, 0, 1.0f);
}

// Glowing point
inline void point(ImDrawList* dl, ImVec2 p, ImU32 color = LINE_ACTIVE, float r = 3.f) {
    dl->AddCircleFilled(p, r + 4.f, with_alpha(color, 20));
    dl->AddCircleFilled(p, r + 2.f, with_alpha(color, 50));
    dl->AddCircleFilled(p, r,       color);
}

} // namespace glow

// ── Traveling border light ────────────────────────────────────────────────────
// Draws a bright moving point along a rectangle's perimeter.
// t is in [0, perimeter). Call each frame with t advancing at 30px/s.

inline void traveling_light(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float t) {
    float w  = mx.x - mn.x;
    float h  = mx.y - mn.y;
    float perimeter = 2.f * (w + h);
    t = std::fmod(t, perimeter);
    if(t < 0) t += perimeter;

    ImVec2 p;
    if     (t < w)         p = {mn.x + t,          mn.y};          // top
    else if(t < w + h)     p = {mx.x,               mn.y + (t-w)};  // right
    else if(t < 2*w + h)   p = {mx.x - (t-w-h),    mx.y};          // bottom
    else                   p = {mn.x,               mx.y - (t-2*w-h)}; // left

    constexpr float seg = 12.f;
    ImVec2 q;
    float nt = t - seg;
    if     (nt < 0)         q = mn;
    else if(nt < w)         q = {mn.x + nt,         mn.y};
    else if(nt < w + h)     q = {mx.x,              mn.y + (nt-w)};
    else if(nt < 2*w + h)   q = {mx.x - (nt-w-h),  mx.y};
    else                    q = {mn.x,              mx.y - (nt-2*w-h)};

    dl->AddLine(q, p, with_alpha(LINE_BRIGHT, 180), 1.5f);
    dl->AddLine(q, p, with_alpha(LINE_BRIGHT, 60),  3.5f);
}

// ── Pressure field colormap ───────────────────────────────────────────────────

struct RGB8 { uint8_t r, g, b; };

inline RGB8 colormap_mag(double t) {
    // void → deep blue → mid blue → blue-cyan → cyan → electric → near-white → white
    static const RGB8 stops[9] = {
        {0x00, 0x00, 0x00},  // 0.00  void
        {0x00, 0x15, 0x20},  // 0.10  barely present
        {0x00, 0x30, 0x60},  // 0.25  deep blue
        {0x00, 0x58, 0x99},  // 0.40  mid blue
        {0x00, 0x88, 0xCC},  // 0.55  blue-cyan
        {0x00, 0xAA, 0xEE},  // 0.70  bright cyan
        {0x00, 0xCC, 0xFF},  // 0.85  electric
        {0x80, 0xEE, 0xFF},  // 0.95  near-white cyan
        {0xFF, 0xFF, 0xFF},  // 1.00  white-hot
    };
    static const float positions[9] = {
        0.00f, 0.10f, 0.25f, 0.40f, 0.55f, 0.70f, 0.85f, 0.95f, 1.00f
    };
    t = std::clamp(t, 0.0, 1.0);
    float ft = (float)t;
    int i = 0;
    for(; i < 7; ++i) if(positions[i+1] >= ft) break;
    float span = positions[i+1] - positions[i];
    float f    = (span > 0) ? (ft - positions[i]) / span : 0.f;
    const auto& a = stops[i]; const auto& b = stops[i+1];
    return { uint8_t(a.r + f*(b.r-a.r)),
             uint8_t(a.g + f*(b.g-a.g)),
             uint8_t(a.b + f*(b.b-a.b)) };
}

inline RGB8 colormap_phase(double phi) {
    // -π→deep blue, -π/2→cyan, 0→white, +π/2→amber, +π→deep blue (circular)
    static const RGB8 stops[5] = {
        {0x00, 0x00, 0xCC},  // -π
        {0x00, 0xC8, 0xFF},  // -π/2
        {0xFF, 0xFF, 0xFF},  //  0
        {0xFF, 0x8C, 0x00},  // +π/2
        {0x00, 0x00, 0xCC},  // +π
    };
    // Normalise phi to [0, 1)
    constexpr double pi = 3.14159265358979323846;
    double t = (phi + pi) / (2.0 * pi);
    t = t - std::floor(t);
    float ft = (float)(t * 4.0);
    int   i  = std::min((int)ft, 3);
    float f  = ft - i;
    const auto& a = stops[i]; const auto& b = stops[i+1];
    return { uint8_t(a.r + f*(b.r-a.r)),
             uint8_t(a.g + f*(b.g-a.g)),
             uint8_t(a.b + f*(b.b-a.b)) };
}

// ── ImGui style application ───────────────────────────────────────────────────

inline void apply() {
    ImGuiStyle& s = ImGui::GetStyle();

    // The Grid has no rounded corners
    s.WindowRounding    = 0.f;
    s.FrameRounding     = 0.f;
    s.GrabRounding      = 0.f;
    s.TabRounding       = 0.f;
    s.ScrollbarRounding = 0.f;
    s.PopupRounding     = 0.f;
    s.ChildRounding     = 0.f;

    s.WindowPadding    = {12.f, 10.f};
    s.FramePadding     = { 6.f,  4.f};
    s.ItemSpacing      = { 6.f,  5.f};
    s.ItemInnerSpacing = { 4.f,  4.f};
    s.IndentSpacing    = 14.f;
    s.ScrollbarSize    = 10.f;
    s.GrabMinSize      =  6.f;
    s.WindowBorderSize = 1.f;
    s.FrameBorderSize  = 1.f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                  = v4(TEXT_PRIMARY);
    c[ImGuiCol_TextDisabled]          = v4(TEXT_GHOST);
    c[ImGuiCol_WindowBg]              = v4(GRID_DARK);
    c[ImGuiCol_ChildBg]               = v4(GRID_DARK);
    c[ImGuiCol_PopupBg]               = v4(GRID_MID);
    c[ImGuiCol_Border]                = v4(LINE_DIM);
    c[ImGuiCol_BorderShadow]          = {0,0,0,0};
    c[ImGuiCol_FrameBg]               = v4(GRID_MID);
    c[ImGuiCol_FrameBgHovered]        = v4(with_alpha(LINE_ACTIVE, 30));
    c[ImGuiCol_FrameBgActive]         = v4(with_alpha(LINE_ACTIVE, 50));
    c[ImGuiCol_TitleBg]               = v4(VOID);
    c[ImGuiCol_TitleBgActive]         = v4(VOID);
    c[ImGuiCol_TitleBgCollapsed]      = v4(VOID);
    c[ImGuiCol_MenuBarBg]             = v4(VOID);
    c[ImGuiCol_ScrollbarBg]           = v4(GRID_DARK);
    c[ImGuiCol_ScrollbarGrab]         = v4(LINE_DIM);
    c[ImGuiCol_ScrollbarGrabHovered]  = v4(LINE_MID);
    c[ImGuiCol_ScrollbarGrabActive]   = v4(LINE_ACTIVE);
    c[ImGuiCol_CheckMark]             = v4(LINE_ACTIVE);
    c[ImGuiCol_SliderGrab]            = v4(LINE_ACTIVE);
    c[ImGuiCol_SliderGrabActive]      = v4(LINE_BRIGHT);
    c[ImGuiCol_Button]                = v4(GRID_MID);
    c[ImGuiCol_ButtonHovered]         = v4(with_alpha(LINE_ACTIVE, 40));
    c[ImGuiCol_ButtonActive]          = v4(with_alpha(LINE_ACTIVE, 70));
    c[ImGuiCol_Header]                = v4(with_alpha(LINE_ACTIVE, 30));
    c[ImGuiCol_HeaderHovered]         = v4(with_alpha(LINE_ACTIVE, 50));
    c[ImGuiCol_HeaderActive]          = v4(with_alpha(LINE_ACTIVE, 80));
    c[ImGuiCol_Separator]             = v4(LINE_DIM);
    c[ImGuiCol_SeparatorHovered]      = v4(LINE_ACTIVE);
    c[ImGuiCol_SeparatorActive]       = v4(LINE_BRIGHT);
    c[ImGuiCol_ResizeGrip]            = {0,0,0,0};
    c[ImGuiCol_ResizeGripHovered]     = v4(LINE_ACTIVE);
    c[ImGuiCol_ResizeGripActive]      = v4(LINE_BRIGHT);
    c[ImGuiCol_Tab]                   = v4(GRID_MID);
    c[ImGuiCol_TabHovered]            = v4(with_alpha(LINE_ACTIVE, 50));
    c[ImGuiCol_TabActive]             = v4(with_alpha(LINE_ACTIVE, 70));
    c[ImGuiCol_TabUnfocused]          = v4(GRID_DARK);
    c[ImGuiCol_TabUnfocusedActive]    = v4(GRID_MID);
    c[ImGuiCol_DockingPreview]        = v4(with_alpha(LINE_ACTIVE, 50));
    c[ImGuiCol_DockingEmptyBg]        = v4(VOID);
    c[ImGuiCol_PlotLines]             = v4(LINE_ACTIVE);
    c[ImGuiCol_PlotHistogram]         = v4(LINE_ACTIVE);
    c[ImGuiCol_TableHeaderBg]         = v4(GRID_MID);
    c[ImGuiCol_TableBorderStrong]     = v4(LINE_MID);
    c[ImGuiCol_TableBorderLight]      = v4(LINE_DIM);
    c[ImGuiCol_NavHighlight]          = v4(LINE_ACTIVE);
    c[ImGuiCol_NavWindowingHighlight] = v4(LINE_ACTIVE);
}

} // namespace gui::theme
